#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <future>

namespace engine3d {

/**
 * @brief Prosta pula wątków (Thread Pool / Job System).
 *
 * Każdy wątek roboczy dostaje stały, unikalny indeks (0..N-1) przypisany przy starcie.
 * Indeks jest dostępny przez thread_local i nigdy się nie zmienia — to usuwa potrzebę
 * per-klatkowo liczenia wątków i eliminuje wszelkie wyścigi w thread-local indexowaniu.
 */
class JobSystem {
public:
    /**
     * @brief Inicjalizuje pulę wątków.
     * @param threadCount 0 = auto (hardware_concurrency - 1).
     */
    explicit JobSystem(size_t threadCount = 0) {
        size_t count = threadCount > 0
            ? threadCount
            : std::max<size_t>(1, std::thread::hardware_concurrency() - 1);

        workers_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this, i]() {
                // Każdy wątek roboczy dostaje stały indeks przypisany RAZ przy starcie.
                // thread_local żyje przez cały czas życia wątku - nie resetuje się.
                s_workerIndex = i;
                workerLoop();
            });
        }
    }

    ~JobSystem() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cond_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /**
     * @brief Zwraca stały indeks aktualnego wątku roboczego (0..N-1).
     * Dla głównego wątku (main) lub wątków spoza puli zwraca SIZE_MAX.
     */
    static size_t currentWorkerIndex() { return s_workerIndex; }

    /** @return Liczba wątków roboczych w puli. */
    size_t threadCount() const { return workers_.size(); }

    /**
     * @brief Zleca zadanie do wykonania w puli wątków.
     * @return std::future pozwalające poczekać na wynik / zakończenie.
     */
    template<typename F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        std::future<R> fut = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            jobs_.push([task]() { (*task)(); });
        }
        cond_.notify_one();
        return fut;
    }

    /**
     * @brief Dzieli zakres [0, count) na chunki i wykonuje je równolegle.
     * Blokuje wątek wywołujący aż wszystkie chunki zostaną ukończone.
     *
     * @param count     Liczba elementów do przetworzenia.
     * @param fn        fn(size_t begin, size_t end) – przetwarza zakres.
     * @param chunkSize Minimalny rozmiar chunka (0 = auto).
     */
    void parallelFor(size_t count, std::function<void(size_t, size_t)> fn, size_t chunkSize = 0) {
        if (count == 0) return;

        const size_t nWorkers = workers_.size();
        if (chunkSize == 0) {
            chunkSize = std::max<size_t>(1, (count + nWorkers - 1) / nWorkers);
        }

        std::vector<std::future<void>> futures;
        for (size_t start = 0; start < count; start += chunkSize) {
            size_t end = std::min(start + chunkSize, count);
            futures.push_back(submit([fn, start, end]() {
                fn(start, end);
            }));
        }

        for (auto& f : futures) {
            f.get();
        }
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            job();
        }
    }

    // Stały indeks wątku roboczego - przypisywany RAZ przy tworzeniu wątku.
    // SIZE_MAX oznacza: "nie jestem wątkiem roboczym tej puli" (main thread lub inny).
    static inline thread_local size_t s_workerIndex = SIZE_MAX;

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stopping_ = false;
};

} // namespace engine3d
