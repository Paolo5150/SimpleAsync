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
	static uint32_t CreateTask(Func&& task, Callback&& callback, Args&&... args)
	{
		uint32_t id = m_id++;

		auto token = std::make_shared<CancellationState>();

		auto boundTask = [t = std::forward<Func>(task), token, argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> decltype(auto)
			{
				auto callWithArgs = [&](auto&&... unpackedArgs) -> decltype(auto) {
					return t(token, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
					};

				return std::apply(callWithArgs, std::move(argsTuple));
			};

		auto future = m_threadPool.EnqueueTask(std::move(boundTask));

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
			m_cancellations.erase(it->second->GetId());
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

	static CancellationToken GetCancellationToken(uint32_t id)
	{
		return m_cancellations[id];
	}

	static void Cancel(uint32_t id)
	{
		auto it = m_tasks.find(id);
		if (it != m_tasks.end())
		{
			m_cancellations[id]->Canceled.store(true);
		}
	}

	static void Destroy()
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_tasks.clear();
	}

private:
	inline static std::unordered_map<uint32_t, std::unique_ptr<AsyncTaskWrapper>> m_tasks;
	inline static std::unordered_map<uint32_t, CancellationToken> m_cancellations;
	inline static std::mutex m_tasksMutex;
	inline static uint32_t m_id = 0;
	inline static ThreadPool m_threadPool;
};
