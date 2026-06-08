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
    
    alignas(64) std::atomic<uint64_t> prod_head{0}; // Producer 預約進度
    alignas(64) std::atomic<uint64_t> prod_tail{0}; // Producer 寫入完成進度
    
    alignas(64) std::atomic<uint64_t> cons_head{0}; // Consumer 讀取進度
    
    uint64_t capacity;
    uint64_t mask;
};

template <bool ReadOnly = false>
class SHMRingBufferImpl {
public:
    SHMRingBufferImpl(const std::string& name, size_t capacity = 16384);
    ~SHMRingBufferImpl();

    bool enqueue(void* data, size_t size) requires (!ReadOnly);
    bool dequeue(void** data, size_t* size) requires (!ReadOnly);

    // 監控與統計指標 API
    constexpr bool is_read_only() const { return ReadOnly; }
    uint64_t get_capacity() const { return m_capacity; }
    uint64_t get_reserved_depth() const;
    uint64_t get_uncommitted_depth() const;
    double get_occupancy_ratio() const;

private:
    std::string m_name;
    int m_fd = -1;
    void* m_mmap = nullptr;
    SHMRing* m_ring = nullptr;
    void* m_data = nullptr;
    size_t m_capacity = 0;
    size_t m_total_size = 0;
};

using SHMRingBuffer = SHMRingBufferImpl<false>;
using SHMObserver = SHMRingBufferImpl<true>;

} // namespace Exchange