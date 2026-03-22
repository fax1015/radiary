#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "app/PreviewPresentation.h"
#include "core/Scene.h"
namespace radiary {

class SoftwareRenderer;

class CpuPreviewWorker {
public:
    struct Request {
        Scene scene;
        int width = 0;
        int height = 0;
        std::uint32_t previewIterations = 0;
        bool interactive = false;
        PreviewPresentationState presentation {};
    };

    struct CompletedFrame {
        std::vector<std::uint32_t> pixels;
        int width = 0;
        int height = 0;
        std::uint32_t previewIterations = 0;
        PreviewPresentationState presentation {};
        std::uint64_t generation = 0;
    };

    CpuPreviewWorker() = default;
    ~CpuPreviewWorker();

    CpuPreviewWorker(const CpuPreviewWorker&) = delete;
    CpuPreviewWorker& operator=(const CpuPreviewWorker&) = delete;

    void Start();
    void Stop();
    std::uint64_t Enqueue(Request request);
    std::optional<CompletedFrame> ConsumeCompletedFrame();

private:
    void ThreadMain();

    std::unique_ptr<SoftwareRenderer> renderer_;
    std::thread workerThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Request> pendingRequest_;
    std::optional<CompletedFrame> completedFrame_;
    std::uint64_t queuedGeneration_ = 0;
    bool stopRequested_ = false;
};

}  // namespace radiary
