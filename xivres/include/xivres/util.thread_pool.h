#ifndef XIVRES_INTERNAL_THREADPOOL_H_
#define XIVRES_INTERNAL_THREADPOOL_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <type_traits>

#include "util.listener_manager.h"
#include "util.on_dtor.h"

namespace xivres::util::thread_pool {
	class pool;

	class cancelled_error : public std::runtime_error {
	public:
		cancelled_error() : runtime_error("Execution cancelled.") {}
	};

	class untyped_task {
	protected:
		pool& m_pool;
		bool m_bCancelled = false;

	public:
		untyped_task(pool& pool)
			: m_pool(pool) {
		}

		untyped_task(untyped_task&&) = delete;
		untyped_task(const untyped_task&) = delete;
		untyped_task& operator=(untyped_task&&) = delete;
		untyped_task& operator=(const untyped_task&) = delete;

		virtual ~untyped_task() = default;

		virtual void operator()() = 0;

		void cancel() {
			m_bCancelled = true;
		}

		[[nodiscard]] virtual bool finished() const = 0;

		[[nodiscard]] bool cancelled() const {
			return m_bCancelled;
		}

		void throw_if_cancelled() const {
			if (cancelled())
				throw cancelled_error();
		}

	protected:
		[[nodiscard]] on_dtor release_working_status() const;
	};

	template<typename R>
	class task : public untyped_task {
		std::packaged_task<R(task<R>&)> m_task;
		std::future<R> m_future;

	public:
		task(pool& pool, std::function<R(task<R>&)> fn)
			: untyped_task(pool)
			, m_task(std::move(fn))
			, m_future(m_task.get_future()) {
		}

		void operator()() override {
			m_task(*this);
		}

		[[nodiscard]] bool finished() const override {
			return m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
		}

		void wait() const {
			const auto releaseWorkingStatus = release_working_status();
			return m_future.wait();
		}

		template <class Rep, class Per>
		std::future_status wait_for(const std::chrono::duration<Rep, Per>& relTime) const {
			const auto releaseWorkingStatus = release_working_status();
			return m_future.wait_for(relTime);
		}

		template <class Clock, class Dur>
		std::future_status wait_until(const std::chrono::time_point<Clock, Dur>& absTime) const {
			const auto releaseWorkingStatus = release_working_status();
			return m_future.wait_until(absTime);
		}

		[[nodiscard]] R get() {
			return m_future.get();
		}
	};

	template<typename T>
	class scoped_tls {
		const std::function<void(T&)> m_fnInit;
		mutable std::shared_mutex m_mtx;
		mutable std::map<std::thread::id, T> m_storage;

	public:
		scoped_tls(std::function<void(T&)> initfn = {})
			: m_fnInit(std::move(initfn)) {
		}
		
		T& operator*() const {
			{
				std::shared_lock slock(m_mtx);
				if (const auto it = m_storage.find(std::this_thread::get_id()); it != m_storage.end())
					return it->second;
			}
			{
				std::lock_guard lock(m_mtx);
				auto& data = m_storage[std::this_thread::get_id()];
				if (m_fnInit)
					m_fnInit(data);
				return data;
			}
		}

		T* operator->() const {
			return &**this;
		}
	};
	
	class pool {
		std::map<std::thread::id, std::thread> m_mapThreads;
		size_t m_nConcurrency;
		size_t m_nWaitingThreads;
		size_t m_nFreeThreads;
		bool m_bQuitting;

		std::shared_ptr<std::mutex> m_pmtxTask;
		std::deque<std::shared_ptr<untyped_task>> m_dqTasks;
		std::condition_variable m_cvTask;

	public:
		listener_manager<pool, void, untyped_task*> OnTaskComplete;

		pool(size_t nConcurrentExecutions = (std::numeric_limits<size_t>::max)())
			: m_nConcurrency(nConcurrentExecutions == (std::numeric_limits<size_t>::max)() ? std::thread::hardware_concurrency() : (std::max<size_t>)(1, nConcurrentExecutions))
			, m_nWaitingThreads(0)
			, m_nFreeThreads(0)
			, m_bQuitting(false)
			, m_pmtxTask(std::make_shared<std::mutex>()) {
		}

		pool(pool&&) = delete;
		pool(const pool&) = delete;
		pool& operator=(pool&&) = delete;
		pool& operator=(const pool&) = delete;

		~pool() {
			m_bQuitting = true;
			m_cvTask.notify_one();

			std::unique_lock lock(*m_pmtxTask);
			while (!m_mapThreads.empty())
				m_cvTask.wait(lock);
		}

		static pool& instance() {
			static pool s_instance;
			return s_instance;
		}

		static pool& current() {
			return instance();  // TODO
		}

		void concurrency(size_t newConcurrency) {
			std::lock_guard lock(*m_pmtxTask);
			m_nConcurrency = newConcurrency;
			m_cvTask.notify_one();
		}

		[[nodiscard]] size_t concurrency() const {
			return m_nConcurrency;
		}

		template<typename TReturn = void>
		std::shared_ptr<task<TReturn>> submit(std::function<TReturn(task<TReturn>&)> fn) {
			std::unique_lock lock(*m_pmtxTask, std::defer_lock);

			auto t = std::make_shared<task<TReturn>>(*this, std::move(fn));

			lock.lock();
			m_dqTasks.emplace_back(t);
			lock.unlock();

			m_cvTask.notify_one();

			if (m_nFreeThreads == 0 && m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency) {
				lock.lock();
				if (!m_dqTasks.empty())
					new_worker();
				lock.unlock();
			}

			return t;
		}

		[[nodiscard]] on_dtor release_working_status() {
			{
				std::lock_guard lock(*m_pmtxTask);
				if (!m_mapThreads.contains(std::this_thread::get_id()))
					return {};

				m_nWaitingThreads += 1;

				if (!m_dqTasks.empty() && m_nFreeThreads == 0 && m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency)
					new_worker();

				m_cvTask.notify_one();
			}

			return { [this]() {
				std::unique_lock lock(*m_pmtxTask);
				m_nWaitingThreads -= 1;
			} };
		}

	private:
		void worker(const size_t threadIndex) {
			std::shared_ptr<std::mutex> pmtxTask(m_pmtxTask);
			std::unique_lock lock(*pmtxTask);
			std::shared_ptr<untyped_task> pTask;

			while (true) {
				m_nWaitingThreads++;
				m_nFreeThreads++;
				m_cvTask.wait(lock, [this]() { return m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency && (!m_dqTasks.empty() || m_bQuitting); });
				m_nWaitingThreads--;
				m_nFreeThreads--;

				if (m_dqTasks.empty())
					break;

				pTask = std::move(m_dqTasks.front());
				m_dqTasks.pop_front();

				lock.unlock();
				(*pTask)();
				OnTaskComplete(pTask.get());
				pTask.reset();
				m_cvTask.notify_one();
				lock.lock();
			}

			auto it = m_mapThreads.find(std::this_thread::get_id());
			it->second.detach();
			m_mapThreads.erase(it);

			m_cvTask.notify_one();
			lock.unlock();
		}

		void new_worker() {
			std::mutex mtx;
			std::condition_variable cv;
			bool ready = false;
			std::thread thr;
			thr = std::thread([this, &mtx, &cv, &ready, &thr]() {
				const auto i = m_mapThreads.size();
				{
					std::lock_guard lock(mtx);
					ready = true;
					m_mapThreads.emplace(std::this_thread::get_id(), std::move(thr));
					cv.notify_one();
				}
				worker(i);
			});

			{
				std::unique_lock lock(mtx);
				cv.wait(lock, [&ready]() { return ready; });
			}
		}
	};

	inline on_dtor untyped_task::release_working_status() const {
		return m_pool.release_working_status();
	}

	template<typename TReturn = void>
	class task_waiter {
		using TPackagedTask = task<TReturn>;

		pool& m_pool;
		std::mutex m_mtx;
		std::map<void*, std::shared_ptr<TPackagedTask>> m_mapPending;

		std::deque<std::shared_ptr<TPackagedTask>> m_dqFinished;
		std::condition_variable m_cvFinished;

		on_dtor::multi m_dtors;

	public:
		listener_manager<pool, void, TPackagedTask*> OnTaskComplete;

		task_waiter(pool& pool = pool::current())
			: m_pool(pool) {
			m_dtors += m_pool.OnTaskComplete([this](untyped_task* pTask) {
				std::lock_guard lock(m_mtx);
				if (const auto it = m_mapPending.find(pTask); it != m_mapPending.end()) {
					m_dqFinished.emplace_back(std::move(it->second));
					m_cvFinished.notify_one();
					m_mapPending.erase(it);
				}
			});
		}

		task_waiter(task_waiter&&) = delete;
		task_waiter(const task_waiter&) = delete;
		task_waiter& operator=(task_waiter&&) = delete;
		task_waiter& operator=(const task_waiter&) = delete;

		~task_waiter() {
			std::unique_lock lock(m_mtx);
			for (auto& task : m_mapPending | std::views::values)
				task->cancel();

			auto release = m_pool.release_working_status();
			m_cvFinished.wait(lock, [this]() { return m_mapPending.empty(); });

			lock.unlock();
			m_dtors.clear();
		}

		[[nodiscard]] size_t pending() {
			std::lock_guard lock(m_mtx);
			return m_mapPending.size();
		}

		[[nodiscard]] pool& pool() const {
			return m_pool;
		}

		void operator+=(std::shared_ptr<TPackagedTask> task) {
			std::lock_guard lock(m_mtx);
			if (task->finished()) {
				m_dqFinished.emplace_back(std::move(task));
				m_cvFinished.notify_one();
			} else {
				m_mapPending.emplace(task.get(), std::move(task));
			}
		}
		
		void submit(std::function<TReturn(task<TReturn>&)> fn) {
			*this += m_pool.submit<TReturn>(std::move(fn));
		}

		template<typename = std::enable_if_t<!std::is_same_v<TReturn, void>>>
		[[nodiscard]] std::optional<TReturn> get() {
			std::unique_lock lock(m_mtx);
			if (m_mapPending.empty() && m_dqFinished.empty())
				return std::nullopt;

			auto release = m_pool.release_working_status();
			m_cvFinished.wait(lock, [this]() { return !m_dqFinished.empty(); });
			auto pTask = std::move(m_dqFinished.front());
			m_dqFinished.pop_front();
			lock.unlock();

			return pTask->get();
		}

		[[nodiscard]] bool wait_one() {
			std::unique_lock lock(m_mtx);
			if (m_mapPending.empty() && m_dqFinished.empty())
				return false;

			auto release = m_pool.release_working_status();
			m_cvFinished.wait(lock, [this]() { return !m_dqFinished.empty(); });
			auto pTask = std::move(m_dqFinished.front());
			m_dqFinished.pop_front();
			lock.unlock();

			pTask->get();
			return true;
		}

		void wait_all() {
			while (wait_one())
				void();
		}
	};
}

#endif
