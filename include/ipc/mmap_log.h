#pragma once

#include <string>
#include <cstdint>
#include <atomic>
#include <functional>

namespace mmaplog {

constexpr uint32_t EOF_MARKER = 0xFFFFFFFF;
constexpr uint64_t INVALID_OFFSET = 0xFFFFFFFFFFFFFFFF;

// Lock-free record header
struct RecordHeader {
    uint32_t reserved_length;
    std::atomic<uint32_t> published_length;
};

class MmapWriter {
public:
    MmapWriter(const std::string& dir, size_t max_file_size = 1 * 64 * 1024);
    ~MmapWriter();

    // Appends data and returns the globally unique offset. (Convenience wrapper)
    // The offset structure: (file_index << 32) | local_byte_offset
    uint64_t append(const void* data, uint32_t len);

    // Zero-Copy Write API: Reserve space and get a direct pointer to the mapped memory.
    void* reserve(uint32_t len, uint64_t& out_offset);

    // Zero-Copy Write API: Publish the record to readers. 
    // 'payload_ptr' must be the pointer returned by reserve().
    void commit(void* payload_ptr);

    using RolloverCallback = std::function<void(uint32_t old_file_index, uint32_t new_file_index)>;
    void set_rollover_callback(RolloverCallback cb) { rollover_cb_ = std::move(cb); }

private:
    RolloverCallback rollover_cb_;
    void open_file(uint32_t file_index);

    std::string dir_;
    size_t max_file_size_;
    uint32_t current_file_index_;
    int fd_;
    uint8_t* mapped_addr_;
    size_t current_offset_;
};

class MmapReader {
public:
    MmapReader(const std::string& dir);
    ~MmapReader();

    // Zero-copy read. Returns true if new data is available.
    // 'data' points directly to the memory-mapped page cache.
    bool read_next(const void*& data, uint32_t& len);

    // Seek to a global offset (e.g. for reconnecting clients).
    bool seek(uint64_t offset);

private:
    void open_file(uint32_t file_index);

    std::string dir_;
    uint32_t current_file_index_;
    int fd_;
    uint8_t* mapped_addr_;
    size_t current_offset_;
    size_t file_size_; 
};

} // namespace mmaplog
