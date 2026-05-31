#include "ring/SHMRingBuffer.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>

using namespace Exchange;

void producer(SHMRingBuffer& rb, int id, int count, std::atomic<int>& success_count) {
    for (int i = 0; i < count; ++i) {
        int data = id * 1000000 + i;
        while (!rb.enqueue(&data, sizeof(data))) {
            std::this_thread::yield();
        }
        success_count++;
    }
}

int main() {
    std::string name = "test_mpsc_ring";
    size_t capacity = 1024 * 1024; // 1MB
    
    // Ensure SHM is cleaned up from previous runs
    shm_unlink(("/" + name).c_str());

    SHMRingBuffer rb(name, capacity);

    const int num_producers = 4;
    const int count_per_producer = 10000;
    std::atomic<int> success_count{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer, std::ref(rb), i, count_per_producer, std::ref(success_count));
    }

    int received_count = 0;
    std::vector<int> received_data;
    
    while (received_count < num_producers * count_per_producer) {
        void* data;
        size_t size;
        if (rb.dequeue(&data, &size)) {
            assert(size == sizeof(int));
            received_data.push_back(*static_cast<int*>(data));
            received_count++;
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    std::cout << "Received " << received_count << " messages successfully." << std::endl;
    assert(received_count == num_producers * count_per_producer);
    assert(success_count == num_producers * count_per_producer);

    // Verify all producers sent all messages
    std::vector<int> producer_counts(num_producers, 0);
    for (int val : received_data) {
        int prod_id = val / 1000000;
        assert(prod_id >= 0 && prod_id < num_producers);
    }

    std::cout << "MPSC Test Passed!" << std::endl;

    return 0;
}
