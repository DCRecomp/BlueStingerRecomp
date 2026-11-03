// N9-L (L3): pool de threads do rasterizador, sem TBB. Cada linha e independente (ver forEachPixel): a ordem nao muda o quadro.
#include "soft_parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace bs::native::render::soft {

namespace {

class Pool {
public:
    Pool() {
        const unsigned n = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
        for (unsigned i = 1; i < n; ++i) workers_.emplace_back([this] { loop(); });
    }
    ~Pool() {
        {
            const std::lock_guard<std::mutex> g(m_);
            stop_ = true;
        }
        wake_.notify_all();
        for (std::thread& t : workers_) t.join();
    }

    void run(const int* rows, size_t count, const std::function<void(int)>& body) {
        const std::lock_guard<std::mutex> serial(run_);  // um trabalho por vez: dois rasterizadores se alternam no pool
        {
            const std::lock_guard<std::mutex> g(m_);
            rows_ = rows, count_ = count, body_ = &body, next_.store(0), pending_ = workers_.size(), ++generation_;
        }
        wake_.notify_all();
        take();  // quem pediu tambem trabalha
        std::unique_lock<std::mutex> g(m_);
        done_.wait(g, [this] { return pending_ == 0; });
        body_ = nullptr;
    }

private:
    void take() {
        for (size_t i; (i = next_.fetch_add(1, std::memory_order_relaxed)) < count_;) (*body_)(rows_[i]);
    }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> g(m_);
                wake_.wait(g, [&] { return stop_ || generation_ != seen; });
                if (stop_) return;
                seen = generation_;
            }
            take();
            const std::lock_guard<std::mutex> g(m_);
            if (--pending_ == 0) done_.notify_one();
        }
    }

    std::vector<std::thread>           workers_;
    std::mutex                         run_, m_;
    std::condition_variable            wake_, done_;
    const int*                         rows_ = nullptr;
    size_t                             count_ = 0, pending_ = 0;
    const std::function<void(int)>*    body_ = nullptr;
    std::atomic<size_t>                next_{0};
    uint64_t                           generation_ = 0;
    bool                               stop_ = false;
};

} // namespace

void for_each_row(const int* rows, size_t count, const std::function<void(int)>& body) {
    static Pool pool;
    pool.run(rows, count, body);
}

} // namespace bs::native::render::soft
