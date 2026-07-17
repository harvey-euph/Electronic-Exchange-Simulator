#include <gtest/gtest.h>
#include "ipc/mmap_log.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <cstring>

class MmapLogTest : public ::testing::Test {
protected:
    std::string test_dir = "./tmp_gtest";

    void SetUp() override {
        struct stat st;
        if (stat(test_dir.c_str(), &st) == -1) {
            mkdir(test_dir.c_str(), 0700);
        }
    }

    void TearDown() override {
        // Optional: clean up test files here
    }
};

TEST_F(MmapLogTest, AppendAndReadBasic) {
    mmaplog::MmapWriter writer(test_dir, 1024);
    
    std::string msg1 = "Hello Mmap Log 1";
    std::string msg2 = "Hello Mmap Log 2";

    writer.append(msg1.c_str(), msg1.size());
    writer.append(msg2.c_str(), msg2.size());

    mmaplog::MmapReader reader(test_dir);
    const void* data = nullptr;
    uint32_t len = 0;

    ASSERT_TRUE(reader.read_next(data, len));
    EXPECT_EQ(len, msg1.size());
    EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg1);

    ASSERT_TRUE(reader.read_next(data, len));
    EXPECT_EQ(len, msg2.size());
    EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg2);
}

TEST_F(MmapLogTest, RolloverAndSeek) {
    mmaplog::MmapWriter writer(test_dir, 1024);
    
    std::string msg1 = "Msg1";
    std::string msg2 = "Msg2";
    std::string msg3 = "Longer message to force rollover to the next file faster.";

    uint64_t offset1 = writer.append(msg1.c_str(), msg1.size());
    uint64_t offset2 = writer.append(msg2.c_str(), msg2.size());
    
    for (int i = 0; i < 20; ++i) {
        writer.append(msg3.c_str(), msg3.size());
    }

    mmaplog::MmapReader reader(test_dir);
    const void* data = nullptr;
    uint32_t len = 0;

    // Read first two
    ASSERT_TRUE(reader.read_next(data, len));
    ASSERT_TRUE(reader.read_next(data, len));

    // Read rollovers
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(reader.read_next(data, len));
        EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg3);
    }
    
    // Read past end
    EXPECT_FALSE(reader.read_next(data, len));

    // Seek back to msg1
    EXPECT_TRUE(reader.seek(offset1));
    ASSERT_TRUE(reader.read_next(data, len));
    EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg1);

    // Seek back to msg2
    EXPECT_TRUE(reader.seek(offset2));
    ASSERT_TRUE(reader.read_next(data, len));
    EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg2);
}

TEST_F(MmapLogTest, ReserveAndCommitZeroCopy) {
    mmaplog::MmapWriter writer(test_dir, 1024);
    
    std::string msg = "Zero Copy Payload";
    uint64_t offset;
    
    // 1. Reserve space
    void* ptr = writer.reserve(msg.size(), offset);
    ASSERT_NE(ptr, nullptr);
    
    // 2. Write payload directly into memory map
    std::memcpy(ptr, msg.c_str(), msg.size());
    
    // 3. Commit it
    writer.commit(ptr);
    
    // Verify
    mmaplog::MmapReader reader(test_dir);
    const void* data = nullptr;
    uint32_t len = 0;
    
    ASSERT_TRUE(reader.read_next(data, len));
    EXPECT_EQ(len, msg.size());
    EXPECT_EQ(std::string(static_cast<const char*>(data), len), msg);
}
