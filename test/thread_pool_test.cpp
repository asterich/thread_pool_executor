#include <stdexec/execution.hpp>
#include <omp.h>

import std;
import thread_pool;

namespace stdchro = ::std::chrono;
using namespace ::std::string_view_literals;

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
    constexpr auto THREAD_NUM = 4z;
    std::vector<float> matrix(MATRIX_SIZE * MATRIX_SIZE);
    std::vector<float> vector(MATRIX_SIZE);
    std::vector<float> result(MATRIX_SIZE);
    
    for (auto i: iota_view(0z, MATRIX_SIZE * MATRIX_SIZE)) {
        matrix[i] = i;
    }

    for (auto i: iota_view(0z, MATRIX_SIZE)) {
        vector[i] = i;
    }

    /* single-threaded */
    {
        scoped_timer timer("single-threaded"sv);
        for (auto i: iota_view(0z, MATRIX_SIZE)) {
            for (auto j: iota_view(0z, MATRIX_SIZE)) {
                result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
            }
        }
    }

    /* thread pool, tiled */
    {
        scoped_timer timer("thread pool, tiled"sv);
        thpool::static_thread_pool pool(THREAD_NUM);
        constexpr auto TILE_SIZE = MATRIX_SIZE / THREAD_NUM;
        for (auto tile_i: iota_view(0z, MATRIX_SIZE)
                          | stride(TILE_SIZE)) {
            pool.add_task([tile_i, &matrix, &vector, &result, TILE_SIZE, MATRIX_SIZE] {
                for (auto i: iota_view(tile_i, std::min(tile_i + TILE_SIZE, MATRIX_SIZE))) {
                    for (auto j: iota_view(0z, MATRIX_SIZE)) {
                        result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
                    }
                }
            });
        }
    }

    /* openmp */
    {
        scoped_timer timer("openmp"sv);

        #pragma omp parallel for num_threads(4)
        for (auto i = 0z; i < MATRIX_SIZE; ++i) {
            for (auto j = 0z; j < MATRIX_SIZE; ++j) {
                result[i] += matrix[i * MATRIX_SIZE + j] * vector[j];
            }
        }
    }

}

int main() {
    test_thread_pool_basic();
    test_thread_pool_gemv();
    return 0;
}