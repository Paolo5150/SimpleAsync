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
        //Example with a low priority queue, which will execute tasks in sequence as they are added in
        SimpleAsync::CreatePool("LowPriorityQueue", 1);

        auto lowPriorityTask = [](CancellationToken token, int durationMs) -> int
            {
                PROFILE_SCOPE("Low task");
                std::cout << "[Task] Started on thread: " << std::this_thread::get_id() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
                std::cout << "[Task] Finished work" << std::endl;
                return 0;
            };

        auto lowPriorityCallback = [](int result)
            {
                PROFILE_SCOPE("Low task callback");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            };

        // Add low priority task to the low priority queue
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, 1500);
        SimpleAsync::CreateTaskInPool("LowPriorityQueue", lowPriorityTask, lowPriorityCallback, 1500);

        auto normalTask = [](CancellationToken token, int iterationsX, int iterationsY) -> int{

            PROFILE_SCOPE("Normal task");
            int result = 0;
            for (int x = 0; x < iterationsX; x++)
            {
                for (int y = 0; y < iterationsY; y++)
                {
                    result += (x + y);
                }
            }

            return result;
            };

        auto canceledTask = [](CancellationToken token, int iterationsX, int iterationsY) -> int {

            PROFILE_SCOPE("Canceled task");
            int result = 0;
            for (int x = 0; x < iterationsX; x++)
            {
                for (int y = 0; y < iterationsY; y++)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    if (token->Canceled) return -1;
                    result += x + y;
                }
            }

            return result;
            };

        auto normalCallback = [](int taskResult) {
            PROFILE_SCOPE("Normal callback", "Result", taskResult);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            };


        SimpleAsync::CreateTask(normalTask, normalCallback, 50000,50000);
        auto cancelTaskID = SimpleAsync::CreateTask(canceledTask, normalCallback, 100,100);

        // Simulated main loop
        while (true)
        {
            SimpleAsync::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));

            // Break once task is done (Update removes it automatically)
            // In a real app, you'd have better lifecycle tracking
            static int frames = 0;
            if (++frames > 200)
                break;

            if (frames == 50)
                SimpleAsync::Cancel(cancelTaskID);
        }



    }
  
    PROFILE_END();
    SimpleAsync::Destroy();
    std::cout << "Shutdown complete." << std::endl;
    return 0;
   
}