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
        auto lowPriorityTask = [](CancellationToken token, int durationMs) -> int
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
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, 1500);

        // === Timeout Task ===
        auto timeoutTask = [](CancellationToken token, int durationMs) -> int
            {
                PROFILE_SCOPE("Timeout task");
                std::cout << "[Timeout Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                for (int i = 0; i < durationMs; i++)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (token->Canceled)
                    {
                        std::cout << "[Timeout Task] CANCELED after " << i << "ms" << std::endl;
                        return -1;
                    }
                }

                std::cout << "[Timeout Task] Finished" << std::endl;
                return 0;
            };

        auto timeoutCallback = [](int result)
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
        SimpleAsync::CreateTaskTimeout("DefaultPool", 1000, timeoutTask, timeoutCallback, timeoutHandler, 500);

        // === Normal Computation Task ===
        auto normalTask = [](CancellationToken token, int iterationsX, int iterationsY) -> int
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
        auto cancelableTask = [](CancellationToken token, int iterationsX, int iterationsY) -> int
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

        // Create tasks
        SimpleAsync::CreateTask(normalTask, normalCallback, 50000, 50000);
        auto cancelTaskID = SimpleAsync::CreateTask(cancelableTask, cancelableCallback, 100, 100);

        // === Main Loop ===
        std::cout << "\n=== Starting main loop ===" << std::endl;
        int frames = 0;

        while (true)
        {
            SimpleAsync::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));

            frames++;

            // Cancel the cancelable task at frame 50
            if (frames == 50)
            {
                std::cout << "\n[Main Loop] Frame " << frames << ": Canceling task " << cancelTaskID << std::endl;
                SimpleAsync::Cancel(cancelTaskID);
            }

            // Exit after 200 frames
            if (frames >= 200)
            {
                std::cout << "\n[Main Loop] Frame " << frames << ": Exiting" << std::endl;
                break;
            }
        }
    }

    PROFILE_END();
    SimpleAsync::Destroy();
    std::cout << "\n=== Shutdown complete ===" << std::endl;
    return 0;
}