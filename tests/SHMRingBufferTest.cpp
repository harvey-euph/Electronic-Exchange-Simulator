#include <gtest/gtest.h>
#include "ring/SHMRingBuffer.hpp"
#include "JsonUtil.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <sys/mman.h>

using namespace Exchange;

class SHMRingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Cleanup potential stale SHM files
        shm_unlink("/test_observer_ring");
        shm_unlink("/test_mpsc_ring");
    }

    void TearDown() override {
        // Cleanup SHM files after tests
        shm_unlink("/test_observer_ring");
        shm_unlink("/test_mpsc_ring");
    }
};

TEST_F(SHMRingBufferTest, ObserverReadOnly) {
    std::string name = "test_observer_ring";
    size_t capacity = 1024;

    // 1. Create read-write ring buffer
    SHMRingBuffer rb_rw(name, capacity);
    EXPECT_FALSE(rb_rw.is_read_only());
    EXPECT_EQ(rb_rw.get_capacity(), capacity);
    EXPECT_EQ(rb_rw.get_reserved_depth(), 0);
    EXPECT_EQ(rb_rw.get_uncommitted_depth(), 0);
    EXPECT_DOUBLE_EQ(rb_rw.get_occupancy_ratio(), 0.0);

    // 2. Create read-only observer instance (capacity 0 because it auto-detects)
    SHMObserver rb_ro(name, 0);
    EXPECT_TRUE(rb_ro.is_read_only());
    EXPECT_EQ(rb_ro.get_capacity(), capacity);

    // 3. Enqueue on RW and check metrics on RO
    int dummy = 42;
    EXPECT_TRUE(rb_rw.enqueue(&dummy, sizeof(dummy)));
    
    // Check depths (each element takes sizeof(uint32_t) + payload size)
    size_t expected_element_size = sizeof(uint32_t) + sizeof(dummy);
    EXPECT_EQ(rb_rw.get_reserved_depth(), expected_element_size);
    EXPECT_EQ(rb_rw.get_uncommitted_depth(), 0);
    
    EXPECT_EQ(rb_ro.get_reserved_depth(), expected_element_size);
    EXPECT_EQ(rb_ro.get_uncommitted_depth(), 0);
    EXPECT_GT(rb_ro.get_occupancy_ratio(), 0.0);

    // 4. Acquire + release on RW and verify metrics clear
    auto slot = rb_rw.acquire();
    EXPECT_TRUE(slot.has_value());
    rb_rw.release(*slot);
    EXPECT_EQ(rb_ro.get_reserved_depth(), 0);
    EXPECT_EQ(rb_ro.get_uncommitted_depth(), 0);
}

// Helper for MPSC test
void producer(SHMRingBuffer& rb, int id, int count, std::atomic<int>& success_count) {
    for (int i = 0; i < count; ++i) {
        int data = id * 1000000 + i;
        while (!rb.enqueue(&data, sizeof(data))) {
            std::this_thread::yield();
        }
        success_count++;
    }
}

TEST_F(SHMRingBufferTest, MPSC) {
    std::string name = "test_mpsc_ring";
    size_t capacity = 1024 * 1024; // 1MB

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
        auto slot = rb.acquire();
        if (slot) {
            ASSERT_EQ(slot->size, sizeof(int));
            received_data.push_back(*static_cast<const int*>(slot->payload));
            rb.release(*slot);
            received_count++;
        } else {
            std::this_thread::yield();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(received_count, num_producers * count_per_producer);
    EXPECT_EQ(success_count, num_producers * count_per_producer);

    // Verify all producers sent all messages
    std::vector<int> producer_counts(num_producers, 0);
    for (int val : received_data) {
        int prod_id = val / 1000000;
        ASSERT_GE(prod_id, 0);
        ASSERT_LT(prod_id, num_producers);
    }
}

TEST(JsonUtilTest, GetJsonString) {
    std::string json_str = "{\"symbol\":\"BTCUSDT\",\"price\":\"66633.56000000\"}";
    EXPECT_EQ(Exchange::get_json_string(json_str, "price"), "66633.56000000");
    EXPECT_EQ(Exchange::get_json_string(json_str, "symbol"), "BTCUSDT");
    EXPECT_EQ(Exchange::get_json_string(json_str, "nonexistent"), "");
}
