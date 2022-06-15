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
#include <queue>
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
	public:
		const std::vector<uint64_t> InvokePath;

	protected:
		pool& m_pool;
		bool m_bCancelled;

	public:
		untyped_task(std::vector<uint64_t> invokePath, pool& pool)
			: InvokePath(std::move(invokePath))
			, m_pool(pool)
			, m_bCancelled(false) {
		}

		untyped_task(untyped_task&&) = delete;
		untyped_task(const untyped_task&) = delete;
		untyped_task& operator=(untyped_task&&) = delete;
		untyped_task& operator=(const untyped_task&) = delete;

		virtual ~untyped_task() = default;

		virtual void operator()() = 0;

		void cancel() { m_bCancelled = true; }

		[[nodiscard]] virtual bool finished() const = 0;

		[[nodiscard]] bool cancelled() const { return m_bCancelled; }

		void throw_if_cancelled() const {
			if (cancelled())
				throw cancelled_error();
		}

		bool operator<(const untyped_task& r) const;

	protected:
		[[nodiscard]] on_dtor release_working_status() const;
	};

	template<typename R>
	class task : public untyped_task {
		std::packaged_task<R(task<R>&)> m_task;
		std::future<R> m_future;

	public:
		task(std::vector<uint64_t> invokePath, pool& pool, std::function<R(task<R>&)> fn)
			: untyped_task(std::move(invokePath), pool)
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
	class object_pool {
		std::mutex m_mutex;
		std::vector<std::unique_ptr<T>> m_objects;
		std::function<bool(size_t, T&)> m_fnKeepCheck;

	public:
		class scoped_pooled_object {
			object_pool* m_parent;
			std::unique_ptr<T> m_object;

			friend class object_pool;

			scoped_pooled_object(object_pool* parent)
				: m_parent(parent) {

				if (m_parent->m_objects.empty())
					return;

				const auto lock = std::lock_guard(m_parent->m_mutex);
				if (m_parent->m_objects.empty())
					return;

				m_object = std::move(m_parent->m_objects.back());
				m_parent->m_objects.pop_back();
			}

		public:
			scoped_pooled_object() : m_parent(nullptr) {}

			scoped_pooled_object(scoped_pooled_object&& r)
				: m_parent(r.m_parent)
				, m_objects(std::move(r.m_object)) {
				r.m_parent = nullptr;
				r.m_object.reset();
			}

			scoped_pooled_object& operator=(scoped_pooled_object&& r) {
				if (this == &r)
					return *this;

				if (m_object && m_parent) {
					const auto lock = std::lock_guard(m_parent->m_mutex);
					if (!m_parent->m_fnKeepCheck || m_parent->m_fnKeepCheck(m_parent->m_objects.size(), *m_object))
						m_parent->m_objects.emplace_back(std::move(m_object));
				}

				m_parent = r.m_parent;
				m_object = std::move(r.m_object);

				r.m_parent = nullptr;
				r.m_object.reset();

				return *this;
			}

			scoped_pooled_object(const scoped_pooled_object&) = delete;
			scoped_pooled_object& operator=(const scoped_pooled_object&) = delete;

			~scoped_pooled_object() {
				if (m_object && m_parent) {
					const auto lock = std::lock_guard(m_parent->m_mutex);
					if (!m_parent->m_fnKeepCheck || m_parent->m_fnKeepCheck(m_parent->m_objects.size(), *m_object))
						m_parent->m_objects.emplace_back(std::move(m_object));
				}
			}

			operator bool() const {
				return !!m_object;
			}

			template<typename...TArgs>
			T& emplace(TArgs&&...args) {
				m_object = std::make_unique<T>(std::forward<TArgs>(args)...);
				return *m_object;
			}

			T& operator*() const {
				return *m_object;
			}

			T* operator->() const {
				return m_object.get();
			}
		};

		object_pool(std::function<bool(size_t, T&)> keepCheck = {})
			: m_fnKeepCheck(std::move(keepCheck)) {
		}
		object_pool(object_pool&&) = delete;
		object_pool(const object_pool&) = delete;
		object_pool& operator=(object_pool&&) = delete;
		object_pool& operator=(const object_pool&) = delete;
		~object_pool() = default;

		scoped_pooled_object operator*() {
			return { this };
		}
	};

	object_pool<std::vector<uint8_t>>::scoped_pooled_object pooled_byte_buffer();

	struct untyped_task_shared_ptr_comparator {
		bool operator()(const std::shared_ptr<untyped_task>& l, const std::shared_ptr<untyped_task>& r) {
			return *l < *r;
		}
	};

	class pool {
		friend class untyped_task;

		struct thread_info {
			std::thread Thread;
			std::vector<untyped_task*> TaskStack;
		};
		std::map<std::thread::id, thread_info> m_mapThreads;
		size_t m_nConcurrency;
		std::atomic_size_t m_nWaitingThreads;
		size_t m_nFreeThreads;
		bool m_bQuitting;

		uint64_t m_nTaskCounter = 0;
		std::priority_queue<std::shared_ptr<untyped_task>, std::vector<std::shared_ptr<untyped_task>>, untyped_task_shared_ptr_comparator> m_pqTasks;
		std::shared_ptr<std::mutex> m_pmtxTask;
		std::condition_variable m_cvTask;

	public:
		listener_manager<pool, void, untyped_task*> OnTaskComplete;

		pool(size_t nConcurrentExecutions = (std::numeric_limits<size_t>::max)());

		pool(pool&&) = delete;
		pool(const pool&) = delete;
		pool& operator=(pool&&) = delete;
		pool& operator=(const pool&) = delete;

		~pool();

		static pool& instance();

		static pool& current();

		void concurrency(size_t newConcurrency);

		[[nodiscard]] size_t concurrency() const;

		template<typename TReturn = void>
		std::shared_ptr<task<TReturn>> submit(std::function<TReturn(task<TReturn>&)> fn) {
			std::unique_lock lock(*m_pmtxTask);

			std::vector<uint64_t> invokePath;
			if (const auto it = m_mapThreads.find(std::this_thread::get_id()); it != m_mapThreads.end()) {
				const auto& parentTask = it->second.TaskStack.back();
				invokePath.reserve(parentTask->InvokePath.size() + 1);
				invokePath.insert(invokePath.end(), parentTask->InvokePath.begin(), parentTask->InvokePath.end());
			}

			invokePath.emplace_back(m_nTaskCounter++);

			auto t = std::make_shared<task<TReturn>>(std::move(invokePath), *this, std::move(fn));
			m_pqTasks.emplace(t);
			lock.unlock();

			m_cvTask.notify_one();

			if (m_nFreeThreads == 0 && m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency) {
				lock.lock();
				if (!m_pqTasks.empty())
					new_worker();
				lock.unlock();
			}

			return t;
		}

		[[nodiscard]] on_dtor release_working_status();

	private:
		void worker(const size_t threadIndex);

		void new_worker();
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
