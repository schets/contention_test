#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
using namespace std;

std::mutex mut;
std::condition_variable cond;
bool go = false;

typedef char buffer[128];
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
    std::thread *ths = new std::thread[ntesters*2];
    for (size_t i = 0; i < ntesters*2; i++) {
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
    for (size_t i = 0; i < ntesters*2; i++) {
        ths[i].join();
    }
    auto diff = std::chrono::steady_clock::now() - cclock;
    auto td = chrono::duration_cast<chrono::milliseconds>(diff).count() - 5;
    double tdiff = ((double)td / 1000.0);
    auto nthread = ntesters*2;
    auto total_elem = (nthread * nrun); //* 2 since popping them all
    auto elempt = total_elem / tdiff;
    auto elemptpt = elempt / nthread;
    cout << "Took " << tdiff
         << " seconds for " << nthread << " threads and "
         << nrun << " elements per thread" << endl
         << "This amounts to " << elemptpt
         << " operations per thread per second "
         << "for " << name << endl << endl;
    delete[] ths;
}

int main() {
    size_t num_test = 3e7;
    for (size_t i = 1; i <= 2; i++) {
        time_threads(i, num_test, test_single_add, "same add");
        time_threads(i, num_test, test_single_cas, "same cas");
        time_threads(i, num_test, test_mfence, "mfence");
        time_threads(i, num_test, test_same_line_f, "same line");
        lines = new test_different_line[num_test*2];
        time_threads(i, num_test, test_different_line_f, "different_lines");
        delete[] lines;
        cout << endl << endl;
    }

    return 0;
}
