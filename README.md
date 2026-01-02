# SimpleAsync

🚀 **SimpleAsync** is a lightweight, header-only C++ task library for running background work on a thread pool, with results safely delivered back on the **main thread via callbacks**.

Ideal for game engines, simulations, and any system requiring background work to sync back to a main loop.

---

## Highlights

* ✅ Header-only
* 🧵 Multiple named thread pools
* 🧠 Callbacks run on the main thread
* ❌ Cooperative cancellation
* ⏱️ Task timeouts with automatic cancellation
* 🧱 Sequential queues via single-thread pools
* 📊 Demo with Built-in profiler support using [ChromeProfiler](https://github.com/Paolo5150/ChromeProfiler) (Chrome tracing compatible)

---

## Requirements

* C++17 or higher

---

## Quick Example

```cpp
SimpleAsync::Initialize("DefaultPool", 4);  // Initialize default pool with 4 threads
SimpleAsync::CreatePool("LowPriorityQueue", 1); // Low-priority single-thread pool

auto task = [](CancellationToken token, int x, int y) -> int {
    // This will run on a separate thread
    int result = 0;
    for (int i = 0; i < x; ++i)
        for (int j = 0; j < y; ++j)
            if (token->Canceled) return -1; // Early return if task was canceled
            else result += i + j;
    return result;
};

auto callback = [](int result) {
    // Invoked on main thread
    //'result' is whatever the task returned
    std::cout << "Task finished with result: " << result << std::endl;
};

uint32_t taskId = SimpleAsync::CreateTask(task, callback, 100, 100); // CreateTask will put task on default pool
uint32_t othertaskId = SimpleAsync::CreateTaskInPool("LowPriorityQueue", task, callback, 100, 100); // CreateTaskInPool will put the task on the specified pool

// Optionally block for a specific task to complete.
SimpleAsync::ForceWait(taskId);

// Optionally request cancellation of a task. Callback will still be invoked.
SimpleAsync::Cancel(taskId);

while (running) 
{
    SimpleAsync::Update(); // Executes completed task callbacks on main thread
}

// Shutdown
SimpleAsync::Destroy();
```

---

## Task Timeouts

SimpleAsync supports task timeout monitoring. When a timeout is reached, a user-defined callback is invoked, allowing you to decide whether to cancel the task or take other actions:

```cpp
auto timeoutTask = [](CancellationToken token, int durationMs) -> int {
    for (int i = 0; i < durationMs; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (token->Canceled) {
            return -1; // Task was canceled due to timeout
        }
    }
    return 0;
};

auto taskCallback = [](int result) {
    std::cout << "Task completed with result: " << result << std::endl;
};

auto timeoutHandler = [](uint32_t taskId) {
    std::cout << "Timeout reached! Canceling task " << taskId << std::endl;
    // You decide what to do - cancel, log, retry, etc.
    SimpleAsync::Cancel(taskId);
};

// Create a task with 500ms timeout (task attempts to run for 1000ms)
// When 500ms elapses, timeoutHandler will be called on the main thread
// The handler can then decide whether to cancel the task or take other actions
uint32_t taskId = SimpleAsync::CreateTaskTimeout(
    "DefaultPool",  // Pool name
    1000,           // Task duration parameter
    timeoutTask,    // Task function
    taskCallback,   // Completion callback
    timeoutHandler, // Timeout handler
    500             // Timeout in milliseconds
);
```

The timeout handler allows you to perform custom logic when a timeout occurs, and you can then cancel the task or take other actions as needed.

---

## Profiler Demo

* Multiple pools with normal and low-priority sequential queues
* Tasks can be canceled manually or via timeout
* All tasks are profiled for timing and thread usage using [ChromeProfiler](https://github.com/Paolo5150/ChromeProfiler)
* Outputs a JSON file compatible with Chrome tracing (`chrome://tracing`)
* Dump the outputted JSON file in chrome://tracing/ to see the tasks timeline

---

## Integration

1. Include `SimpleAsync.h`, `ThreadPool.h`, and integrate [ChromeProfiler](https://github.com/Paolo5150/ChromeProfiler) if profiling is needed
2. Initialize with `SimpleAsync::Initialize(...)`
3. Schedule tasks with `CreateTask(...)`, `CreateTaskInPool(...)`, or `CreateTaskTimeout(...)` for timeout support
4. Call `SimpleAsync::Update()` each frame
5. Cancel tasks with `Cancel(id)` if needed
6. Shutdown with `Destroy()`

---

## License

MIT License.