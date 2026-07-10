#pragma once
#include <atomic>
#include <signal.h>

inline std::atomic<bool> g_running{true};

inline void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

inline void setup_signals() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
}
