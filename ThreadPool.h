#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <vector>
#include <atomic>

class ThreadPool
{
public:
	ThreadPool(size_t numOfThreads) : m_stop(false)
	{
		for (size_t i = 0; i < numOfThreads; i++)
		{
			m_workers.emplace_back([this]() {

				std::function<void()> task;
				while (1)
				{
					{
						std::unique_lock l(m_mutex);
						m_condition.wait(l, [this]() { return m_stop || !m_tasks.empty(); });
						if (m_stop && m_tasks.empty()) return;

						task = std::move(m_tasks.front());
						m_tasks.pop();
					}

					m_activeThreads.fetch_add(1, std::memory_order_relaxed);
					try
					{
						task();
					}
					catch (...)
					{
					}
					m_activeThreads.fetch_sub(1, std::memory_order_relaxed);


				}
				});
		}
	}

	~ThreadPool()
	{
		{
			std::scoped_lock l(m_mutex);
			m_stop = true;
		}
		m_condition.notify_all();
		for (auto& w : m_workers)
		{
			if (w.joinable())
				w.join();
		}

	}

	uint32_t GetActiveThreadsCount() const
	{
		return m_activeThreads.load();
	}

	template<typename Func, typename... Args>
	auto EnqueueTask(Func&& func, Args&&... args) -> std::future<std::invoke_result_t<Func, Args...>>
	{
		using ReturnType = std::invoke_result_t<Func, Args...>;

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);

		std::future<ReturnType> res = task->get_future();

		{
			std::scoped_lock l(m_mutex);
			if (m_stop) throw std::runtime_error("Enqueue on stop thread pool");
			m_tasks.emplace([task]() { (*task)(); });
		}

		m_condition.notify_one();
		return res;
	}


private:
	std::vector<std::thread> m_workers;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	std::queue<std::function<void()>> m_tasks;
	std::atomic<uint32_t> m_activeThreads = 0;
	bool m_stop;
};

	
