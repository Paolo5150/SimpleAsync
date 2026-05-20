#include "SimpleAsync.h"
#include <atomic>
#include <mutex>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <functional>
#include <cmath>
#include "Profiler.h"

int main(int argc, char* argv[])
{
    PROFILE_BEGIN("SimpleAsync");
    {
        PROFILE_SCOPE("Main program");

        std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

        // Initialize async system with default thread pool
        SimpleAsync::Initialize("DefaultPool", 4);

        // Create a low priority queue with single thread for sequential task execution
        SimpleAsync::CreatePool("LowPriorityQueue", 1);

        // === Low Priority Sequential Tasks ===
        auto lowPriorityTask = [](CancellationToken token, Progress prog, int durationMs) -> int
            {
                PROFILE_SCOPE("Low priority task");
                std::cout << "[Low Priority Task] Started on thread: " << std::this_thread::get_id() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
                std::cout << "[Low Priority Task] Finished" << std::endl;
                return 0;
            };

        auto lowPriorityCallback = [](int result)
            {
                PROFILE_SCOPE("Low priority callback");
                std::cout << "[Low Priority Callback] Executing callback on thread: " << std::this_thread::get_id() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::cout << "[Low Priority Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;
            };

        // Queue three sequential low priority tasks
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);

        // === Timeout Task ===
        auto timeoutTask = [](CancellationToken token, Progress prog, int durationMs) -> int
            {
                auto started = std::chrono::steady_clock::now();

                PROFILE_SCOPE("Timeout task");
                std::cout << "[Timeout Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                for (int i = 0; i < durationMs; i++)
                {
                    if (token->Canceled)
                    {
                        auto now = std::chrono::steady_clock::now();
                        auto diff = std::chrono::duration<float, std::milli>(now - started).count();
                        std::cout << "[Timeout Task] CANCELED after " << diff << "ms" << std::endl;
                        return -1;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }

                std::cout << "[Timeout Task] Finished" << std::endl;
                return 0;
            };

        auto resultCallback = [](int result)
            {
                PROFILE_SCOPE("Timeout callback");
                std::cout << "[Timeout Callback] Result: " << result << " on thread: " << std::this_thread::get_id() << std::endl;
            };

        auto timeoutHandler = [](uint32_t id)
            {
                PROFILE_SCOPE("Timeout handler");
                std::cout << "[Timeout Handler] Timeout reached! Canceling task " << id << std::endl;
                SimpleAsync::Cancel(id);
            };

        // Create task with 500ms timeout (task attempts to run for 1000ms)
        AsyncOptions opt;
        opt.TimeoutMilliseconds = 500;
        opt.TimeoutCallback = timeoutHandler;
        SimpleAsync::CreateTaskInPool("DefaultPool", timeoutTask, resultCallback, opt, 1000);

        // === Normal Computation Task ===
        auto normalTask = [](CancellationToken token, Progress prog, int iterationsX, int iterationsY) -> int
            {
                PROFILE_SCOPE("Normal computation task");
                std::cout << "[Normal Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                int result = 0;
                for (int x = 0; x < iterationsX; x++)
                {
                    for (int y = 0; y < iterationsY; y++)
                    {
                        result += (x + y);
                    }
                }

                std::cout << "[Normal Task] Finished with result: " << result << std::endl;
                return result;
            };

        auto normalCallback = [](int taskResult)
            {
                PROFILE_SCOPE("Normal callback", "Result", taskResult);
                std::cout << "[Normal Callback] Received result: " << taskResult << " on thread: " << std::this_thread::get_id() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "[Normal Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;
            };

        // === Cancelable Task ===
        auto cancelableTask = [](CancellationToken token, Progress prog, int iterationsX, int iterationsY) -> int
            {
                PROFILE_SCOPE("Cancelable task");
                std::cout << "[Cancelable Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                int result = 0;
                for (int x = 0; x < iterationsX; x++)
                {
                    for (int y = 0; y < iterationsY; y++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));

                        if (token->Canceled)
                        {
                            std::cout << "[Cancelable Task] CANCELED at iteration (" << x << ", " << y << ")" << std::endl;
                            return -1;
                        }
                        result += x + y;
                    }
                }

                std::cout << "[Cancelable Task] Finished with result: " << result << std::endl;
                return result;
            };

        auto cancelableCallback = [](int taskResult)
            {
                PROFILE_SCOPE("Cancelable callback", "Result", taskResult);
                std::cout << "[Cancelable Callback] Received result: " << taskResult << " on thread: " << std::this_thread::get_id() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "[Cancelable Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;
            };

        SimpleAsync::CreateTask(normalTask, normalCallback, {}, 50000, 50000);
        auto cancelTaskID = SimpleAsync::CreateTask(cancelableTask, cancelableCallback, {},100, 100);

        bool loopRunning = true;

        // ---- Task with progress to exit loop
        auto taskKillLoop = [](CancellationToken token, Progress prog) {
            for (int i = 1; i <= 10; i++)
            {
                prog->Value = i / 10.0f;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            return 0;
            };

        auto killLoopCB = [&](int a) {
            std::cout << "Progress to exit loop: complete! Bye!\n";
            loopRunning = false;
            };
        AsyncOptions opt2;
        opt2.ProgressCallback = [](float p) {
            static float previous = 0;
            if (p > previous)
            {
                previous = p;
                std::cout << "[Progress Callback] Progress until exit loop: " << p << " on thread: " << std::this_thread::get_id() << std::endl;
            }
            };

        SimpleAsync::CreateTask(taskKillLoop, killLoopCB, opt2);


        // === Main Loop ===
        std::cout << "\n=== Starting main loop ===" << std::endl;
        int frames = 0;

        while (loopRunning)
        {
            SimpleAsync::Update();

            frames++;

            // Cancel the cancelable task at frame 50
            if (frames == 50)
            {
                //std::cout << "\n[Main Loop] Frame " << frames << ": Canceling task " << cancelTaskID << std::endl;
                //SimpleAsync::Cancel(cancelTaskID);
            }

    
            std::this_thread::sleep_for(std::chrono::milliseconds(16));

        }
    }

    PROFILE_END();
    SimpleAsync::Destroy();
    std::cout << "\n=== Shutdown complete ===" << std::endl;
    return 0;
}