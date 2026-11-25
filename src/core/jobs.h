// Worker thread pool for terrain generation and chunk meshing.
#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>

class JobSystem {
public:
    void start(int threads) {
        running = true;
        for (int i = 0; i < threads; i++) workers.emplace_back([this] { loop(); });
    }
    void stop() {
        { std::lock_guard<std::mutex> l(m); running = false; active -= (int)queue.size(); queue.clear(); }
        cv.notify_all();
        for (auto& t : workers) t.join();
        workers.clear();
    }
    void submit(std::function<void()> f, bool urgent = false) {
        { std::lock_guard<std::mutex> l(m); if (urgent) queue.push_front(std::move(f)); else queue.push_back(std::move(f)); }
        active++;
        cv.notify_one();
    }
    int pending() const { return active.load(); }
    // Block until every submitted job has finished
    void waitIdle() { while (active.load() > 0) std::this_thread::yield(); }
    int threadCount() const { return (int)workers.size(); }
    ~JobSystem() { if (running) stop(); }
private:
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> queue;
    std::mutex m;
    std::condition_variable cv;
    bool running = false;
    std::atomic<int> active{0};
    void loop() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(m);
                cv.wait(l, [this] { return !queue.empty() || !running; });
                if (!running) return;
                f = std::move(queue.front()); queue.pop_front();
            }
            f();
            active--;
        }
    }
};
