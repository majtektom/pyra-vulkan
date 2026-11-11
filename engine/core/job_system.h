#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
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
     * @brief Indeks bufora "na wątek" dla kodu wykonywanego w parallelFor: 0..N-1 dla wątków roboczych,
     * N dla wątku, który wywołał parallelFor (on też liczy kawałki). Bufory: threadCount() + 1.
     */
    size_t currentSlot() const {
        const size_t w = s_workerIndex;
        return w < workers_.size() ? w : workers_.size();
    }

    /**
     * @brief Dzieli zakres [0, count) na kawałki i wykonuje je równolegle; wraca, gdy wszystkie są gotowe.
     *
     * Bez alokacji na kawałek: kontekst (licznik kawałków) leży na stosie wywołującego, do kolejki trafia najwyżej
     * N małych zadań-pomocników (każde trzyma tylko wskaźnik - mieści się w std::function bez alokacji), które
     * pobierają kolejne kawałki licznikiem atomowym. Wywołujący też liczy kawałki, a czekając na pomocników
     * wykonuje inne zadania z kolejki - zagnieżdżone parallelFor nie zakleszczy puli.
     *
     * @param count     Liczba elementów do przetworzenia.
     * @param fn        fn(size_t begin, size_t end) – przetwarza zakres.
     * @param chunkSize Rozmiar kawałka (0 = auto: ok. jeden kawałek na wątek).
     */
    template <class F>
    void parallelFor(size_t count, F&& fn, size_t chunkSize = 0) {
        if (count == 0) return;
        const size_t nWorkers = workers_.size();
        if (chunkSize == 0) chunkSize = std::max<size_t>(1, (count + nWorkers) / (nWorkers + 1));
        const size_t chunks = (count + chunkSize - 1) / chunkSize;
        if (chunks <= 1 || nWorkers == 0) {
            fn(size_t{0}, count);
            return;
        }
        using Fn = std::remove_reference_t<F>;
        struct Context {
            Fn* fn;
            size_t count, chunkSize, chunks;
            std::atomic<size_t> next{0};
            std::atomic<size_t> running{0};
            void runChunks() {
                for (size_t i = next.fetch_add(1, std::memory_order_relaxed); i < chunks;
                     i = next.fetch_add(1, std::memory_order_relaxed)) {
                    const size_t begin = i * chunkSize;
                    (*fn)(begin, std::min(begin + chunkSize, count));
                }
            }
        };
        Context ctx;
        ctx.fn = &fn;
        ctx.count = count;
        ctx.chunkSize = chunkSize;
        ctx.chunks = chunks;
        const size_t helpers = std::min(nWorkers, chunks - 1);
        ctx.running.store(helpers, std::memory_order_relaxed);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (size_t h = 0; h < helpers; ++h) {
                jobs_.push([c = &ctx]() {
                    c->runChunks();
                    c->running.fetch_sub(1, std::memory_order_release);   // po tym zadanie nie dotyka już kontekstu
                });
            }
        }
        cond_.notify_all();
        ctx.runChunks();
        // Kontekst jest na naszym stosie - czekamy, aż każdy pomocnik skończy (także taki, który nie zdążył
        // zacząć: wtedy wykonujemy zadania z kolejki sami).
        while (ctx.running.load(std::memory_order_acquire) != 0) {
            if (!runOnePending()) std::this_thread::yield();
        }
    }

private:
    // Wykonuje jedno zadanie z kolejki na bieżącym wątku (false = kolejka pusta).
    bool runOnePending() {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (jobs_.empty()) return false;
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        job();
        return true;
    }

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
