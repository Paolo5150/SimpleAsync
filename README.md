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

## 📋 Requirements

- **C++17** or higher

---

## 🔧 How It Works

```cpp

// 0. Initialize, pass in the number of threads to be created for the pool.
SimpleAsync::Initialize(6);

// 1. Your async task (can return any type). First argument(or only mandatory argument) is always the cancellation token, can be used to interrupt the task.
auto task = [](CancellationToken token, int cycles) -> int {

    //This will run on a separate thread
    //Started an incredibly demanding task...
    for(int i=0; i< cycles; i++>)
    {
        //Insanely advanced maths happening....
        if(token->Canceled) return -1; //Do an early return if cancellation was requested
    }
    //More incredibly complicated calculations....
    //At the end, return the data
    return 25;
};

// 2. Callback invoked when task completes (on main thread). Argument is the returned type from the task
auto callback = [](int result) {
    //Invoked on main thread
    //'result' should be 25...or -1, if cancellation was requested
    std::cout << "Result: " << result << std::endl;
};

// 3. Schedule task. Returns the id, can be used to force wait, or cancel.
auto taskId = SimpleAsync::CreateTask(task, callback, 5);

// 4. In your main loop, call every frame:
SimpleAsync::Update();  // executes any completed task callbacks

// 5. Optionally block for a specific task to complete.
SimpleAsync::ForceWait(taskId);

// 6. Optionally request cancellation of a task. Callback will still be invoked.
SimpleAsync::Cancel(taskId);

// 7. Shutdown
SimpleAsync::Destroy();
```

---

## 🧪 Included Demo: Parallel Image Blur

This repo includes a full multithreaded image blur demo using `SimpleAsync`.

### How it works:

- Loads an image (`stb_image`)
- Splits it into tiles
- Applies blur to each tile **in parallel**
- Recombines and saves the final result (`stb_image_write`)
- Build and launch the program with an arg, the path to the image

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
2. Call `SimpleAsync::Initialize(numOfThreads)` once, during initialization.
3. Call `SimpleAsync::CreateTask(...)` to launch tasks.
4. Call `SimpleAsync::Update()` each frame or tick (main thread).
5. Call `SimpleAsync::Cancel(id)` to request cancellation of a task. Task itself will need to deal with how to interrupt the running task.
6. Call `Destroy()` on quit.

---

## 🧱 Designed to Be Extended

Future extensions could include:

- ⏳ **Timeouts**: Automatically cancel tasks that exceed a time limit
- 🔄 **Chained tasks**: `then()` style continuations
- 📊 **Task prioritization**: High/medium/low priority queues
- 📈 **Progress callbacks**: Report completion percentage during execution
- 🔁 **Task groups / batch operations**: Wait for multiple tasks to complete
- 🧵 **Flexible callback threading**: Option to invoke callbacks on any thread (worker thread, dedicated callback thread, or custom thread) instead of requiring main thread sync

Pull requests are welcome!

---

## 📜 License

MIT License.

Includes:

- [stb_image.h](https://github.com/nothings/stb)
- [stb_image_write.h](https://github.com/nothings/stb)

These are public domain or MIT licensed.

---
