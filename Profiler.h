#pragma once
#include <string>
#include <fstream>
#include <thread>
#include <map>
#include <optional>
#include <sstream>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <ctime>
#include <Windows.h> //TODO: cross platform
#define PROFILE_ON //Comment this out to disable all profiling

struct ProfileEventInfo
{
	std::string EventName;
	std::string Category;
	uint32_t ProcessID;
	uint32_t ThreadID;
	char EventType;
	long long TimePoint;
	std::map<std::string, std::string> Args;
	std::optional<long long> TimeDuration;
	std::optional<char> Scope; // Used for instant event
	std::optional<std::uintptr_t> Id; //Used for custom event
};

class ProfileEvent
{
public:
	template<typename T, typename... Args>
	void AddArgs(T firstArg, Args... args)
	{
		auto tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));

		std::stringstream ss;
		ss << firstArg;

		if (m_counter % 2 == 0)
			m_currentKey = ss.str();
		else
			m_info.Args[m_currentKey] = ss.str();
		m_counter++;
		AddArgs(args...);
	}

	template <typename T>
	void AddArgs(T t)
	{
		auto tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));

		if (m_counter % 2 == 0)
			throw std::runtime_error("Added a key with no value!");
		std::stringstream ss;
		ss << t;
		m_info.Args[m_currentKey] = ss.str();
		m_counter++;
	}

	void AddArgs() {}

protected:
	ProfileEventInfo m_info;

private:
	int m_counter = 0;
	std::string m_currentKey;
};

class CustomEvent : public ProfileEvent
{
public:
	CustomEvent(const char* name, bool async = false);
	~CustomEvent();
private:
	bool m_isAsync = false;
};

class InstantEvent : public ProfileEvent
{
public:
	friend class Profiler;
	InstantEvent(const char* name);
	~InstantEvent();
};

class Profiler
{
public:
	static Profiler& Instance()
	{
		static Profiler instance;
		return instance;
	}
	~Profiler() = default;
	Profiler() = default;
	Profiler(const Profiler&) = delete;
	Profiler& operator=(const Profiler&) = delete;

	void StartSession(const std::string& sessionName = "Profile", bool useInfoConsoleLogs = false)
	{
		m_useInternalCommandLogs = useInfoConsoleLogs;
		if (m_useInternalCommandLogs) std::cout << "PROFILER: Starting session " << sessionName << "\n";
		// If a previos session was sterted, make sure to join the thread
		if (m_thread)
		{
			if (m_thread->joinable())
				m_thread->join();

			m_thread.reset();
		}

		m_threadRunning = true;
		m_writeComma = false;
		if (m_useInternalCommandLogs) std::cout << "PROFILER: Starting writing thread\n";
		m_thread = std::make_unique<std::thread>(&Profiler::ThreadJob, this, sessionName);
	}
	void EndSession()
	{
		if (m_useInternalCommandLogs) std::cout << "PROFILER: Ending session\n";

		//Notify the thread we want to end the session
		//Wrap in scope so the lock is released
		{
			std::lock_guard l(m_outstreamMutex);
			m_threadRunning = false;
			m_waitCondition.notify_one();
		}

		if (m_useInternalCommandLogs) std::cout << "PROFILER: Writing remaining logs\n";
		//Let the thread write the remaining logs
		if (m_thread && m_thread->joinable())
		{
			m_thread->join();
			m_thread.reset();
		}
	}
	void WriteInfo(const ProfileEventInfo& info)
	{
		std::unique_lock<std::mutex> l(m_outstreamMutex);
		m_eventQueue.push(info);
		m_waitCondition.notify_one();
	}

	CustomEvent* StartCustomAsyncEvent(const std::string& eventName)
	{
		std::lock_guard l(m_asyncEventMapMutex);
		auto ce = new CustomEvent(eventName.c_str(), true);
		m_customAsyncEvents[eventName] = ce;
		return ce;
	}

	void EndCustomAsyncEvent(const std::string& eventName)
	{
		std::lock_guard l(m_asyncEventMapMutex);
		if (m_customAsyncEvents.find(eventName) != m_customAsyncEvents.end())
		{
			delete m_customAsyncEvents[eventName];
			m_customAsyncEvents.erase(eventName);
		}
	}
	CustomEvent* GetCustomAsyncEvent(const std::string& eventName)
	{
		std::lock_guard l(m_asyncEventMapMutex);
		if (m_customAsyncEvents.find(eventName) != m_customAsyncEvents.end())
			return m_customAsyncEvents[eventName];

		throw std::runtime_error("Could not find async event!");
	}

	CustomEvent* StartCustomEvent(const std::string& eventName)
	{
		auto ce = new CustomEvent(eventName.c_str());
		m_customEvents[eventName] = ce;
		return ce;

	}
	CustomEvent* GetCustomEvent(const std::string& eventName)
	{
		if (m_customEvents.find(eventName) != m_customEvents.end())
			return m_customEvents[eventName];

		return nullptr;
	}
	void EndCustomEvent(const std::string& eventName)
	{
		if (m_customEvents.find(eventName) != m_customEvents.end())
		{
			delete m_customEvents[eventName];
			m_customEvents.erase(eventName);
		}
	}

private:

	std::map<std::string, CustomEvent*> m_customAsyncEvents; //In a separate map, as we need to lock if events are started/ended in different threads
	std::map<std::string, CustomEvent*> m_customEvents;
	void ThreadJob(const std::string& sessionName)
	{
		std::ofstream m_outStream;

		std::time_t t = std::time(0);   // get time now
		std::tm* now = std::localtime(&t);

		std::stringstream ss;
		ss << sessionName << "_" << now->tm_mday << "-" << now->tm_mon << "-" << (now->tm_year + 1900) <<
			"_" << now->tm_hour << "-" << now->tm_min << "-" << now->tm_sec << ".json";

		m_outStream.open(ss.str());
		m_outStream << "[";
		m_outStream.flush();
		m_isSessionActive = true;

		while (true)
		{
			if (m_isSessionActive)
			{
				std::unique_lock<std::mutex> l(m_outstreamMutex);
				m_waitCondition.wait(l, [&] {return !m_eventQueue.empty() || !m_threadRunning; });

				if (!m_threadRunning && m_eventQueue.empty()) break;
				if (m_eventQueue.empty()) continue;
				auto localQueue = std::move(m_eventQueue);
				l.unlock(); //Unlock queue
				while (!localQueue.empty())
				{
					auto info = localQueue.front();
					localQueue.pop();

					if (!m_threadRunning && m_useInternalCommandLogs)
						std::cout << "PROFILER: Logs left " << localQueue.size() << std::endl;

					if (m_writeComma)
						m_outStream << ",\n";

					m_outStream << "{";
					m_outStream << "\"name\": \"" << info.EventName << "\",";
					m_outStream << "\"cat\": \"" << info.Category << "\",";
					m_outStream << "\"ph\": \"" << info.EventType << "\",";
					m_outStream << "\"pid\": " << info.ProcessID << ",";
					m_outStream << "\"tid\": " << info.ThreadID << ",";
					m_outStream << "\"ts\": " << info.TimePoint;

					if (info.Id.has_value())
						m_outStream << ", \"id\": " << info.Id.value();

					if (info.Scope.has_value())
						m_outStream << ", \"s\": \"" << info.Scope.value() << "\"";

					if (info.Args.size() > 0)
					{
						m_outStream << ",\"args\": {";

						bool first = true;
						for (auto it = info.Args.begin(); it != info.Args.end(); it++)
						{
							if (!first)
								m_outStream << ",";
							m_outStream << "\"" << it->first << "\": \"" << it->second << "\"";
							first = false;
						}
						m_outStream << "}";
					}

					m_outStream << "}";
					m_writeComma = true;
					m_outStream.flush();
				}
			}
			else
				m_outStream << "Error: no session was started!\n";
		}

		m_isSessionActive = false;
		m_outStream << "]";
		m_outStream.flush();
		m_outStream.close();
	}

	bool m_isSessionActive = false;
	bool m_writeComma = false;
	bool m_threadRunning = false;
	bool m_useInternalCommandLogs = false;
	std::unique_ptr<std::thread> m_thread; // Writing thread
	std::queue< ProfileEventInfo> m_eventQueue; // List of events to write
	std::mutex m_outstreamMutex; // Used to lock list of events to write
	std::mutex m_asyncEventMapMutex; // Used to lock the map of async events
	std::condition_variable m_waitCondition; // Synchronize the list of events};
};

class ScopeEvent : public ProfileEvent
{
public:
	ScopeEvent(const char* name)
	{
		auto now = std::chrono::high_resolution_clock::now();

		m_info.Category = "Scope";
		m_info.EventName = name;
		m_info.EventType = 'B';
		m_info.TimePoint = std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count();
		m_info.ProcessID = static_cast<uint32_t>(GetCurrentProcessId());
		m_info.ThreadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));

		Profiler::Instance().WriteInfo(m_info);
	}
	~ScopeEvent()
	{
		auto now = std::chrono::high_resolution_clock::now();

		m_info.Category = "Scope";
		m_info.EventType = 'E';
		m_info.TimePoint = std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count();
		m_info.ProcessID = static_cast<uint32_t>(GetCurrentProcessId());
		m_info.ThreadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
		Profiler::Instance().WriteInfo(m_info);
	}
};

// Custom event
inline CustomEvent::CustomEvent(const char* name, bool async) : m_isAsync(async)
{
	auto now = std::chrono::high_resolution_clock::now();

	m_info.Category = "Custom";
	m_info.EventName = name;
	m_info.EventType = async ? 'b' : 'B';
	m_info.TimePoint = std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count();
	m_info.ProcessID = static_cast<uint32_t>(GetCurrentProcessId());
	m_info.ThreadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	m_info.Id = reinterpret_cast<std::uintptr_t>(this);

	Profiler::Instance().WriteInfo(m_info);
}

inline CustomEvent::~CustomEvent()
{
	auto now = std::chrono::high_resolution_clock::now();

	m_info.Category = "Custom";
	m_info.EventType = m_isAsync ? 'e' : 'E';
	m_info.TimePoint = std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count();
	m_info.ProcessID = static_cast<uint32_t>(GetCurrentProcessId());
	m_info.ThreadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	Profiler::Instance().WriteInfo(m_info);
}

// Instant Event
inline InstantEvent::InstantEvent(const char* name)
{
	auto now = std::chrono::high_resolution_clock::now();

	m_info.EventName = name;
	m_info.Category = "Instant";
	m_info.EventType = 'i';

	m_info.ProcessID = static_cast<uint32_t>(GetCurrentProcessId());
	m_info.ThreadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	m_info.TimePoint = std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count();
	m_info.Scope = 't'; //Default is thread scope
}

inline InstantEvent::~InstantEvent()
{
	Profiler::Instance().WriteInfo(m_info);
}

#ifdef PROFILE_ON
#define PROFILE_BEGIN(fileName) Profiler::Instance().StartSession(fileName)
#define PROFILE_BEGIN_WLOGS(fileName) Profiler::Instance().StartSession(fileName,true)
#define PROFILE_END() Profiler::Instance().EndSession()
#define PROFILE_FUNC(...) ScopeEvent __event__(__FUNCSIG__); __event__.AddArgs(__VA_ARGS__)
#define PROFILE_SCOPE(eventName,...) ScopeEvent __event__(eventName); __event__.AddArgs(__VA_ARGS__)
#define PROFILE_CUSTOM_ASYNC_START(eventName,...) Profiler::Instance().StartCustomAsyncEvent(eventName)->AddArgs(__VA_ARGS__)
#define PROFILE_CUSTOM_ASYNC_END(eventName,...){ Profiler::Instance().GetCustomAsyncEvent(eventName)->AddArgs(__VA_ARGS__);  Profiler::Instance().EndCustomAsyncEvent(eventName);}
#define PROFILE_CUSTOM_START(eventName,...) Profiler::Instance().StartCustomEvent(eventName)->AddArgs(__VA_ARGS__)
#define PROFILE_CUSTOM_END(eventName,...) {Profiler::Instance().GetCustomEvent(eventName)->AddArgs(__VA_ARGS__); Profiler::Instance().EndCustomEvent(eventName); }
#define PROFILE_INSTANT(eventName,...) {InstantEvent __event__(eventName); __event__.AddArgs(__VA_ARGS__);}
#else
#define PROFILE_BEGIN(fileName)
#define PROFILE_END()
#define PROFILE_FUNC(...)
#define PROFILE_SCOPE(eventName,...)
#define PROFILE_CUSTOM_ASYNC_START(eventName,...)
#define PROFILE_CUSTOM_ASYNC_END(eventName,...)
#define PROFILE_CUSTOM_START(eventName,...)
#define PROFILE_CUSTOM_END(eventName,...)
#define PROFILE_INSTANT(eventName,...)
#endif