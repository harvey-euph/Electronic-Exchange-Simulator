// SHMRing.cpp
#include "ring/SHMRingBuffer.hpp"
#include <sys/stat.h>
#include <iostream>

namespace Exchange {

SHMRingBuffer::SHMRingBuffer(const std::string& name, size_t capacity)
    : m_name("/" + name), m_capacity(capacity) // shm_open 通常要求以 '/' 開頭
{
    // 計算總大小：結構體大小 + 緩衝區大小
    // 假設你的資料區大小是 capacity，請根據你原本的實作調整
    m_total_size = sizeof(SHMRing) + m_capacity; 

    // 嘗試以 O_EXCL 建立。如果檔案已存在，此呼叫會失敗並回傳 EEXIST
    m_fd = shm_open(m_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
    
    bool is_creator = false;

    if (m_fd >= 0) {
        // 1. 進入此分支，代表目前 Process 是「第一個建立者」 (Creator)
        is_creator = true;
        
        // 調整 SHM 檔案大小
        if (ftruncate(m_fd, m_total_size) == -1) {
            shm_unlink(m_name.c_str());
            throw std::runtime_error("Failed to ftruncate SHM");
        }
    } else if (errno == EEXIST) {
        // 2. 進入此分支，代表 SHM 檔案早就被另一個 Process 創好了
        m_fd = shm_open(m_name.c_str(), O_RDWR, 0666);
        if (m_fd < 0) {
            throw std::runtime_error("Failed to open existing SHM");
        }
    } else {
        throw std::runtime_error("shm_open failed with unknown error");
    }

    // 3. 記憶體映射
    m_mmap = mmap(nullptr, m_total_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_mmap == MAP_FAILED) {
        close(m_fd);
        throw std::runtime_error("mmap failed");
    }

    m_ring = reinterpret_cast<SHMRing*>(m_mmap);
    m_data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(m_mmap) + sizeof(SHMRing));

    // 4. 根據身份執行「初始化」或「自旋等待」
    if (is_creator) {
        std::cout << "[SHMRing] " << m_name << " not found. Creating and initializing SHM..." << std::endl;
        
        // 初始化內部結構 (使用 memory_order_relaxed 因為此時還沒有其他人會讀這塊區塊)
        m_ring->head.store(0, std::memory_order_relaxed);
        m_ring->tail.store(0, std::memory_order_relaxed);
        m_ring->capacity = m_capacity;
        m_ring->mask = m_capacity - 1; // 假設 capacity 是 2 的冪次方
        
        m_ring->magic.store(SHM_RING_MAGIC, std::memory_order_relaxed);
        
        // 最後一步：發布 ready 訊號，讓等待中的 Consumer/Producer 通行
        m_ring->ready.store(1, std::memory_order_release);
    } else {
        std::cout << "[SHMRing] " << m_name << " already exists. Waiting for initialization..." << std::endl;
        
        // 自旋等待 ready 標記變為 1 (使用 acquire 確保能看見 Creator 寫入的所有變數)
        while (m_ring->ready.load(std::memory_order_acquire) != 1) {
            #if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause(); // 減少 CPU 功耗與流水線阻塞
            #else
                std::this_thread::yield();
            #endif
        }

        // 安全驗證 Magic Number
        if (m_ring->magic.load(std::memory_order_relaxed) != SHM_RING_MAGIC) {
            throw std::runtime_error("SHM Magic number mismatch! Corrupted or unexpected memory segment.");
        }
        
        std::cout << "[SHMRing] SHM is ready and verified." << std::endl;
    }
}

SHMRingBuffer::~SHMRingBuffer() {
    if (m_mmap && m_mmap != MAP_FAILED) {
        munmap(m_mmap, m_total_size);
    }
    if (m_fd >= 0) {
        close(m_fd);
    }
    // 注意：在低延遲交易系統中，我們通常「不」在析構子呼叫 shm_unlink。
    // 因為重啟單個 Process 時，我們希望共享記憶體裏的資料與結構依然留在作業系統中。
    // 如果你希望關閉時徹底刪除，可以手動呼叫 shm_unlink(m_name.c_str());
}

constexpr uint32_t WRAP_MARKER = 0xFFFFFFFF;

bool SHMRingBuffer::enqueue(void* data, size_t size) {
    if (!data || size == 0) return false;

    // 每筆資料需要：4位元組長度 + 實際 Payload 大小
    size_t required_space = sizeof(uint32_t) + size;
    
    // 如果單筆封包大於整個 Ring 的容量，這是不可能的任務
    if (required_space > m_capacity) return false;

    // 載入當前的 head 與 tail (核心關鍵：SPSC 下 tail 只有 Producer 會改，放 relaxed 即可)
    uint64_t current_tail = m_ring->tail.load(std::memory_order_relaxed);
    uint64_t current_head = m_ring->head.load(std::memory_order_acquire); // 確保看見 Consumer 更新的 head

    // 計算當前在 Buffer 內的 Offset 位置 (利用 mask)
    uint64_t tail_offset = current_tail & m_ring->mask;
    uint64_t head_offset = current_head & m_ring->mask;

    // 計算當前可用的連續剩餘空間
    size_t free_space = 0;
    if (tail_offset >= head_offset) {
        // tail 在 head 後面，剩餘空間被切成兩段：末端一段，開頭一段
        size_t space_to_end = m_capacity - tail_offset;
        
        if (space_to_end >= required_space) {
            // 情況 A：末端空間足夠寫入
            uint8_t* write_ptr = static_cast<uint8_t*>(m_data) + tail_offset;
            
            // 1. 寫入長度
            *reinterpret_cast<uint32_t*>(write_ptr) = static_cast<uint32_t>(size);
            // 2. 寫入 Payload
            std::memcpy(write_ptr + sizeof(uint32_t), data, size);
            
            // 3. 更新 tail (使用 release 確保 memcpy 的資料先於 tail 刷新被外面的 CPU 看見)
            m_ring->tail.store(current_tail + required_space, std::memory_order_release);
            return true;
        } 
        
        // 情況 B：末端空間不夠寫入，需要「繞回」到最前面 (Offset 0)
        // 此時必須確認開頭的空間（到 head 之前）是否夠放
        if (head_offset >= required_space) {
            uint8_t* write_ptr = static_cast<uint8_t*>(m_data) + tail_offset;
            
            // 1. 在末端打上 WRAP_MARKER 標記
            *reinterpret_cast<uint32_t*>(write_ptr) = WRAP_MARKER;
            
            // 2. 跑到最開頭 (Offset 0) 寫入真正的資料
            uint8_t* start_ptr = static_cast<uint8_t*>(m_data);
            *reinterpret_cast<uint32_t*>(start_ptr) = static_cast<uint32_t>(size);
            std::memcpy(start_ptr + sizeof(uint32_t), data, size);
            
            // 3. 推進 tail。注意：必須加上 (space_to_end + required_space)
            // 這樣才能讓 tail_offset 完美對齊到下一次的開頭
            m_ring->tail.store(current_tail + space_to_end + required_space, std::memory_order_release);
            return true;
        }
        
        // 空間不足（不論是末端還是繞回後的開頭都放不下）
        return false;
        
    } else {
        // tail 在 head 前面，剩下的可用空間是連續的一整塊 (head_offset - tail_offset)
        free_space = head_offset - tail_offset;
        
        if (free_space >= required_space) {
            uint8_t* write_ptr = static_cast<uint8_t*>(m_data) + tail_offset;
            *reinterpret_cast<uint32_t*>(write_ptr) = static_cast<uint32_t>(size);
            std::memcpy(write_ptr + sizeof(uint32_t), data, size);
            
            m_ring->tail.store(current_tail + required_space, std::memory_order_release);
            return true;
        }
        
        return false; // 空間不足
    }
}

bool SHMRingBuffer::dequeue(void** data, size_t* size) {
    if (!data || !size) return false;

    // 載入當前的 head 與 tail (SPSC 下 head 只有 Consumer 會改，放 relaxed 即可)
    uint64_t current_head = m_ring->head.load(std::memory_order_relaxed);
    uint64_t current_tail = m_ring->tail.load(std::memory_order_acquire); // 確保看見 Producer 寫完的 tail

    // 如果 head 等於 tail，代表 Ring 裡面空空如也
    if (current_head == current_tail) {
        return false;
    }

    uint64_t head_offset = current_head & m_ring->mask;
    uint8_t* read_ptr = static_cast<uint8_t*>(m_data) + head_offset;

    // 讀取標頭中的長度
    uint32_t length = *reinterpret_cast<uint32_t*>(read_ptr);

    if (length == 0 || length > m_capacity) {
        // Corrupt memory or invalid length
        return false;
    }

    // 處理繞回標記
    if (length == WRAP_MARKER) {
        // 遇到了末端邊界，說明真正的資料在最開頭 (Offset 0)
        uint64_t space_to_end = m_capacity - head_offset;
        
        // 修正 head 指標，直接跳過末端的無效 Padding 空間
        uint64_t new_head = current_head + space_to_end;
        
        // 再次檢查跳過後是否追上 tail，如果追上代表後面沒資料了
        if (new_head == current_tail) {
            m_ring->head.store(new_head, std::memory_order_relaxed); // 默默更新 head
            return false;
        }

        // 從最開頭讀取真正的資料
        read_ptr = static_cast<uint8_t*>(m_data);
        length = *reinterpret_cast<uint32_t*>(read_ptr);
        
        *size = length;
        *data = read_ptr + sizeof(uint32_t); // 傳回 Payload 的記憶體指標 (零拷貝)

        // 更新 head (使用 release 讓 Producer 知道這塊空間釋放了)
        m_ring->head.store(new_head + sizeof(uint32_t) + length, std::memory_order_release);
        return true;
    }

    // 正常情況：資料就在當前位置
    *size = length;
    *data = read_ptr + sizeof(uint32_t); // 傳回 Payload 的記憶體指標

    // 推進 head 指標
    m_ring->head.store(current_head + sizeof(uint32_t) + length, std::memory_order_release);
    return true;
}

} // namespace Exchange