#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <stdlib.h>
using namespace std;

std::mutex mut;
std::condition_variable cond;
bool go = false;

typedef char buffer[4096*2];
using atom = std::atomic<size_t>;
typedef void (*fnc_type)(size_t, size_t);

atom lock_instr_test;

struct {
    atom v1;
    atom v2;
} __attribute__((aligned(64))) test_same_line;


struct test_different_line {
    buffer back;
    atom value;
    buffer front;
};

test_different_line *lines;

void test_single_add(size_t id, size_t nrun) {
    for (size_t i = 0; i < nrun; i++) {
        lock_instr_test.fetch_add(1, std::memory_order_relaxed);
    }
}

void test_single_cas(size_t id, size_t nrun) {
    size_t dummy;
    for (size_t i = 0; i < nrun; i++) {
        lock_instr_test.compare_exchange_weak(dummy,
                                              dummy+1,
					      std::memory_order_relaxed,
                                              std::memory_order_relaxed);
    }
}

void test_many_cas(size_t id, size_t nrun) {
    size_t dummy;
    for (size_t i = 0; i < nrun; i++) {
        while (!lock_instr_test.compare_exchange_weak(dummy,
                                                      dummy+1,
                                                      std::memory_order_relaxed,
                                                      std::memory_order_relaxed))
        {}
    }
}

void test_mfence(size_t id, size_t nrun) {
    for (size_t i = 0; i < nrun; i++) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}

void test_mfence_stores(size_t id, size_t nrun) {
    uint64_t *stores = (uint64_t *)&lines[id];
    for (size_t i = 0; i < nrun; i++) {
        for (size_t j = 0; j < 8; j++) {
            __atomic_store_n(&stores[j], j+i, __ATOMIC_RELEASE);
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}

void test_mfence_stores_contended(size_t id, size_t nrun) {
    uint64_t *stores = (uint64_t *)&lines[0];
    for (size_t i = 0; i < nrun; i++) {
        for (size_t j = 0; j < 8; j++) {
            __atomic_store_n(&stores[j], j+i, __ATOMIC_RELEASE);
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}

void test_stores_contended(size_t id, size_t nrun) {
    uint64_t *stores = (uint64_t *)&lines[0];
    for (size_t i = 0; i < nrun; i++) {
        for (size_t j = 0; j < 8; j++) {
            __atomic_store_n(&stores[j], j+i, __ATOMIC_RELEASE);
        }
    }
}

void test_stores(size_t id, size_t nrun) {
    uint64_t *stores = (uint64_t *)&lines[0];
    for (size_t i = 0; i < nrun; i++) {
        for (size_t j = 0; j < 8; j++) {
            __atomic_store_n(&stores[j], j+i, __ATOMIC_RELEASE);
        }
    }
}

void test_same_line_f(size_t id, size_t nrun) {
    atom *a;
    if (id % 2) {
        a = &test_same_line.v1;
    }
    else {
        a = &test_same_line.v2;
    }

    for (size_t i = 0; i < nrun; i++) {
        a->fetch_add(1, std::memory_order_relaxed);
    }
}

void test_different_line_f(size_t id, size_t nrun) {
    atom *a = &lines[id].value;
    for (size_t i = 0; i < nrun; i++) {
        a->fetch_add(1, std::memory_order_relaxed);
    }
}

void time_threads(size_t ntesters, size_t nrun, fnc_type op, std::string name) {
    std::thread *ths = new std::thread[ntesters];
    for (size_t i = 0; i < ntesters; i++) {
        ths[i] = std::thread([&]() {
            {
                std::unique_lock<std::mutex> lck(mut);
                cond.wait(lck, [&]() {return go; });
            }
            op(i, nrun);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto cclock = std::chrono::steady_clock::now();
    go = true;
    cond.notify_all();
    for (size_t i = 0; i < ntesters; i++) {
        ths[i].join();
    }
    auto diff = std::chrono::steady_clock::now() - cclock;
    auto td = chrono::duration_cast<chrono::milliseconds>(diff).count();
    double tdiff = ((double)td / 1000.0);
    auto nthread = ntesters;
    auto total_elem = (nthread * nrun); //* 2 since popping them all
    auto elempt = total_elem / tdiff;
    auto elemptpt = elempt / nthread;
    auto ns_per_elem = 1e9 * tdiff / nrun;
    cout << "Took " << tdiff
         << " seconds for " << nthread << " threads and "
         << nrun << " elements per thread" << endl
         << "This amounts to " << ns_per_elem
         << " nanoseconds per operation "
         << "for " << name << endl << endl;
    delete[] ths;
}

int main() {
    size_t num_test = 1e7;
    for (size_t i = 1; i <= 10; i++) {
        lines = new test_different_line[i+1];
        time_threads(i, num_test, test_single_add, "same add");
        time_threads(i, num_test, test_single_cas, "same single cas");
        time_threads(i, num_test, test_many_cas, "same many cas");
        time_threads(i, num_test, test_mfence, "mfence");
        time_threads(i, num_test, test_mfence_stores, "mfence_store");
        time_threads(i, num_test, test_mfence_stores_contended, "mfence_store_contended");
        time_threads(i, num_test, test_stores, "stores");
        time_threads(i, num_test, test_stores_contended, "stores_contended");
        time_threads(i, num_test, test_same_line_f, "same line");
        time_threads(i, num_test, test_different_line_f, "different_lines");
	delete [] lines;
        cout << endl << endl;
    }

    return 0;
}
