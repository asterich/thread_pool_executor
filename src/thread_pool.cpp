module thread_pool;

import std;

namespace thpool {

static_thread_pool::static_thread_pool(size_t thread_num) {
    this->thread_num_ = thread_num;
    this->threads_.reserve(thread_num);
}

void static_thread_pool::start() {
    auto _loop = [this] {
        while (true) {
            std::unique_lock lk(this->mtx_);

            /* Wait for tasks or stop token. */
            this->cv_.wait(lk, [this] { return this->stop_ || !this->tasks_.empty(); });
            if (this->stop_ && this->tasks_.empty()) {
                return;
            }

            /* Fetch and submit a task. */
            auto task = std::move(this->tasks_.front());
            this->tasks_.pop();
            lk.unlock();
            std::invoke(task);
        }
    };

    for (std::size_t i = 0; i < this->thread_num_; ++i) {
        this->threads_.emplace_back(_loop);
    }
}

void static_thread_pool::stop() {
    std::unique_lock lk(this->mtx_);
    stop_ = true;
    cv_.notify_all();
}

bool static_thread_pool::stopped() {
    std::unique_lock lk(this->mtx_);
    return this->stop_;
}

static_thread_pool::~static_thread_pool() {
    {
        std::unique_lock lk(this->mtx_);
        if (this->stop_) {
            return;
        }
        stop_ = true;
    }

    this->cv_.notify_all();

    for (auto &thread: this->threads_) {
        thread.join();
    }
}

} // namespace thpool