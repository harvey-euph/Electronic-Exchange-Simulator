#include <gtest/gtest.h>
#include "util/Mempool.hpp"
#include <string>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using namespace Exchange;

struct TestStruct {
    int id;
    char data[32];
    TestStruct(int i, const std::string& d) : id(i) {
        std::strncpy(data, d.c_str(), sizeof(data) - 1);
        data[sizeof(data) - 1] = '\0';
    }
};

class MempoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Cleanup potential stale SHM files
        Mempool<TestStruct>::unlink("test_mempool_1");
        Mempool<TestStruct>::unlink("test_mempool_oom");
        Mempool<TestStruct>::unlink("test_mempool_ipc");
    }

    void TearDown() override {
        // Cleanup SHM files after tests
        Mempool<TestStruct>::unlink("test_mempool_1");
        Mempool<TestStruct>::unlink("test_mempool_oom");
        Mempool<TestStruct>::unlink("test_mempool_ipc");
    }
};

TEST_F(MempoolTest, AllocateAndDeallocate) {
    Mempool<TestStruct> pool("test_mempool_1", 10);
    EXPECT_EQ(pool.get_capacity(), 10);

    TestStruct* p1 = pool.allocate(1, "hello");
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->id, 1);
    EXPECT_STREQ(p1->data, "hello");

    TestStruct* p2 = pool.allocate(2, "world");
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->id, 2);
    EXPECT_STREQ(p2->data, "world");

    pool.deallocate(p1);
    
    TestStruct* p3 = pool.allocate(3, "reused");
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p3->id, 3);
    EXPECT_STREQ(p3->data, "reused");

    pool.deallocate(p2);
    pool.deallocate(p3);
}

TEST_F(MempoolTest, OutOfMemory) {
    Mempool<int> pool("test_mempool_oom", 2);
    
    int* p1 = pool.allocate(100);
    int* p2 = pool.allocate(200);
    
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(*p1, 100);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(*p2, 200);
    
    int* p3 = pool.allocate(300);
    EXPECT_EQ(p3, nullptr); // should be out of memory
    
    pool.deallocate(p1);
    int* p4 = pool.allocate(400);
    ASSERT_NE(p4, nullptr);
    EXPECT_EQ(*p4, 400);

    pool.deallocate(p2);
    pool.deallocate(p4);
}

TEST_F(MempoolTest, IPCAttachment) {
    std::string pool_name = "test_mempool_ipc";
    
    // Process 1 creates the mempool
    auto pool1 = std::make_unique<Mempool<TestStruct>>(pool_name, 5);
    TestStruct* p1 = pool1->allocate(1, "from_pool1");
    ASSERT_NE(p1, nullptr);
    
    // Process 2 attaches to the mempool
    auto pool2 = std::make_unique<Mempool<TestStruct>>(pool_name, 5); // capacity will be read from shm
    TestStruct* p2 = pool2->allocate(2, "from_pool2");
    ASSERT_NE(p2, nullptr);
    
    // Validate that p1 and p2 do not point to the same object
    EXPECT_NE(p1, p2);
    
    // Simulate IPC: Process 1 sends the index of p1 to Process 2
    uint32_t p1_index = pool1->get_index(p1);
    
    // Process 2 resolves its local pointer from the index
    TestStruct* p1_local_in_pool2 = pool2->get_pointer(p1_index);
    
    // Validate that Process 2's local pointer has the correct data
    EXPECT_EQ(p1_local_in_pool2->id, 1);
    EXPECT_STREQ(p1_local_in_pool2->data, "from_pool1");
    
    // Process 2 deallocates Process 1's allocation using its local pointer
    pool2->deallocate(p1_local_in_pool2);
    
    // Process 1 should now be able to allocate it again
    TestStruct* p3 = pool1->allocate(3, "reallocated");
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p3->id, 3);
    
    // Fix pool1 deallocating p2:
    uint32_t p2_index = pool2->get_index(p2);
    pool1->deallocate(pool1->get_pointer(p2_index));
    
    pool1->deallocate(p3);
}
