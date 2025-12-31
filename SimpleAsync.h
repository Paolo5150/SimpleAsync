#pragma once
#include <thread>
#include <future>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include "ThreadPool.h"

namespace {
	const std::string DefaultPoolName = "DefaultPool";
}

class AsyncTaskWrapper {
public:
	virtual ~AsyncTaskWrapper() = default;
	virtual bool CheckAndExecuteCallback() = 0;
	virtual void ForceWait() = 0;
	virtual uint32_t GetId() const = 0;
};

struct CancellationState
{
	std::atomic<bool> Canceled{ false };
};

using CancellationToken = std::shared_ptr<CancellationState>;

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
				T&& result = Task.get();
				if (Callback) Callback(std::move(result));
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
						T&& result = Task.get();
						if (Callback) Callback(std::move(result));
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

class SimpleAsync
{
public:
	
	template<typename Func, typename Callback, typename... Args>
	static uint32_t CreateTask(Func&& task,	Callback&& callback,Args&&... args)
	{
		return CreateTaskInPool(
			m_defaultPoolName,
			std::forward<Func>(task),
			std::forward<Callback>(callback),
			std::forward<Args>(args)...);
	}

	template<typename Func, typename Callback, typename... Args>
	static uint32_t CreateTaskInPool(const std::string& poolName, Func&& task, Callback&& callback, Args&&... args)
	{
		if (!m_initialized)
		{
			throw std::runtime_error("Initialize was never called!");
		}
		auto pool = m_threadPools.find(poolName);
		if(pool == m_threadPools.end())
			throw std::runtime_error("Thread pool does not exist");

		uint32_t id = m_id++;

		auto token = std::make_shared<CancellationState>();

		auto boundTask = [t = std::forward<Func>(task), token, argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> decltype(auto)
			{
				auto callWithArgs = [&](auto&&... unpackedArgs) -> decltype(auto) {
					return t(token, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
					};

				return std::apply(callWithArgs, std::move(argsTuple));
			};

		auto future = pool->second->EnqueueTask(std::move(boundTask));

		using ReturnType = decltype(task(token, std::forward<Args>(args)...));

		auto asyncTask = std::make_unique<ConcreteAsyncTaskWrapper<ReturnType>>(
			id, std::move(future), std::forward<Callback>(callback));

		m_tasks[id] = std::move(asyncTask);
		m_cancellations[id] = token;

		return id;
	}


	static void ForceWait(uint32_t id)
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		auto it = m_tasks.find(id);
		if (it != m_tasks.end()) {
			it->second->ForceWait();
			m_tasks.erase(it);
			m_cancellations.erase(id);
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

	static void Cancel(uint32_t id)
	{
		auto it = m_tasks.find(id);
		if (it != m_tasks.end())
		{
			m_cancellations[id]->Canceled.store(true);
		}
	}

	static void CreatePool(const std::string& poolName, size_t threadsCount)
	{
		if (poolName.empty())
			throw std::runtime_error("Pool name cannot be empty");

		auto it = m_threadPools.find(poolName);
		if(it != m_threadPools.end())
			throw std::runtime_error("Pool name already exists");

		m_threadPools[poolName] = std::make_unique<ThreadPool>(threadsCount);
	}

	static void Initialize(const std::string& defaultPoolName = DefaultPoolName, size_t maxThreads = std::thread::hardware_concurrency())
	{
		auto poolName = defaultPoolName;
		if (poolName.empty())
			poolName = DefaultPoolName;

		m_defaultPoolName = poolName;
		m_threadPools[m_defaultPoolName] = std::make_unique<ThreadPool>(maxThreads);
		m_initialized = true;
	}

	static uint32_t GetAvailableThreads(const std::string& poolName)
	{
		auto it = m_threadPools.find(poolName);
		if (it == m_threadPools.end())
			throw std::runtime_error("Pool does not exists");

		return it->second->GetAvailableThreads();
	}

	static void Destroy()
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_tasks.clear();
		m_threadPools.clear();
	}

private:
	inline static std::unordered_map<uint32_t, std::unique_ptr<AsyncTaskWrapper>> m_tasks;
	inline static std::unordered_map<uint32_t, CancellationToken> m_cancellations;
	inline static std::mutex m_tasksMutex;
	inline static uint32_t m_id = 0;
	inline static std::unordered_map<std::string, std::unique_ptr<ThreadPool>> m_threadPools;
	inline static bool m_initialized = false;
	inline static std::string m_defaultPoolName;
};
