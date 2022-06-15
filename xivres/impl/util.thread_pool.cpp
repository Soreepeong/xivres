#include "../include/xivres/util.thread_pool.h"

xivres::util::thread_pool::pool::pool(size_t nConcurrentExecutions)
	: m_nConcurrency(nConcurrentExecutions == (std::numeric_limits<size_t>::max)() ? std::thread::hardware_concurrency() : (std::max<size_t>)(1, nConcurrentExecutions))
	, m_nWaitingThreads(0)
	, m_nFreeThreads(0)
	, m_bQuitting(false)
	, m_pmtxTask(std::make_shared<std::mutex>()) {
}

xivres::util::thread_pool::pool::~pool() {
	m_bQuitting = true;
	m_cvTask.notify_one();

	std::unique_lock lock(*m_pmtxTask);
	while (!m_mapThreads.empty())
		m_cvTask.wait(lock);
}

xivres::util::thread_pool::pool& xivres::util::thread_pool::pool::instance() {
	static pool s_instance;
	return s_instance;
}

xivres::util::thread_pool::pool& xivres::util::thread_pool::pool::current() {
	return instance();
}

void xivres::util::thread_pool::pool::concurrency(size_t newConcurrency) {
	std::lock_guard lock(*m_pmtxTask);
	m_nConcurrency = newConcurrency;
	m_cvTask.notify_one();
}

size_t xivres::util::thread_pool::pool::concurrency() const {
	return m_nConcurrency;
}

bool xivres::util::thread_pool::untyped_task::operator<(const untyped_task& r) const {
	return InvokePath > r.InvokePath;
}

xivres::util::on_dtor xivres::util::thread_pool::pool::release_working_status() {
	std::lock_guard lock(*m_pmtxTask);
	if (!m_mapThreads.contains(std::this_thread::get_id()))
		return {};

	m_nWaitingThreads += 1;

	if (!m_pqTasks.empty() && m_nFreeThreads == 0 && m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency)
		new_worker();

	m_cvTask.notify_one();

	return { [this]() { m_nWaitingThreads -= 1; } };
}

void xivres::util::thread_pool::pool::worker(const size_t threadIndex) {
	std::shared_ptr<std::mutex> pmtxTask(m_pmtxTask);
	std::unique_lock lock(*pmtxTask);
	std::shared_ptr<untyped_task> pTask;

	const auto it = m_mapThreads.find(std::this_thread::get_id());

	while (true) {
		m_nWaitingThreads++;
		m_nFreeThreads++;
		m_cvTask.wait(lock, [this]() { return m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency && (!m_pqTasks.empty() || m_bQuitting); });
		m_nWaitingThreads--;
		m_nFreeThreads--;

		if (m_pqTasks.empty())
			break;

		pTask = std::move(const_cast<std::shared_ptr<untyped_task>&>(m_pqTasks.top()));
		m_pqTasks.pop();

		it->second.TaskStack.emplace_back(pTask.get());
		lock.unlock();
		(*pTask)();
		OnTaskComplete(pTask.get());
		pTask.reset();
		m_cvTask.notify_one();
		lock.lock();
		it->second.TaskStack.pop_back();
	}

	it->second.Thread.detach();
	m_mapThreads.erase(it);

	m_cvTask.notify_one();
	lock.unlock();
}

void xivres::util::thread_pool::pool::new_worker() {
	std::mutex mtx;
	std::condition_variable cv;
	bool ready = false;
	std::thread thr;
	std::unique_lock lock(mtx);

	thr = std::thread([this, &mtx, &cv, &ready, &thr]() {
		const auto i = m_mapThreads.size();
		{
			std::lock_guard lock(mtx);
			ready = true;
			auto& threadInfo = m_mapThreads[std::this_thread::get_id()];
			threadInfo.Thread = std::move(thr);
			cv.notify_one();
		}
		worker(i);
	});

	cv.wait(lock, [&ready]() { return ready; });
}

xivres::util::thread_pool::object_pool<std::vector<uint8_t>>::scoped_pooled_object xivres::util::thread_pool::pooled_byte_buffer() {
	static object_pool<std::vector<uint8_t>> s_pool([](size_t c, std::vector<uint8_t>& buf) { return c / 2 < std::thread::hardware_concurrency() && buf.size() < 1048576; });
	return *s_pool;
}
