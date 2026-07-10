#pragma once
#include <pthread.h>
#include <sched.h>
#include <iostream>
#include <string>

#include <unistd.h>

namespace Exchange {

inline bool set_thread_affinity(int core_id, const std::string& thread_name = "Thread") {
#ifdef __linux__
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores <= 1) {
        std::cout << "[ThreadUtil] Skipping pinning for " << thread_name << " (system has only " << num_cores << " core)" << std::endl;
        return true;
    }
    if (core_id < 0 || core_id >= num_cores) {
        std::cout << "[ThreadUtil] Skipping pinning for " << thread_name << " (CPU " << core_id << " is out of range, total online cores: " << num_cores << ")" << std::endl;
        return true;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "[ThreadUtil] Failed to set affinity for " << thread_name << " to CPU " << core_id << ": error=" << rc << std::endl;
        return false;
    }
    std::cout << "[ThreadUtil] Successfully pinned " << thread_name << " to CPU " << core_id << std::endl;
    return true;
#else
    (void)core_id; (void)thread_name;
    return false;
#endif
}

} // namespace Exchange
