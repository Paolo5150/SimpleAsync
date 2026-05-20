# SimpleAsync

🚀 **SimpleAsync** is a lightweight, header-only C++ task system for running background work on thread pools, with results safely delivered back to the **main thread via callbacks**.

Designed for game engines, simulations, rendering systems, tools, and applications with a frame/update loop.

---

# Features

* ✅ Header-only
* 🧵 Multiple named thread pools
* 🧠 Main-thread callback execution
* ❌ Cooperative cancellation
* ⏱️ Task timeout monitoring
* 📊 Progress reporting
* 🧱 Sequential task queues using single-thread pools
* 🔥 Optional callbacks
* ⚡ Lightweight API
* 📈 Built-in profiling demo using Chrome tracing compatible profiler

---

# Requirements

* C++17 or higher

---

# Basic Usage

```cpp
#include "SimpleAsync.h"

int main()
{
    // Initialize default pool with 4 worker threads
    SimpleAsync::Initialize("DefaultPool", 4);

    // Optional additional pool
    SimpleAsync::CreatePool("LowPriorityQueue", 1);

    auto task = [](CancellationToken token, Progress prog, int x, int y) -> int
    {
        int result = 0;

        for (int i = 0; i < x; ++i)
        {
            for (int j = 0; j < y; ++j)
            {
                // Cooperative cancellation
                if (token->Canceled)
                    return -1;

                result += i + j;
            }
        }

        return result;
    };

    auto callback = [](int result)
    {
        // Runs on the main thread during Update()
        std::cout << "Task result: " << result << std::endl;
    };

    // Execute on default pool
    uint32_t taskId = SimpleAsync::CreateTask(
        task,
        callback,
        {},
        100,
        100);

    bool running = true;

    while (running)
    {
        // Executes completed callbacks on main thread
        SimpleAsync::Update();
    }

    SimpleAsync::Destroy();
}
```

---

# Optional Callbacks

Callbacks are optional.

```cpp
auto task = [](CancellationToken token, Progress prog, int ms) -> int
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 42;
};

// No callback supplied
SimpleAsync::CreateTask(task, {}, 1000);
```

This is useful for fire-and-forget tasks.

---

# Multiple Thread Pools

You can create multiple named pools for different workloads.

```cpp
SimpleAsync::Initialize("DefaultPool", 4);

// Sequential single-thread queue
SimpleAsync::CreatePool("LowPriorityQueue", 1);

// Background streaming pool
SimpleAsync::CreatePool("StreamingPool", 2);
```

Schedule tasks onto a specific pool:

```cpp
SimpleAsync::CreateTaskInPool(
    "LowPriorityQueue",
    task,
    callback,
    {},
    100,
    100);
```

Single-thread pools naturally behave like sequential queues.

---

# Cancellation

SimpleAsync uses cooperative cancellation.

```cpp
auto task = [](CancellationToken token, Progress prog) -> int
{
    while (true)
    {
        if (token->Canceled)
        {
            std::cout << "Task canceled" << std::endl;
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

uint32_t id = SimpleAsync::CreateTask(task, {}, {});

// Later...
SimpleAsync::Cancel(id);
```

Tasks must periodically check `token->Canceled`.

---

# Progress Reporting

Tasks can report progress back to the main thread.

```cpp
auto task = [](CancellationToken token, Progress prog)
{
    for (int i = 0; i <= 10; i++)
    {
        prog->Value = i / 10.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
};

AsyncOptions opt;
opt.ProgressCallback = [](float progress)
{
    std::cout << "Progress: " << progress * 100.0f << "%" << std::endl;
};

SimpleAsync::CreateTask(task, {}, opt);
```

Progress callbacks execute on the main thread during `Update()`.

---

# Task Timeouts

SimpleAsync supports timeout monitoring through `AsyncOptions`.

Timeout handlers execute on the main thread and can decide how to respond.

```cpp
auto timeoutTask = [](CancellationToken token, Progress prog, int durationMs) -> int
{
    for (int i = 0; i < durationMs; i++)
    {
        if (token->Canceled)
        {
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
};

auto resultCallback = [](int result)
{
    std::cout << "Task completed with result: " << result << std::endl;
};

auto timeoutHandler = [](uint32_t taskId)
{
    std::cout << "Timeout reached for task " << taskId << std::endl;

    // Optional cancellation
    SimpleAsync::Cancel(taskId);
};

AsyncOptions opt;
opt.TimeoutMilliseconds = 500;
opt.TimeoutCallback = timeoutHandler;

SimpleAsync::CreateTask(
    timeoutTask,
    resultCallback,
    opt,
    1000);
```

---

# Force Waiting

You can block until a task completes.

```cpp
uint32_t taskId = SimpleAsync::CreateTask(task, callback, {}, 100);

SimpleAsync::ForceWait(taskId);
```

This waits for completion and immediately executes the callback.

---

# Main Loop Integration

`SimpleAsync::Update()` should typically be called once per frame or tick.

```cpp
while (running)
{
    SimpleAsync::Update();

    Render();
    SimulationTick();
}
```

The following execute during `Update()`:

* Completion callbacks
* Timeout callbacks
* Progress callbacks

All on the main thread.

---

# Threading Model

| Component           | Thread        |
| ------------------- | ------------- |
| Task execution      | Worker thread |
| Completion callback | Main thread   |
| Timeout callback    | Main thread   |
| Progress callback   | Main thread   |
| `Update()`          | Main thread   |

---

# Example Demo Features

The included demo showcases:

* Multiple pools
* Sequential low-priority queues
* Timeout handling
* Cooperative cancellation
* Progress tracking
* Main-thread callbacks
* Profiling integration

---

# Profiling Support

The demo integrates with:

[ChromeProfiler](https://github.com/Paolo5150/ChromeProfiler?utm_source=chatgpt.com)

Features include:

* Thread timelines
* Scope profiling
* Chrome tracing support
* JSON trace output

Open the generated trace file in:

```text
chrome://tracing
```

to inspect task execution timelines.

---

# Integration

1. Include:

   * `SimpleAsync.h`
   * `ThreadPool.h`

2. Initialize:

```cpp
SimpleAsync::Initialize();
```

3. Schedule tasks:

```cpp
SimpleAsync::CreateTask(...);
SimpleAsync::CreateTaskInPool(...);
```

4. Call:

```cpp
SimpleAsync::Update();
```

every frame/tick

5. Shutdown:

```cpp
SimpleAsync::Destroy();
```

---

# Notes

* Callbacks are never executed on worker threads
* Tasks are not forcibly killed during cancellation
* Cancellation is cooperative
* `Update()` must be called regularly for callbacks/timeouts/progress updates
* Thread pools persist until `Destroy()`

---

# License

MIT License
