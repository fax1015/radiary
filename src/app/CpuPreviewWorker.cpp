#include "app/CpuPreviewWorker.h"

#include <memory>

#include "renderer/SoftwareRenderer.h"

namespace radiary {

CpuPreviewWorker::~CpuPreviewWorker() {
    Stop();
}

void CpuPreviewWorker::Start() {
    Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        renderer_ = std::make_unique<SoftwareRenderer>();
        stopRequested_ = false;
        pendingRequest_.reset();
        completedFrame_.reset();
        queuedGeneration_ = 0;
    }
    workerThread_ = std::thread(&CpuPreviewWorker::ThreadMain, this);
}

void CpuPreviewWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
        pendingRequest_.reset();
    }
    cv_.notify_one();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        completedFrame_.reset();
        renderer_.reset();
    }
}

std::uint64_t CpuPreviewWorker::Enqueue(Request request) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = ++queuedGeneration_;
        pendingRequest_ = std::move(request);
    }
    cv_.notify_one();
    return generation;
}

std::optional<CpuPreviewWorker::CompletedFrame> CpuPreviewWorker::ConsumeCompletedFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!completedFrame_.has_value()) {
        return std::nullopt;
    }

    CompletedFrame frame = std::move(*completedFrame_);
    completedFrame_.reset();
    return frame;
}

void CpuPreviewWorker::ThreadMain() {
    for (;;) {
        Request request;
        std::uint64_t generation = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&]() { return stopRequested_ || pendingRequest_.has_value(); });
            if (stopRequested_) {
                return;
            }

            request = std::move(*pendingRequest_);
            pendingRequest_.reset();
            generation = queuedGeneration_;
        }

        std::vector<std::uint32_t> pixels;
        SoftwareRenderer::RenderOptions options;
        options.interactive = request.interactive;
        options.shouldAbort = [this, generation]() {
            std::lock_guard<std::mutex> lock(mutex_);
            return stopRequested_ || queuedGeneration_ > generation;
        };

        if (renderer_ == nullptr || !renderer_->RenderViewport(request.scene, request.width, request.height, pixels, options)) {
            continue;
        }

        CompletedFrame frame;
        frame.pixels = std::move(pixels);
        frame.width = request.width;
        frame.height = request.height;
        frame.previewIterations = request.previewIterations;
        frame.presentation = request.presentation;
        frame.generation = generation;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queuedGeneration_ > generation) {
                continue;
            }
            completedFrame_ = std::move(frame);
        }
    }
}

}  // namespace radiary
