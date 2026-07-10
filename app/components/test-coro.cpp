#include <coroutine>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <queue>
#include <syncstream>
#include <thread>
#include <random>
#include "ipc/SHMRingBuffer.hpp"

using namespace std;
using namespace Exchange;

struct Request {
    int id;
    long long enqueue_time_us;
    char payload[52];
};

struct Response {
    int id;
    char result[128];
};

// Coroutine Boilerplate
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        suspend_never initial_suspend() { return {}; }
        suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { terminate(); }
    };
};

// Parent process state
unordered_map<int, coroutine_handle<>> pending_coroutines;
unordered_map<int, Response> ready_responses;
SHMRingBuffer* req_ring_ptr = nullptr;

struct GetAwaiter {
    Request request;

    bool await_ready() const { return false; }

    void await_suspend(coroutine_handle<> handle) {
        pending_coroutines[request.id] = handle;
        osyncstream(cout) << "P [Awaiter] suspending, enqueue request " << request.id << endl;
        
        request.enqueue_time_us = chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now().time_since_epoch()).count();
            
        while (!req_ring_ptr->enqueue(&request, sizeof(Request))) {
            this_thread::yield();
        }
    }

    Response await_resume() {
        Response resp = ready_responses[request.id];
        ready_responses.erase(request.id);
        return resp;
    }
};

GetAwaiter get_async(int id, string payload) {
    Request req;
    req.id = id;
    strncpy(req.payload, payload.c_str(), sizeof(req.payload) - 1);
    req.payload[sizeof(req.payload) - 1] = '\0';
    return GetAwaiter{req};
}

Task client(queue<Response>& outputs, int id, string msg) {
    osyncstream(cout) << "Q [Coroutine] started for '" << msg << "'" << endl;
    auto resp = co_await get_async(id, msg);
    outputs.push(resp);
}

void worker_process() {
    osyncstream(cout) << "      [Worker] Process started (PID: " << getpid() << ")" << endl;
    
    SHMRingBuffer req_ring("test_req_ring", 1024 * 1024);
    SHMRingBuffer resp_ring("test_resp_ring", 1024 * 1024);

    while (true) {
        auto slot = req_ring.acquire();
        if (!slot) {
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }
        
        auto dequeue_time_us = chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now().time_since_epoch()).count();

        Request req;
        memcpy(&req, slot->payload, sizeof(Request));
        req_ring.release(*slot);

        if (strcmp(req.payload, "STOP") == 0) {
            osyncstream(cout) << "W     [Worker] Stop received, exiting" << endl;
            exit(0);
        }

        long long queue_delay_us = dequeue_time_us - req.enqueue_time_us;
        osyncstream(cout) << "W     [Worker] processing request '" << req.payload 
                            << "' (Queue Delay: " << queue_delay_us << " us)" << endl;
        
        // 模擬耗時運算
        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(2, 6);
        int sleep_time = dist(rng);
        this_thread::sleep_for(chrono::milliseconds(sleep_time));
        osyncstream(cout) << "W     [Worker] process done for '" << req.payload << "' with " << sleep_time << "ms." << endl;

        Response resp;
        resp.id = req.id;
        snprintf(resp.result, sizeof(resp.result), "processed: %s", req.payload);

        while (!resp_ring.enqueue(&resp, sizeof(Response))) {
            this_thread::yield();
        }
    }
}

int main() {
    // 1. 初始化
    shm_unlink("/test_req_ring");
    shm_unlink("/test_resp_ring");

    // 2. Fork Worker Process
    pid_t pid = fork();
    if (pid == 0) {
        worker_process();
        return 0;
    }

    // 3. Parent Process
    SHMRingBuffer req_ring("test_req_ring", 1024 * 1024);
    SHMRingBuffer resp_ring("test_resp_ring", 1024 * 1024);
    req_ring_ptr = &req_ring;

    int total_requests = 10;
    vector<string> requests;
    for (int i = 0; i < total_requests; ++i) {
        requests.push_back(to_string(i));
    }

    int received_count = 0;
    int req_index = 0;
    queue<Response> outputs;
    unordered_map<int, chrono::high_resolution_clock::time_point> req_start_times;

    auto main_start_time = chrono::high_resolution_clock::now();
    osyncstream(cout) << "  [Main] Polling loop started" << endl;
    
    while (received_count < total_requests) {
        if (req_index < total_requests) {
            req_start_times[req_index] = chrono::high_resolution_clock::now();
            if (req_index % 2 == 0) {
                osyncstream(cout) << "Q [Main Loop] req: dispatching to coroutine -> " << requests[req_index] << endl;
                client(outputs, req_index, requests[req_index]);
            } else {
                osyncstream(cout) << "Q [Main Loop] req: start direct processing -> " << requests[req_index] << endl;
                this_thread::sleep_for(chrono::milliseconds(5));
                Response resp;
                resp.id = req_index;
                snprintf(resp.result, sizeof(resp.result), "direct: %s", requests[req_index].c_str());
                outputs.push(resp);
                osyncstream(cout) << "Q [Main Loop] req: processed " << requests[req_index] << endl;
            }
            req_index++;
        }

        // 直接透過 SHMRingBuffer 進行 Lock-free polling
        while (true) {
            auto slot = resp_ring.acquire();
            if (!slot) break;

            Response resp;
            memcpy(&resp, slot->payload, sizeof(Response));
            resp_ring.release(*slot);

            ready_responses[resp.id] = resp;
            
            auto it = pending_coroutines.find(resp.id);
            if (it != pending_coroutines.end()) {
                auto handle = it->second;
                pending_coroutines.erase(it);
                // 在 Main Process 喚醒協程！
                handle.resume();
            }
        }
        
        // Polling 負責印出得到的東西
        while (!outputs.empty()) {
            auto r = outputs.front();
            outputs.pop();
            osyncstream(cout) << "P >>> [Main Loop] resp printed: " << r.id << " -> " << r.result << endl;
            received_count++;
        }

    }

    auto main_end_time = chrono::high_resolution_clock::now();
    auto total_ms = chrono::duration_cast<chrono::milliseconds>(main_end_time - main_start_time).count();
    osyncstream(cout) << "  [Main] All complete. Shutting down. Total execution time: " << total_ms << " ms" << endl;

    // 發送結束訊號
    Request stop_req{-1, 0, "STOP"};
    while (!req_ring.enqueue(&stop_req, sizeof(Request))) {
        this_thread::yield();
    }
    
    waitpid(pid, nullptr, 0);
    
    shm_unlink("/test_req_ring");
    shm_unlink("/test_resp_ring");

    return 0;
}