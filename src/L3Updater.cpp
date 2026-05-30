#include "L3Updater.hpp"
#include <flatbuffers/flatbuffers.h>
#include <chrono>
#include <iostream>

namespace Exchange {

L3Updater::L3Updater(const std::string& ring_name, unsigned int ring_size)
    : fbb(128) 
{
    try {
        m_ring = new SHMRingBuffer(ring_name, ring_size);
    } catch (const std::exception& e) {
        std::cerr << "[L3Updater] Failed to create SHMRingBuffer: " << e.what() << std::endl;
        m_ring = nullptr;
    }
}

L3Updater::~L3Updater()
{
    if (m_ring) {
        delete m_ring;
        m_ring = nullptr;
    }
}

bool L3Updater::update(uint32_t symbol_id, ExecType exec_type, uint64_t order_id, Side side, int64_t price, uint64_t qty)
{
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::system_clock::now().time_since_epoch()
               ).count();

    return update(symbol_id, exec_type, order_id, side, price, qty, static_cast<uint64_t>(now));
}

bool L3Updater::update(uint32_t symbol_id, ExecType exec_type, uint64_t order_id, Side side, int64_t price, uint64_t qty, uint64_t timestamp)
{
    if (m_ring == nullptr) return false;

    fbb.Clear();

    uint64_t seq = m_seq_num.fetch_add(1, std::memory_order_relaxed);

    auto l3_update = CreateL3Update(fbb, symbol_id, exec_type, seq, order_id, side, price, qty, timestamp);
    fbb.Finish(l3_update);

    size_t size = fbb.GetSize();
    void* buf_ptr = fbb.GetBufferPointer();

    if (!m_ring->enqueue(buf_ptr, size)) {
        return false; 
    }
    return true;
}

} // namespace Exchange
