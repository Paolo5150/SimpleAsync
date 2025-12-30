#define USE_ASYNC  // Comment out this line to use single-threaded version

#ifdef USE_ASYNC
#include "SimpleAsync.h"
#include <atomic>
#include <mutex>
#endif

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <functional>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct Image {
    std::vector<unsigned char> data;
    int width, height, channels;

    Image() : width(0), height(0), channels(0) {}

    bool Load(const std::string& filename) {
        unsigned char* img = stbi_load(filename.c_str(), &width, &height, &channels, 0);
        if (!img) {
            std::cout << "Failed to load: " << filename << std::endl;
            return false;
        }
        int size = width * height * channels;
        data.resize(size);
        for (int i = 0; i < size; i++) {
            data[i] = img[i];
        }
        stbi_image_free(img);
        std::cout << "Loaded: " << filename << " (" << width << "x" << height << ")" << std::endl;
        return true;
    }

    bool Save(const std::string& filename) {
        if (data.empty()) return false;
        int result = stbi_write_png(filename.c_str(), width, height, channels, data.data(), width * channels);
        if (result) {
            std::cout << "Saved: " << filename << std::endl;
        }
        return result != 0;
    }

    bool IsValid() const {
        return width > 0 && height > 0 && !data.empty();
    }
};

struct ImageTile {
    int startX, startY, endX, endY;
    int tileId;
    std::vector<unsigned char> data;

    // Default constructor
    ImageTile() : startX(0), startY(0), endX(0), endY(0), tileId(0) {}

    // Parameterized constructor
    ImageTile(int sx, int sy, int ex, int ey, int id)
        : startX(sx), startY(sy), endX(ex), endY(ey), tileId(id) {}
};

// Extract a tile from the source image with padding for blur operations
ImageTile ExtractTile(const Image& source, int startX, int startY, int endX, int endY, int tileId) {
    ImageTile tile(startX, startY, endX, endY, tileId);

    // Add padding of 3 pixels for larger blur kernel
    int paddedStartX = std::max(0, startX - 3);
    int paddedStartY = std::max(0, startY - 3);
    int paddedEndX = std::min(source.width, endX + 3);
    int paddedEndY = std::min(source.height, endY + 3);

    int tileWidth = paddedEndX - paddedStartX;
    int tileHeight = paddedEndY - paddedStartY;

    tile.data.resize(tileWidth * tileHeight * source.channels);

    for (int y = paddedStartY; y < paddedEndY; y++) {
        for (int x = paddedStartX; x < paddedEndX; x++) {
            for (int c = 0; c < source.channels; c++) {
                int srcIdx = (y * source.width + x) * source.channels + c;
                int tileIdx = ((y - paddedStartY) * tileWidth + (x - paddedStartX)) * source.channels + c;
                tile.data[tileIdx] = source.data[srcIdx];
            }
        }
    }

    return tile;
}

// Apply blur to a tile
ImageTile ApplyBlurToTile(const ImageTile& inputTile, const Image& originalImage) {
    ImageTile outputTile = inputTile;

    // Calculate actual tile dimensions with padding
    int paddedStartX = std::max(0, inputTile.startX - 1);
    int paddedStartY = std::max(0, inputTile.startY - 1);
    int paddedEndX = std::min(originalImage.width, inputTile.endX + 1);
    int paddedEndY = std::min(originalImage.height, inputTile.endY + 1);

    int tileWidth = paddedEndX - paddedStartX;
    int tileHeight = paddedEndY - paddedStartY;

    std::cout << "Processing tile " << inputTile.tileId << " (" << inputTile.startX << "," << inputTile.startY
        << " to " << inputTile.endX << "," << inputTile.endY << ")..." << std::endl;

    // Create temporary buffer for multiple blur passes
    std::vector<unsigned char> tempBuffer = inputTile.data;
    std::vector<unsigned char> workBuffer(inputTile.data.size());

    // Apply multiple blur passes for more intense effect
    const int blurPasses = 5; // Increase this for even more intense blur
    const int kernelSize = 7; // Larger kernel for more intense blur
    const int kernelRadius = kernelSize / 2;

    for (int pass = 0; pass < blurPasses; pass++) {
        // Apply box blur with larger kernel
        for (int y = kernelRadius; y < tileHeight - kernelRadius; y++) {
            for (int x = kernelRadius; x < tileWidth - kernelRadius; x++) {
                // Check if we're in the actual tile region (not padding)
                int globalX = paddedStartX + x;
                int globalY = paddedStartY + y;

                if (globalX >= inputTile.startX && globalX < inputTile.endX &&
                    globalY >= inputTile.startY && globalY < inputTile.endY) {

                    for (int c = 0; c < originalImage.channels; c++) {
                        int sum = 0;
                        int count = 0;

                        // Apply larger kernel
                        for (int ky = -kernelRadius; ky <= kernelRadius; ky++) {
                            for (int kx = -kernelRadius; kx <= kernelRadius; kx++) {
                                int sampleY = y + ky;
                                int sampleX = x + kx;

                                // Bounds check
                                if (sampleY >= 0 && sampleY < tileHeight &&
                                    sampleX >= 0 && sampleX < tileWidth) {
                                    int idx = (sampleY * tileWidth + sampleX) * originalImage.channels + c;
                                    sum += tempBuffer[idx];
                                    count++;
                                }
                            }
                        }

                        int outIdx = (y * tileWidth + x) * originalImage.channels + c;
                        workBuffer[outIdx] = (count > 0) ? (sum / count) : tempBuffer[outIdx];
                    }
                }
            }
        }

        // Swap buffers for next pass
        if (pass < blurPasses - 1) {
            tempBuffer = workBuffer;
        }
    }

    // Copy final result back to output tile
    outputTile.data = workBuffer;

    std::cout << "✓ Tile " << inputTile.tileId << " processed with " << blurPasses
        << " passes of " << kernelSize << "x" << kernelSize << " blur!" << std::endl;
    return outputTile;
}

// Global variables for task management
#ifdef USE_ASYNC
std::atomic<int> completedTasks(0);
std::vector<ImageTile> processedTiles;
std::mutex tilesLock;
#else
std::vector<ImageTile> processedTiles;
#endif
int totalTasks = 0;
Image originalImage;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image_file>" << std::endl;
        return 1;
    }

#ifdef USE_ASYNC
     SimpleAsync::Initialize(6);
#endif

    if (!originalImage.Load(argv[1])) {
#ifdef USE_ASYNC
        SimpleAsync::Destroy();
#endif
        return 1;
    }

    // Determine optimal tile size based on available threads
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4; // Fallback if detection fails

    // Calculate tile size to create more tasks than threads for better load balancing
    int targetTasks = numThreads * 3; // 3x threads for better load balancing
    int pixelsPerTask = (originalImage.width * originalImage.height) / targetTasks;
    int tileSize = std::max(32, static_cast<int>(std::sqrt(pixelsPerTask))); // Minimum 32x32 tiles

    int tilesX = (originalImage.width + tileSize - 1) / tileSize;
    int tilesY = (originalImage.height + tileSize - 1) / tileSize;
    totalTasks = tilesX * tilesY;

    processedTiles.resize(totalTasks);

#ifdef USE_ASYNC
    std::cout << "Starting parallel processing with " << totalTasks << " tiles (" << tilesX << "x" << tilesY
        << ") on " << numThreads << " threads..." << std::endl;
    std::cout << "Tile size: " << tileSize << "x" << tileSize << " pixels" << std::endl;
#else
    std::cout << "Starting single-threaded processing with " << totalTasks << " tiles (" << tilesX << "x" << tilesY << ")..." << std::endl;
    std::cout << "Tile size: " << tileSize << "x" << tileSize << " pixels" << std::endl;
#endif

    // Start timing
    auto startTime = std::chrono::high_resolution_clock::now();

    // Create tasks for each tile
    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            int startX = tx * tileSize;
            int startY = ty * tileSize;
            int endX = std::min(startX + tileSize, originalImage.width);
            int endY = std::min(startY + tileSize, originalImage.height);
            int tileId = ty * tilesX + tx;

            // Extract tile with padding
            ImageTile tile = ExtractTile(originalImage, startX, startY, endX, endY, tileId);

#ifdef USE_ASYNC
            // Create blur task
            auto blurTask = [](CancellationToken token, ImageTile input) -> ImageTile {
                    return ApplyBlurToTile(input, originalImage);
                };

            std::function<void(ImageTile)> blurCallback = [tileId](ImageTile result) {
                {
                    std::lock_guard<std::mutex> lock(tilesLock);
                    processedTiles[tileId] = result;
                }
                completedTasks++;

                // Only print progress every 10% to avoid spam
                int progress = (completedTasks.load() * 100) / totalTasks;
                static int lastProgress = -1;
                if (progress >= lastProgress + 10) {
                    std::cout << "Progress: " << progress << "% (" << completedTasks.load() << "/" << totalTasks << " tiles)" << std::endl;
                    lastProgress = progress;
                }
                };

            uint32_t taskId = SimpleAsync::CreateTask(blurTask, blurCallback, tile);

            // Only print task creation for first few and last few to avoid spam
            if (tileId < 5 || tileId >= totalTasks - 5) {
                std::cout << "Created task " << taskId << " for tile " << tileId << std::endl;
            }
            else if (tileId == 5) {
                std::cout << "... creating " << (totalTasks - 10) << " more tasks ..." << std::endl;
            }
#else
            // Single-threaded processing
            ImageTile result = ApplyBlurToTile(tile, originalImage);
            processedTiles[tileId] = result;
            std::cout << "Tile " << tileId << " completed (" << (tileId + 1) << "/" << totalTasks << ")" << std::endl;
#endif
        }
    }

#ifdef USE_ASYNC
    // Wait for all tasks to complete
    while (completedTasks < totalTasks) {
        SimpleAsync::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif

    std::cout << "All tiles processed! Recomposing image..." << std::endl;

    // End timing for processing
    auto endProcessingTime = std::chrono::high_resolution_clock::now();
    auto processingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endProcessingTime - startTime);

#ifdef USE_ASYNC
    std::cout << "⏱️  Parallel processing time: " << processingDuration.count() << " ms" << std::endl;
#else
    std::cout << "⏱️  Single-threaded processing time: " << processingDuration.count() << " ms" << std::endl;
#endif

    // Recompose the final image
    Image finalImage;
    finalImage.width = originalImage.width;
    finalImage.height = originalImage.height;
    finalImage.channels = originalImage.channels;
    finalImage.data.resize(originalImage.data.size());

    for (int i = 0; i < totalTasks; i++) {
        const ImageTile& tile = processedTiles[i];

        // Calculate padded dimensions
        int paddedStartX = std::max(0, tile.startX - 3);
        int paddedStartY = std::max(0, tile.startY - 3);
        int paddedEndX = std::min(originalImage.width, tile.endX + 3);
        int paddedEndY = std::min(originalImage.height, tile.endY + 3);
        int tileWidth = paddedEndX - paddedStartX;

        // Copy processed data back to final image
        for (int y = tile.startY; y < tile.endY; y++) {
            for (int x = tile.startX; x < tile.endX; x++) {
                for (int c = 0; c < originalImage.channels; c++) {
                    // Calculate position in tile data (accounting for padding)
                    int tileY = y - paddedStartY;
                    int tileX = x - paddedStartX;
                    int tileIdx = (tileY * tileWidth + tileX) * originalImage.channels + c;

                    // Calculate position in final image
                    int finalIdx = (y * finalImage.width + x) * finalImage.channels + c;

                    finalImage.data[finalIdx] = tile.data[tileIdx];
                }
            }
        }
    }

    // End timing for complete operation
    auto endTotalTime = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTotalTime - startTime);
    auto recompositionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTotalTime - endProcessingTime);

#ifdef USE_ASYNC
    finalImage.Save("output_blur_parallel.png");
    std::cout << "✓ Parallel processing completed! Image saved as output_blur_parallel.png" << std::endl;
    std::cout << "⏱️  Recomposition time: " << recompositionDuration.count() << " ms" << std::endl;
    std::cout << "⏱️  Total time: " << totalDuration.count() << " ms" << std::endl;
#else
    finalImage.Save("output_blur_single.png");
    std::cout << "✓ Single-threaded processing completed! Image saved as output_blur_single.png" << std::endl;
    std::cout << "⏱️  Recomposition time: " << recompositionDuration.count() << " ms" << std::endl;
    std::cout << "⏱️  Total time: " << totalDuration.count() << " ms" << std::endl;
#endif

#ifdef USE_ASYNC
    SimpleAsync::Destroy();
#endif
    return 0;
}