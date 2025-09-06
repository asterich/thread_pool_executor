#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>
#include <omp.h>

import std;
import thread_pool;

namespace stdchro = ::std::chrono;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

struct scoped_timer {
    
    using stclk = stdchro::steady_clock;

    scoped_timer(std::string_view name) : name_(name) {
        start_ = stclk::now();
    }

    ~scoped_timer() {
        auto end = stclk::now();
        std::println(
            "{}: {}ms",
            this->name_,
            stdchro::duration_cast<stdchro::milliseconds>(end - this->start_).count()
        );
    }

    std::string_view name_;
    stclk::time_point start_; 
};

/* basic */
void test_thread_pool_basic() {
    thpool::static_thread_pool pool(16);
    for (int i = 0; i < 100; ++i) {
        pool.add_task([i] {
            std::println("hello from {}th task", i);
        });
    }
}

/* gemv */
void test_thread_pool_gemv() {
    using namespace std::ranges;
    using namespace std::views;

    constexpr auto MATRIX_SIZE = 32768z;
    constexpr auto THREAD_NUM = 8z;
    std::vector<float> matrix(MATRIX_SIZE * MATRIX_SIZE);
    std::vector<float> vector(MATRIX_SIZE);
    std::vector<float> result(MATRIX_SIZE);
    
    for (auto i: views::iota(0z, MATRIX_SIZE * MATRIX_SIZE)) {
        matrix[i] = i;
    }

    for (auto i: views::iota(0z, MATRIX_SIZE)) {
        vector[i] = i;
    }

    /* single-threaded */
    {
        scoped_timer timer("single-threaded"sv);
        for (auto i: views::iota(0z, MATRIX_SIZE)) {
            for (auto j: views::iota(0z, MATRIX_SIZE)) {
                result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
            }
        }
    }

    /* thread pool, tiled */
    {
        scoped_timer timer("thread pool, tiled"sv);
        thpool::static_thread_pool pool(THREAD_NUM);
        constexpr auto TILE_SIZE = MATRIX_SIZE / THREAD_NUM;
        for (auto tile_i: views::iota(0z, MATRIX_SIZE)
                        | views::filter([TILE_SIZE](int64_t i) { return i % TILE_SIZE == 0; })) { /* libc++ doesn't have stride */
            pool.add_task([tile_i, &matrix, &vector, &result, TILE_SIZE, MATRIX_SIZE] {
                for (auto i: views::iota(tile_i, std::min(tile_i + TILE_SIZE, MATRIX_SIZE))) {
                    for (auto j: views::iota(0z, MATRIX_SIZE)) {
                        result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
                    }
                }
            });
        }
    }

    /* openmp */
    {
        scoped_timer timer("openmp"sv);

        #pragma omp parallel for num_threads(THREAD_NUM)
        for (auto i = 0z; i < MATRIX_SIZE; ++i) {
            for (auto j = 0z; j < MATRIX_SIZE; ++j) {
                result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
            }
        }
    }

    /* sender-based approach

       the async_scope here is not the one P3296 specifies,
       but is more like counting_scope in P3149 */
    {
        /// FIXME: Why senders are much slower?
        scoped_timer timer("thread pool with sender"sv);
        exec::async_scope scope;
        thpool::static_thread_pool_scheduler sch(THREAD_NUM);

        std::span matrix_span{matrix};
        std::span result_span{result};

        std::vector<std::span<float>> matrix_chunk_spans;
        std::vector<std::span<float>> result_chunk_spans;

        for (auto tid: views::iota(0z, THREAD_NUM)) {
            constexpr auto matrix_chunk_size = (MATRIX_SIZE / THREAD_NUM) * MATRIX_SIZE;
            constexpr auto result_chunk_size = (MATRIX_SIZE / THREAD_NUM);
            matrix_chunk_spans.emplace_back(
                matrix_span.subspan(
                    tid * matrix_chunk_size,
                    std::max((tid + 1) * matrix_chunk_size, MATRIX_SIZE * MATRIX_SIZE)
                )
            );
            result_chunk_spans.emplace_back(
                result_span.subspan(
                    tid * result_chunk_size,
                    std::max((tid + 1) * result_chunk_size, MATRIX_SIZE)
                )
            );
        }

        auto packed_chunks_view =
            std::views::zip(
                matrix_chunk_spans,
                std::views::repeat(vector, THREAD_NUM),
                result_chunk_spans
            );
        
        auto snd_views = packed_chunks_view
                       | std::views::transform([MATRIX_SIZE, THREAD_NUM, &sch] (auto packed) mutable {
                               auto [matrix_chunk, vector, result_chunk] = packed;
                               auto snd = stdexec::just(matrix_chunk, vector, result_chunk)
                                        | stdexec::continues_on(sch.get_scheduler())
                                        | stdexec::then([=] (auto matrix, auto vector, auto result) {
                                                for (auto i: views::iota(0z, MATRIX_SIZE / THREAD_NUM)) {
                                                    for (auto j: views::iota(0z, MATRIX_SIZE)) {
                                                        result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
                                                    }
                                                }
                                            });
                               return snd;
                           }
                       );
        
        for (auto snd: snd_views) {
            scope.spawn(snd);
        }

        stdexec::sync_wait(scope.on_empty());
    }
}

void test_thread_pool_scheduler() {
    exec::static_thread_pool nvpool(3);
    thpool::static_thread_pool_scheduler sch(4);
    stdexec::sender auto snd = stdexec::just()
                             | stdexec::continues_on(nvpool.get_scheduler())
                             | stdexec::then([]() { std::println("start on nv static pool"); })
                             | stdexec::continues_on(sch.get_scheduler())
                             | stdexec::then([]() { std::println("continue on thpool::static_thread_pool"); });
    auto hello_world = [] {
        std::this_thread::sleep_for(2000ms);
        std::println("hello world from {}th thread", std::this_thread::get_id());
    };

    stdexec::sync_wait(
        stdexec::when_all(
            snd | stdexec::then(hello_world),
            snd | stdexec::then(hello_world),
            snd | stdexec::then(hello_world),
            snd | stdexec::then(hello_world)
        )
        | stdexec::continues_on(nvpool.get_scheduler())
        | stdexec::then([] { std::println("finished"); })
    );
}

int main() {
    test_thread_pool_basic();
    test_thread_pool_scheduler();
    test_thread_pool_gemv();
    return 0;
}