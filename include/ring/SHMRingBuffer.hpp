// SHMRing.hpp
#pragma once
#include <cstdint>
#include <atomic>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace Exchange {

// 定義 Magic Number 用於識別與驗證
constexpr uint32_t SHM_RING_MAGIC = 0x52494E47; // "RING" 的 ASCII

struct SHMRing {
    std::atomic<uint32_t> magic{0};    // 驗證是否為正確的 shm 格式
    std::atomic<uint32_t> ready{0};    // 0: 初始化中, 1: 初始化完成可安全使用
    
    // 為了防止 False Sharing，將 Control 變數與 Data 變數對齊到 Cache Line (64 bytes)
    alignas(64) std::atomic<uint64_t> head{0};
    alignas(64) std::atomic<uint64_t> tail{0};
    
    uint64_t capacity;
    uint64_t mask;
    // 實際資料從這裡開始
};

class SHMRingBuffer {
public:
    SHMRingBuffer(const std::string& name, size_t capacity = 16384);
    ~SHMRingBuffer();

    bool enqueue(void* data, size_t size);
    bool dequeue(void** data, size_t* size);

private:
    std::string m_name;
    int m_fd = -1;
    void* m_mmap = nullptr;
    SHMRing* m_ring = nullptr;
    void* m_data = nullptr;
    size_t m_capacity = 0;
    size_t m_total_size = 0;
};

} // namespace Exchange