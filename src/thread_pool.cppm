module;

#include <stdexec/execution.hpp>

export module thread_pool;

import std;
import std.compat;

export namespace thpool {

class static_thread_pool {
public:
    explicit static_thread_pool(size_t thread_num);

    template <typename F>
    void add_task(F &&f);

    void start();
    void stop();
    bool stopped();

    ~static_thread_pool();

private:
    static_thread_pool() = delete;
    static_thread_pool(const static_thread_pool &) = delete;
    static_thread_pool &operator=(const static_thread_pool &) = delete;
    static_thread_pool(static_thread_pool &&) = delete;
    static_thread_pool &operator=(static_thread_pool &&) = delete;

private:
    size_t thread_num_;
    std::condition_variable cv_;
    std::mutex mtx_;
    std::vector<std::jthread> threads_;
    std::queue<std::function<void ()>> tasks_;
    bool started_ = false;
    bool stop_ = false;

};

template <typename F>
void static_thread_pool::add_task(F &&f) {
    {
        std::unique_lock lk(this->mtx_);
        if (!this->started_) {
            start();
            this->started_ = true;
        }
    }

    {
        std::unique_lock lk(this->mtx_);
        this->tasks_.emplace(std::forward<F>(f));
    }

    this->cv_.notify_one();
}

} // namespace thpool