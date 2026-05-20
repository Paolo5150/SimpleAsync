#pragma once
#include <thread>
#include <future>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include "ThreadPool.h"

namespace 
{
	const std::string DefaultPoolName = "DefaultPool";
}

class AsyncTaskWrapper 
{
public:
	virtual ~AsyncTaskWrapper() = default;
	virtual bool CheckAndExecuteCallback() = 0;
	virtual void ForceWait() = 0;
	virtual uint32_t GetId() const = 0;
};

struct AsyncOptions
{
	float TimeoutMilliseconds;
	std::function<void(uint32_t)> TimeoutCallback;
	std::function<void(float)> ProgressCallback;
};

struct CancellationState
{
	std::atomic<bool> Canceled{ false };
};

struct ProgressValue
{
	std::atomic<float> Value{0};
};

using CancellationToken = std::shared_ptr<CancellationState>;
using Progress = std::shared_ptr<ProgressValue>;

struct TaskTimeout
{
	float TimeoutMs;
	std::chrono::steady_clock::time_point StartedTime;
};

template<class T>
class ConcreteAsyncTaskWrapper : public AsyncTaskWrapper 
{
public:
	uint32_t ID;
	std::future<T> Task;
	std::function<void(T)> Callback;
	bool CallbackInvoked = false;

	ConcreteAsyncTaskWrapper(uint32_t id, std::future<T>&& task, std::function<void(T)> callback = {})
		: ID(id), Task(std::move(task)), Callback(std::move(callback)) {}

	uint32_t GetId() const override { return ID; }

	void ForceWait() override {
		if (Task.valid() && !CallbackInvoked) 
		{
			try 
			{
				Task.wait();
				T&& result = Task.get();
				if (Callback) Callback(std::move(result));
			}
			catch (...) 
			{
				// std::cerr << "AsyncTask Error: " << e.what() << std::endl;
			}
			CallbackInvoked = true;
		}
	}

	bool CheckAndExecuteCallback() override 
	{
		if (Task.valid()) 
		{
			if (Task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) 
			{
				try 
				{
					if (!CallbackInvoked) 
					{
						CallbackInvoked = true;
						T result = Task.get();
						if (Callback) Callback(std::move(result));
					}
					return true;
				}
				catch (...) 
				{
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
	static uint32_t CreateTask(Func&& task,	Callback&& callback, AsyncOptions opt , Args&&... args)
	{
		return CreateTaskInPool(
			m_defaultPoolName,
			std::forward<Func>(task),
			std::forward<Callback>(callback),
			opt,
			std::forward<Args>(args)...);
	}

	template<typename Func, typename... Args>
	static uint32_t CreateTask(Func&& task, AsyncOptions opt, Args&&... args)
	{
		return CreateTaskInPool(
			m_defaultPoolName,
			std::forward<Func>(task),
			[](auto&&) {},
			opt,
			std::forward<Args>(args)...);
	}

	template<typename Func, typename... Args>
	static uint32_t CreateTaskInPool(const std::string& poolName, Func&& task, AsyncOptions opt, Args&&... args)
	{
		return CreateTaskInPool(
			poolName,
			std::forward<Func>(task),
			[](auto&&) {},
			opt,
			std::forward<Args>(args)...);
	}

	template<typename Func, typename Callback, typename... Args>
	static uint32_t CreateTaskInPool(const std::string& poolName, Func&& task, Callback resultCB, AsyncOptions opt, Args&&... args)
	{
		if (!m_initialized)
		{
			throw std::runtime_error("Initialize was never called!");
		}
		auto pool = m_threadPools.find(poolName);
		if (pool == m_threadPools.end())
			throw std::runtime_error("Thread pool does not exist");

		auto token = std::make_shared<CancellationState>();
		auto prog = std::make_shared<ProgressValue>();
		using ReturnType = decltype(task(token, prog, std::forward<Args>(args)...));

		static_assert(std::is_invocable_r_v<void, Callback, ReturnType>, "Callback must have one argument of the same type as the returned type of the task");
		uint32_t id = m_id++;

		auto boundTask = [t = std::forward<Func>(task), token, prog, argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> decltype(auto)
			{
				auto callWithArgs = [&](auto&&... unpackedArgs) -> decltype(auto) {
					return t(token, prog, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
					};

				return std::apply(callWithArgs, std::move(argsTuple));
			};

		auto future = pool->second->EnqueueTask(std::move(boundTask));
		if (opt.ProgressCallback)
		{
			m_progressCallbacks[id] = opt.ProgressCallback;
			m_progress[id] = prog;
		}

		if (opt.TimeoutCallback)
		{
			m_timeoutCallbacks[id] = opt.TimeoutCallback;
			TaskTimeout tt;
			tt.StartedTime = std::chrono::steady_clock::now();
			tt.TimeoutMs = opt.TimeoutMilliseconds;
			m_timepoints[id] = tt;
		}

		auto asyncTask = std::make_unique<ConcreteAsyncTaskWrapper<ReturnType>>(id, std::move(future), std::forward<Callback>(resultCB));

		m_tasks[id] = std::move(asyncTask);
		m_cancellations[id] = token;

		return id;
	}

	static void ForceWait(uint32_t id)
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		auto it = m_tasks.find(id);
		if (it != m_tasks.end()) 
		{
			it->second->ForceWait();
			m_tasks.erase(it);
			m_cancellations.erase(id);
		}
	}

	static void Update()
	{
		//Timeouts
		for (auto it = m_timepoints.begin(); it != m_timepoints.end(); )
		{
			auto& k = it->first;
			auto& v = it->second;

			auto now = std::chrono::steady_clock::now();
			auto diffMs = std::chrono::duration<float, std::milli>(now - v.StartedTime).count();

			if (diffMs >= v.TimeoutMs)
			{
				if (auto cb = m_timeoutCallbacks.find(k);
					cb != m_timeoutCallbacks.end())
				{
					cb->second(k);
					m_timeoutCallbacks.erase(cb);
				}

				it = m_timepoints.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Progress
		for (auto it = m_progressCallbacks.begin(); it != m_progressCallbacks.end(); it++)
		{
			auto val = m_progress[it->first];
			it->second(val->Value);
		}

		std::lock_guard<std::mutex> lock(m_tasksMutex);
		for (auto it = m_tasks.begin(); it != m_tasks.end(); )
		{
			if (it->second->CheckAndExecuteCallback()) 
			{
				if (auto tout = m_timeoutCallbacks.find(it->first); tout != m_timeoutCallbacks.end())
				{
					m_timeoutCallbacks.erase(it->first);
					m_timepoints.erase(it->first);
				}

				if (auto prog = m_progressCallbacks.find(it->first); prog != m_progressCallbacks.end())
				{
					m_progressCallbacks.erase(it->first);
					m_progress.erase(it->first);
				}

				it = m_tasks.erase(it);
			}
			else 
			{
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

		m_threadPools[poolName] = std::make_unique<ThreadPool>(threadsCount, poolName);
	}

	static void Initialize(const std::string& defaultPoolName = DefaultPoolName, size_t maxThreads = std::thread::hardware_concurrency())
	{
		auto poolName = defaultPoolName;
		if (poolName.empty())
			poolName = DefaultPoolName;

		m_defaultPoolName = poolName;
		m_threadPools[m_defaultPoolName] = std::make_unique<ThreadPool>(maxThreads, defaultPoolName);
		m_initialized = true;
	}

	static uint32_t GetAvailableThreadsCount(const std::string& poolName)
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
	inline static std::unordered_map<uint32_t, TaskTimeout> m_timepoints;
	inline static std::unordered_map<uint32_t, std::function<void(uint32_t)>> m_timeoutCallbacks;
	inline static std::unordered_map<uint32_t, std::function<void(float)>> m_progressCallbacks;
	inline static std::unordered_map<uint32_t, Progress> m_progress;
	inline static std::mutex m_tasksMutex;
	inline static uint32_t m_id = 0;
	inline static std::unordered_map<std::string, std::unique_ptr<ThreadPool>> m_threadPools;
	inline static bool m_initialized = false;
	inline static std::string m_defaultPoolName;
};
