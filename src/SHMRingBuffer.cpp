// SHMRing.cpp
#include "ring/SHMRingBuffer.hpp"
#include <sys/stat.h>
#include <iostream>

namespace Exchange {

SHMRingBuffer::SHMRingBuffer(const std::string& name, size_t capacity)
    : m_name("/" + name), m_capacity(capacity) // shm_open 通常要求以 '/' 開頭
{
    // 計算總大小：結構體大小 + 緩衝區大小 + 4位元組安全 Padding
    m_total_size = sizeof(SHMRing) + m_capacity + sizeof(uint32_t); 

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
        
        // 清空整塊記憶體，確保沒有殘留垃圾
        std::memset(m_mmap, 0, m_total_size);

        // 初始化內部結構 (使用 memory_order_relaxed 因為此時還沒有其他人會讀這塊區塊)
        m_ring->prod_head.store(0, std::memory_order_relaxed);
        m_ring->prod_tail.store(0, std::memory_order_relaxed);
        m_ring->cons_head.store(0, std::memory_order_relaxed);
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

    uint64_t old_prod_head, new_prod_head;
    uint64_t tail_offset;
    size_t space_to_end;
    bool wrapped = false;

    // 1. 預約空間 (Reservation Phase)
    while (true) {
        old_prod_head = m_ring->prod_head.load(std::memory_order_acquire);
        uint64_t current_cons_head = m_ring->cons_head.load(std::memory_order_acquire);
        
        tail_offset = old_prod_head & m_ring->mask;
        space_to_end = m_capacity - tail_offset;

        if (space_to_end >= required_space) {
            // 情況 A：末端空間足夠
            new_prod_head = old_prod_head + required_space;
            wrapped = false;
        } else {
            // 情況 B：末端空間不夠，需要繞回。總消耗 = 末端填充 + 實際空間
            new_prod_head = old_prod_head + space_to_end + required_space;
            wrapped = true;
        }

        // 檢查剩餘絕對空間是否足夠 (防止 Producer 追上 Consumer)
        if (new_prod_head - current_cons_head > m_capacity) {
            return false;
        }

        // 原子地嘗試更新 prod_head 以完成預約
        if (m_ring->prod_head.compare_exchange_weak(old_prod_head, new_prod_head, 
                                                   std::memory_order_acquire, 
                                                   std::memory_order_relaxed)) {
            break;
        }
        // 若 CAS 失敗，代表有其他 Producer 搶先，繼續迴圈重試
    }

    // 2. 寫入資料 (Writing Phase)
    if (!wrapped) {
        uint8_t* write_ptr = static_cast<uint8_t*>(m_data) + tail_offset;
        *reinterpret_cast<uint32_t*>(write_ptr) = static_cast<uint32_t>(size);
        std::memcpy(write_ptr + sizeof(uint32_t), data, size);
    } else {
        uint8_t* write_ptr = static_cast<uint8_t*>(m_data) + tail_offset;
        *reinterpret_cast<uint32_t*>(write_ptr) = WRAP_MARKER;
        
        uint8_t* start_ptr = static_cast<uint8_t*>(m_data);
        *reinterpret_cast<uint32_t*>(start_ptr) = static_cast<uint32_t>(size);
        std::memcpy(start_ptr + sizeof(uint32_t), data, size);
    }

    // 3. 提交 (Commit Phase)
    // 必須等到 prod_tail 追上自己的 old_prod_head，才代表輪到自己提交
    while (m_ring->prod_tail.load(std::memory_order_acquire) != old_prod_head) {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #else
            std::this_thread::yield();
        #endif
    }
    
    // 更新 prod_tail，讓 Consumer 可以看見這批新資料，也讓下一個 Producer 准許提交
    m_ring->prod_tail.store(new_prod_head, std::memory_order_release);
    return true;
}

bool SHMRingBuffer::dequeue(void** data, size_t* size) {
    if (!data || !size) return false;

    // 載入當前的 cons_head 與 prod_tail (MPSC 下 cons_head 只有單個 Consumer 會改，放 relaxed 即可)
    uint64_t current_head = m_ring->cons_head.load(std::memory_order_relaxed);
    uint64_t current_tail = m_ring->prod_tail.load(std::memory_order_acquire); // 確保看見 Producer 寫完並提交的 prod_tail

    // 如果 head 等於 tail，代表 Ring 裡面空空如也
    if (current_head == current_tail) {
        return false;
    }

    uint64_t head_offset = current_head & m_ring->mask;
    uint8_t* read_ptr = static_cast<uint8_t*>(m_data) + head_offset;

    // 讀取標頭中的長度
    uint32_t length = *reinterpret_cast<uint32_t*>(read_ptr);

    if (current_head != current_tail && (length == 0 || length > m_capacity) && length != WRAP_MARKER) {
        std::cerr << "[SHMRing] Corrupt length: " << length << " head=" << current_head << " tail=" << current_tail << " offset=" << head_offset << std::endl;
    }

    // 處理繞回標記
    if (length == WRAP_MARKER) {
        // 遇到了末端邊界，說明真正的資料在最開頭 (Offset 0)
        uint64_t space_to_end = m_capacity - head_offset;
        
        // 修正 head 指標，直接跳過末端的無效 Padding 空間
        uint64_t new_head = current_head + space_to_end;
        
        // 再次檢查跳過後是否追上 tail，如果追上代表後面沒資料了
        if (new_head == current_tail) {
            m_ring->cons_head.store(new_head, std::memory_order_relaxed); // 默默更新 head
            return false;
        }

        // 從最開頭讀取真正的資料
        read_ptr = static_cast<uint8_t*>(m_data);
        length = *reinterpret_cast<uint32_t*>(read_ptr);
        
        if (length == 0 || length > m_capacity) {
            return false;
        }

        *size = length;
        *data = read_ptr + sizeof(uint32_t); // 傳回 Payload 的記憶體指標 (零拷貝)

        // 更新 cons_head (使用 release 讓 Producer 知道這塊空間釋放了)
        m_ring->cons_head.store(new_head + sizeof(uint32_t) + length, std::memory_order_release);
        return true;
    }

    if (length == 0 || length > m_capacity) {
        // Corrupt memory or invalid length
        return false;
    }

    // 正常情況：資料就在當前位置
    *size = length;
    *data = read_ptr + sizeof(uint32_t); // 傳回 Payload 的記憶體指標

    // 推進 cons_head 指標
    m_ring->cons_head.store(current_head + sizeof(uint32_t) + length, std::memory_order_release);
    return true;
}

} // namespace Exchange