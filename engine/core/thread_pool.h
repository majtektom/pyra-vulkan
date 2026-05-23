#pragma once

#include <algorithm>
#include <functional>
#include <future>
#include <thread>
#include <vector>

namespace engine3d {

class ThreadPool {
public:
    explicit ThreadPool(int numThreads = std::max(1u, std::thread::hardware_concurrency()))
        : running_(true) {
        workers_.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock lock(mutex_);
            running_ = false;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    int threadCount() const { return static_cast<int>(workers_.size()); }

    template <typename F>
    void parallelFor(int start, int end, F&& fn) {
        int count = end - start;
        if (count <= 0) return;
        int numThreads = threadCount();
        if (numThreads <= 1 || count < numThreads * 4) {
            fn(start, end);
            return;
        }

        int chunkSize = (count + numThreads - 1) / numThreads;
        std::vector<std::future<void>> futs;

        for (int t = 0; t < numThreads; ++t) {
            int cstart = start + t * chunkSize;
            int cend = std::min(cstart + chunkSize, end);
            if (cstart >= cend) break;

            futs.push_back(submit([=] { fn(cstart, cend); }));
        }
        for (auto& f : futs) f.get();
    }

private:
    std::future<void> submit(std::function<void()> task) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        {
            std::unique_lock lock(mutex_);
            tasks_.push_back([promise = std::move(promise), task = std::move(task)]() {
                task();
                promise->set_value();
            });
        }
        cv_.notify_one();
        return future;
    }

    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });
                if (!running_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
};

} // namespace engine3d
