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

template <typename T>
concept executor_concept = requires(T t) {
    { t.start() } -> std::same_as<void>;
    { t.add_task(std::declval<std::function<void()>>()) } -> std::same_as<void>;
};

template <typename T>
concept executor_has_stop = requires(T t) {
    { t.stop() } -> std::same_as<void>;
    { t.stopped() } -> std::convertible_to<bool>;
};

template <executor_concept _Executor, typename _Derived>
struct scheduler_adaptor {
    struct _None {};

    using _Sched = scheduler_adaptor<_Executor, _Derived>;

    using _compl_sigs = std::conditional_t<
        executor_has_stop<_Executor>,
        stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>,
        stdexec::completion_signatures<stdexec::set_value_t()>
    >;

    using completion_signatures = _compl_sigs;

    template <typename _Sched__, stdexec::receiver _Recv>
    requires std::is_same_v<std::remove_cv_t<_Sched__>, _Sched>
    struct _operation {
        _Sched__ &sch_;
        _Recv recv_;

        void __execute_impl() {
            if constexpr (executor_has_stop<_Executor>) {
                if (this->sch_.stopped()) {
                    std::println("thpool set stopped");
                    stdexec::set_stopped(this->recv_);
                    return;
                }
            }

            stdexec::set_value(this->recv_);
        }

        void start() noexcept {
            this->sch_.add_task([this]() { __execute_impl(); });
        }
    };

    template <typename _Sched__>
    requires std::is_same_v<std::remove_cv_t<_Sched__>, _Sched>
    struct _env {
        _Sched__ &sch_;

        template <typename _Tag>
        auto query(this auto &&_self, stdexec::get_completion_scheduler_t<_Tag>) noexcept {
            return _self.sch_;
        }
    };

    template <typename _Sched__>
    requires std::is_same_v<std::remove_cv_t<_Sched__>, _Sched>
    struct _sender {
        using is_sender = void;
        using result_type = _None;
        using completion_signatures = _Sched::completion_signatures;

        _Sched__ &sch_;
        
        template <typename _Recv>
        constexpr auto connect(this auto &&_self, _Recv &&_recv) noexcept {
            return _operation<_Sched__, _Recv>{_self.sch_, std::forward<_Recv>(_recv)};
        }

        constexpr auto get_env(this auto &&_self) noexcept {
            return _env<_Sched__>{_self.sch_};
        }
    };

    template <typename _Sched__>
    stdexec::sender auto schedule(this _Sched__ &&_self) noexcept {
        return _sender{_self};
    }

    template <std::invocable F>
    void add_task(F &&f) {
        this->_alloc_thread_pool_if_not();
        this->_executor->add_task(std::forward<F>(f));
    }

    void stop() {
        if constexpr (executor_has_stop<_Executor>) {
            this->_executor->stop();
        }
    }

    bool stopped() {
        if constexpr (executor_has_stop<_Executor>) {
            return this->_executor->stopped();
        }
        return false;
    }

    friend bool operator==(const _Sched &lhs, const _Sched &rhs) {
        return lhs._executor == rhs._executor;
    }

    _Sched get_scheduler() {
        this->_alloc_thread_pool_if_not();
        return _Sched{this->_executor};
    }

protected:
    scheduler_adaptor(std::shared_ptr<_Executor> _executor) noexcept {
        this->_executor = _executor;
    }

protected:
    void _alloc_thread_pool_if_not() {
        static_cast<_Derived *>(this)->_alloc_thread_pool_if_not_impl();
    }

protected:
    std::shared_ptr<_Executor> _executor;

};

struct static_thread_pool_scheduler
    : scheduler_adaptor<static_thread_pool, static_thread_pool_scheduler> {
    using _Base = scheduler_adaptor<static_thread_pool, static_thread_pool_scheduler>;
    using completion_signatures = _Base::completion_signatures;

public:
    explicit static_thread_pool_scheduler(int _thread_num)
        : thread_num_(_thread_num), _Base(nullptr) {}

public:
        void _alloc_thread_pool_if_not_impl() {
            _executor = std::make_shared<static_thread_pool>(this->thread_num_);
        }

private:
    int thread_num_;

};

} // namespace thpool