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
#include <cassert>
#include "Profiler.h"

int main(int argc, char* argv[])
{
    PROFILE_BEGIN("SimpleAsync");
    {
        PROFILE_SCOPE("Main program");

        std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

        // ---------------- TEST FLAGS ----------------
        std::atomic<bool> voidExecuted = false;
        std::atomic<bool> noCBExecuted = false;
        std::atomic<bool> lowPriorityExecuted = false;
        std::atomic<bool> timeoutTriggered = false;
        std::atomic<bool> normalCallbackReceived = false;
        std::atomic<bool> cancelConfirmed = false;

        // Initialize async system
        SimpleAsync::Initialize("DefaultPool", 4);
        SimpleAsync::CreatePool("LowPriorityQueue", 1);

        // === Simple task ===
        auto simpleVoid = [&](TaskContext ctx, int mills) -> void
            {
                PROFILE_SCOPE("Void task");
                std::cout << "[Void] Started on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(mills));

                std::cout << "[Void] Finished" << std::endl;

                voidExecuted = true;
            };

        SimpleAsync::CreateTask(simpleVoid,  500);

        auto task = [](TaskContext ctx, int x) -> int { return x; };

        SimpleAsync::CreateTask(task, [](int r) {}, 500);


        // === No Callback task ===
        auto noCBTask = [&](TaskContext ctx, int durationMs) -> int
            {
                PROFILE_SCOPE("No callback task");
                std::cout << "[No callback] Started on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

                std::cout << "[No callback] Finished" << std::endl;

                noCBExecuted = true;
                return 0;
            };

        SimpleAsync::CreateTask(noCBTask, {}, 2000);

        // === Low Priority Sequential Tasks ===
        auto lowPriorityTask = [](TaskContext ctx, int durationMs) -> int
            {
                PROFILE_SCOPE("Low priority task");
                std::cout << "[Low Priority Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

                std::cout << "[Low Priority Task] Finished" << std::endl;

                return 0;
            };

        auto lowPriorityCallback = [&](int result)
            {
                PROFILE_SCOPE("Low priority callback");
                std::cout << "[Low Priority Callback] Executing callback on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                std::cout << "[Low Priority Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;

                lowPriorityExecuted = true;
            };

        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, {}, 1500);

        // === Timeout Task ===
        auto timeoutTask = [](TaskContext ctx, int durationMs) -> int
            {
                auto started = std::chrono::steady_clock::now();

                PROFILE_SCOPE("Timeout task");
                std::cout << "[Timeout Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                for (int i = 0; i < durationMs; i++)
                {
                    if (ctx.Token->Canceled)
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

        auto resultCallback = [&](int result)
            {
                PROFILE_SCOPE("Timeout callback");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cout << "[Timeout Callback] Result: " << result
                    << " on thread: " << std::this_thread::get_id() << std::endl;

                normalCallbackReceived = true;
            };

        auto timeoutHandler = [&](uint32_t id)
            {
                PROFILE_SCOPE("Timeout handler");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cout << "[Timeout Handler] Timeout reached! Canceling task " << id << std::endl;

                timeoutTriggered = true;
                SimpleAsync::Cancel(id);
            };

        AsyncOptions opt;
        opt.TimeoutMilliseconds = 500;
        opt.TimeoutCallback = timeoutHandler;

        SimpleAsync::CreateTaskInPool("DefaultPool", timeoutTask, resultCallback, opt, 1000);

        // === Normal Computation Task ===
        auto normalTask = [](TaskContext ctx, int iterationsX, int iterationsY) -> int
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

        auto normalCallback = [&](int taskResult)
            {
                PROFILE_SCOPE("Normal callback", "Result", taskResult);
                std::cout << "[Normal Callback] Received result: " << taskResult
                    << " on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                std::cout << "[Normal Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;

                normalCallbackReceived = true;
            };

        SimpleAsync::CreateTask(normalTask, normalCallback, {}, 50000, 50000);

        // === Cancelable Task ===
        auto cancelableTask = [&](TaskContext ctx, int iterationsX, int iterationsY) -> int
            {
                PROFILE_SCOPE("Cancelable task");
                std::cout << "[Cancelable Task] Started on thread: " << std::this_thread::get_id() << std::endl;

                int result = 0;
                for (int x = 0; x < iterationsX; x++)
                {
                    for (int y = 0; y < iterationsY; y++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));

                        if (ctx.Token->Canceled)
                        {
                            std::cout << "[Cancelable Task] CANCELED at iteration (" << x << ", " << y << ")" << std::endl;
                            cancelConfirmed = true;
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
                std::cout << "[Cancelable Callback] Received result: " << taskResult
                    << " on thread: " << std::this_thread::get_id() << std::endl;

                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                std::cout << "[Cancelable Callback] Finished on thread: " << std::this_thread::get_id() << std::endl;
            };

        auto cancelID = SimpleAsync::CreateTask(cancelableTask, cancelableCallback, {}, 100, 100);

        bool loopRunning = true;

        // ---- Task with progress to exit loop
        auto taskKillLoop = [&](TaskContext ctx)
            {
                for (int i = 1; i <= 10; i++)
                {
                    ctx.Prog->Value = i / 10.0f;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
                return 0;
            };

        auto killLoopCB = [&](int a)
            {
                std::cout << "Progress to exit loop: complete! Bye!\n";
                loopRunning = false;
            };

        AsyncOptions opt2;
        opt2.ProgressCallback = [](float p)
            {
                static float previous = 0;
                if (p > previous)
                {
                    previous = p;
                    std::cout << "[Progress Callback] Progress until exit loop: "
                        << p << " on thread: " << std::this_thread::get_id() << std::endl;
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

            if (frames == 150)
            {
                SimpleAsync::Cancel(cancelID);
                std::cout << "\n[Main Loop] Frame " << frames
                    << ": Canceling task" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // Flush remaining tasks
        for (int i = 0; i < 100; i++)
        {
            SimpleAsync::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // ---------------- ASSERTIONS ----------------
        std::cout << "\n=== ASSERTIONS ===\n";

        assert(loopRunning == false && "Loop did not exit correctly");
        assert(voidExecuted && "Void task did not execute");
        assert(noCBExecuted && "No-callback task did not execute");
        assert(lowPriorityExecuted && "Low priority callback did not execute");
        assert(timeoutTriggered && "Timeout did not trigger");
        assert(normalCallbackReceived && "Normal callback not received");
        assert(cancelConfirmed && "Cancellation was not triggered");


        std::cout << "All async tests passed!\n";
    }

    PROFILE_END();
    SimpleAsync::Destroy();

    std::cout << "\n=== Shutdown complete ===" << std::endl;
    return 0;
}