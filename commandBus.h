#pragma once
#include <queue>
#include <string>
#include <mutex>
#include <condition_variable>

struct CommandBus {
    std::queue<std::string> pending;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> alive{ true };
    void send(const std::string& cmd) { std::lock_guard<std::mutex> lk(mtx); pending.push(cmd); cv.notify_one(); }
    bool pop(std::string& out) {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait_for(lk, std::chrono::milliseconds(10), [this] {return !pending.empty() || !alive; });
        if (pending.empty()) return false;
        out = pending.front(); pending.pop(); return true;
    }
};