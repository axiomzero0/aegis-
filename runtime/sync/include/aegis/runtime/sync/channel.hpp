// aegis/runtime/sync/channel.hpp — Zero-cost message passing.
#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
namespace aegis::runtime::sync {

template <typename T>
class Channel {
public:
    void send(T value) {
        std::lock_guard<std::mutex> lk(m_);
        queue_.push(std::move(value));
        cv_.notify_one();
    }
    T recv() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return !queue_.empty(); });
        T v = std::move(queue_.front());
        queue_.pop();
        return v;
    }
private:
    std::queue<T>        queue_;
    std::mutex           m_;
    std::condition_variable cv_;
};

} // namespace aegis::runtime::sync
