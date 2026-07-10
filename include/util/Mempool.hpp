#pragma once

#include "ipc/SHMRingBuffer.hpp"
#include <string>
#include <stdexcept>
#include <new>
#include <cstdint>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <atomic>

namespace Exchange {

template <typename T>
class Mempool {
private:
    struct alignas(T) Element {
        uint8_t data[sizeof(T)];
    };

    // Shared state header in SHM
    struct PoolHeader {
        uint32_t magic;
        uint32_t capacity;
        std::atomic<uint32_t> ready;
    };

    static constexpr uint32_t POOL_MAGIC = 0x504F4F4C; // "POOL"

public:
    Mempool(const std::string& name, size_t capacity) 
        : name_(name), capacity_(capacity) {
        
        std::string shm_name = "/" + name + "_mempool_data";
        size_t total_size = sizeof(PoolHeader) + capacity * sizeof(Element);

        int fd = shm_open(shm_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        bool is_creator = false;

        if (fd >= 0) {
            is_creator = true;
            if (ftruncate(fd, total_size) == -1) {
                shm_unlink(shm_name.c_str());
                throw std::runtime_error("Failed to ftruncate Mempool SHM");
            }
        } else if (errno == EEXIST) {
            fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
            if (fd < 0) {
                throw std::runtime_error("Failed to open existing Mempool SHM");
            }
        } else {
            throw std::runtime_error("shm_open failed for Mempool");
        }

        void* mmap_ptr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mmap_ptr == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("mmap failed for Mempool");
        }

        m_fd = fd;
        m_mmap = mmap_ptr;
        m_total_size = total_size;

        PoolHeader* header = reinterpret_cast<PoolHeader*>(m_mmap);
        pool_ = reinterpret_cast<Element*>(reinterpret_cast<uint8_t*>(m_mmap) + sizeof(PoolHeader));

        // Calculate ring buffer size (must be power of 2)
        // Each enqueue takes sizeof(uint32_t) + sizeof(uint32_t) = 8 bytes.
        size_t min_ring_size = (capacity + 1) * (sizeof(uint32_t) + sizeof(uint32_t)) * 2; 
        size_t ring_size = 1024;
        while (ring_size < min_ring_size) {
            ring_size *= 2;
        }

        ring_ = new SHMRingBuffer(name + "_mempool_ring", ring_size);

        if (is_creator) {
            std::memset(m_mmap, 0, total_size);
            header->capacity = capacity;
            header->magic = POOL_MAGIC;
            
            // Enqueue all available indices
            for (uint32_t i = 0; i < capacity; ++i) {
                if (!ring_->enqueue(&i, sizeof(uint32_t))) {
                    throw std::runtime_error("Failed to enqueue index to mempool ring during initialization");
                }
            }
            
            header->ready.store(1, std::memory_order_release);
        } else {
            // Wait for creator to initialize
            while (header->ready.load(std::memory_order_acquire) != 1) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #else
                    std::this_thread::yield();
                #endif
            }

            if (header->magic != POOL_MAGIC) {
                throw std::runtime_error("Mempool magic number mismatch!");
            }
            
            // Override local capacity with the actual created capacity
            capacity_ = header->capacity;
        }
    }

    ~Mempool() {
        delete ring_;
        if (m_mmap && m_mmap != MAP_FAILED) {
            munmap(m_mmap, m_total_size);
        }
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    static void unlink(const std::string& name) {
        shm_unlink(("/" + name + "_mempool_data").c_str());
        shm_unlink(("/" + name + "_mempool_ring").c_str());
    }

    // Allocate memory and construct the object
    template <typename... Args>
    T* allocate(Args&&... args) {
        auto slot = ring_->acquire();
        if (!slot) return nullptr; // Out of memory

        T* ptr = nullptr;
        if (slot->size == sizeof(uint32_t)) {
            uint32_t index = *reinterpret_cast<const uint32_t*>(slot->payload);
            if (index < capacity_) {
                ptr = reinterpret_cast<T*>(&pool_[index]);
                new (ptr) T(std::forward<Args>(args)...);
            }
        }
        ring_->release(*slot);
        return ptr;
    }

    // Call destructor and return memory to the pool
    void deallocate(T* ptr) {
        if (!ptr) return;
        uint32_t index = get_index(ptr);
        // Call destructor
        ptr->~T();
        ring_->enqueue(&index, sizeof(uint32_t));
    }

    uint32_t get_index(T* ptr) const {
        std::ptrdiff_t diff = reinterpret_cast<uint8_t*>(ptr) - reinterpret_cast<uint8_t*>(pool_);
        if (diff < 0 || diff >= static_cast<std::ptrdiff_t>(capacity_ * sizeof(Element))) {
            throw std::out_of_range("Pointer does not belong to this mempool");
        }
        return static_cast<uint32_t>(diff / sizeof(Element));
    }

    T* get_pointer(uint32_t index) const {
        if (index >= capacity_) {
            throw std::out_of_range("Index out of bounds");
        }
        return reinterpret_cast<T*>(&pool_[index]);
    }

    size_t get_capacity() const { return capacity_; }

private:
    std::string name_;
    size_t capacity_;
    int m_fd = -1;
    void* m_mmap = nullptr;
    size_t m_total_size = 0;
    
    SHMRingBuffer* ring_;
    Element* pool_;
};

} // namespace Exchange
