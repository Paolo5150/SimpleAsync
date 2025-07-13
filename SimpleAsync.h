#pragma once
#include <thread>
#include <future>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include "ThreadPool.h"

class AsyncTaskWrapper {
public:
	virtual ~AsyncTaskWrapper() = default;
	virtual bool CheckAndExecuteCallback() = 0;
	virtual void ForceWait() = 0;
	virtual uint32_t GetId() const = 0;
};

template<class T>
class ConcreteAsyncTaskWrapper : public AsyncTaskWrapper {
public:
	uint32_t ID;
	std::future<T> Task;
	std::function<void(T)> Callback;
	bool CallbackInvoked = false;

	ConcreteAsyncTaskWrapper(uint32_t id, std::future<T>&& task, std::function<void(T)> callback)
		: ID(id), Task(std::move(task)), Callback(std::move(callback)) {}

	uint32_t GetId() const override { return ID; }

	void ForceWait() override {
		if (Task.valid() && !CallbackInvoked) {
			try {
				Task.wait();
				T result = Task.get();
				if (Callback) Callback(result);
			}
			catch (...) {
				// Optionally log error
				// std::cerr << "AsyncTask Error: " << e.what() << std::endl;
			}
			CallbackInvoked = true;
		}
	}

	bool CheckAndExecuteCallback() override {
		if (Task.valid()) {
			if (Task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				try {
					if (!CallbackInvoked) {
						CallbackInvoked = true;
						T result = Task.get();
						if (Callback) Callback(result);
					}
					return true;
				}
				catch (...) {
					// std::cerr << "AsyncTask Exception: " << e.what() << std::endl;
					return true;
				}
			}
			return false;
		}
		return true;
	}
};

/**
 * A lightweight asynchronous task manager.
 *
 * Allows scheduling tasks to run in a separate thread, and specifies a callback to be invoked
 * on the main thread. Meant to be driven by `SimpleAsync::Update()` on the main thread.
 *
 * Example usage:
 *
 * 1. Define the async task:
 *    std::function<MeshData(uint32_t, uint32_t, std::function<void(...)>)> task = ...;
 *
 * 2. Define the callback:
 *    std::function<void(MeshData)> callback = ...;
 *
 * 3. (Optional) Pass function arguments:
 *    std::function<void(...args...)> heightFunc = ...;
 *
 * 4. Create the task:
 *    SimpleAsync::CreateTask(task, callback, 100, 100, std::move(heightFunc));
 */
class SimpleAsync
{
public:
	template<typename Func, typename Callback, typename... Args>
	[[nodiscard]] static uint32_t CreateTask(Func&& task, Callback&& callback, Args&&... args)
	{
		using ReturnType = std::invoke_result_t<Func, Args...>;
		auto f = m_threadPool.EnqueueTask(std::forward<Func>(task), std::forward<Args>(args)...);
		auto asyncTask = std::make_unique<ConcreteAsyncTaskWrapper<ReturnType>>(m_id, std::move(f), std::forward<Callback>(callback));

		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_tasks[m_id] = std::move(asyncTask);
		return m_id++;
	}

	static void ForceWait(uint32_t id)
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		auto it = m_tasks.find(id);
		if (it != m_tasks.end()) {
			it->second->ForceWait();
			m_tasks.erase(it);
		}
	}

	static void Update()
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		for (auto it = m_tasks.begin(); it != m_tasks.end(); )
		{
			if (it->second->CheckAndExecuteCallback()) {
				it = m_tasks.erase(it);
			}
			else {
				++it;
			}
		}
	}
	static void Destroy()
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_tasks.clear();
	}

private:
	inline static std::unordered_map<uint32_t, std::unique_ptr<AsyncTaskWrapper>> m_tasks;
	inline static std::mutex m_tasksMutex;
	inline static uint32_t m_id = 0;
	inline static ThreadPool m_threadPool;
};
