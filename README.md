# SimpleAsync

🚀 **SimpleAsync** is a lightweight, header-only C++ task library for running background work on a thread pool, with results safely delivered back on the **main thread via callbacks**.

Ideal for:
- Game engines
- UI frameworks
- Any system where background work needs to be **synced back** to the main loop.

---

## ✨ Highlights

- ✅ **Header-only**: Just include and use. No build steps, no setup.
- 🧵 **Runs tasks on a thread pool**.
- 🧠 **Callback runs on the main thread** — perfect for updating game state, UI, etc.
- 🧩 **Easy to extend** with timeouts, priorities, etc.

---

## 🔧 How It Works

```cpp
// 1. Your async task (can return any type). First argument(or only mandatory argument) is always the cancellation token, can be used to interrupt the task if cancellation is requested.
auto task = [](CancellationToken token, int x) {
    return x * x;
};

// 2. Callback invoked when task completes (on main thread). Argument is the returned type from the task
auto callback = [](int result) {
    std::cout << "Result: " << result << std::endl;
};

// 3. Schedule task
SimpleAsync::CreateTask(task, callback, 5);

// 4. In your main loop:
SimpleAsync::Update();  // executes any completed task callbacks

// 5. Optionally block for a specific task and invoke its callback immediately
SimpleAsync::ForceWait(taskId);
```

---

## 🧪 Included Demo: Parallel Image Blur

This repo includes a full multithreaded image blur demo using `SimpleAsync`.

### How it works:
- Loads an image (`stb_image`)
- Splits it into tiles
- Applies blur to each tile **in parallel**
- Recombines and saves the final result (`stb_image_write`)

### Toggle async vs single-threaded:

```cpp
#define USE_ASYNC  // Comment this out to run in single-threaded mode
```

### Example output:

```bash
$ ./blur_demo input.png
✓ Parallel processing completed! Image saved as output_blur_parallel.png
```

---

## 🛠️ Integration

1. Drop `SimpleAsync.h` and `ThreadPool.h` into your project.
2. Call `SimpleAsync::CreateTask(...)` to launch tasks.
3. Call `SimpleAsync::Update()` each frame or tick (main thread).
4. Call `SimpleAsync::Cancel(id)` to request cancellation of a task. Task itself will need to deal with how to interrupt the running task.
5. Call `Destroy()` on quit.

---

## 🧱 Designed to Be Extended

Future extensions could include:

- ⏳ Timeouts
- 🔄 Chained tasks (`then()`)
- 📊 Prioritization

Pull requests are welcome!

---

## 📜 License

MIT License.

Includes:
- [stb_image.h](https://github.com/nothings/stb)
- [stb_image_write.h](https://github.com/nothings/stb)

These are public domain or MIT licensed.

---