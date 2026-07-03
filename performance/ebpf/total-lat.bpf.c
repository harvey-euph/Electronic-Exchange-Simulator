#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "ebpf_common.h"

char LICENSE[] SEC("license") = "GPL";

const volatile uint16_t target_port = 9001;

struct latency_event {
    uint8_t exec_type;
    uint8_t padding[7];
    uint64_t exec_id;
    uint64_t latency_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, uint32_t); // tid
    __type(value, struct recv_ctx);
} recv_ctx_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, uint8_t[512]);
} scratch_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, uint64_t); // exec_id
    __type(value, uint64_t); // start timestamp
} active_requests SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 262144); // 256KB
} rb SEC(".maps");


SEC("kprobe/tcp_recvmsg")
int BPF_KPROBE(tcp_recvmsg, struct sock *sk, struct msghdr *msg)
{
    uint16_t sport = BPF_CORE_READ(sk, __sk_common.skc_num);
    if (target_port != 0 && sport != target_port) return 0;

    uint32_t tid = bpf_get_current_pid_tgid();
    struct recv_ctx rctx = {};
    rctx.sk = sk;
    rctx.iter_type = BPF_CORE_READ(msg, msg_iter.iter_type);
    rctx.iov_offset = BPF_CORE_READ(msg, msg_iter.iov_offset);
    rctx.ubuf = BPF_CORE_READ(msg, msg_iter.ubuf);
    rctx.iov = BPF_CORE_READ(msg, msg_iter.__iov);
    rctx.nr_segs = BPF_CORE_READ(msg, msg_iter.nr_segs);
    rctx.ts0 = bpf_ktime_get_ns();

    bpf_map_update_elem(&recv_ctx_map, &tid, &rctx, BPF_ANY);
    return 0;
}

SEC("kretprobe/tcp_recvmsg")
int BPF_KRETPROBE(tcp_recvmsg_ret, int ret)
{
    uint32_t tid = bpf_get_current_pid_tgid();
    struct recv_ctx *rctx = bpf_map_lookup_elem(&recv_ctx_map, &tid);
    if (!rctx) return 0;

    uint8_t iter_type = rctx->iter_type;
    size_t iov_offset = rctx->iov_offset;
    void *ubuf = rctx->ubuf;
    const struct iovec *iov = rctx->iov;
    uint32_t nr_segs = rctx->nr_segs;
    uint64_t timestamp_ns = rctx->ts0;

    bpf_map_delete_elem(&recv_ctx_map, &tid);

    if (ret <= 0) return 0;

    uint32_t zero = 0;
    uint8_t *payload = bpf_map_lookup_elem(&scratch_map, &zero);
    if (!payload) return 0;

    copy_iov_iter(payload, iter_type, iov_offset, ubuf, iov, nr_segs);

    uint64_t exec_id = 0;
    if (parse_rx_exec_id(payload, ret, &exec_id)) {
        bpf_map_update_elem(&active_requests, &exec_id, &timestamp_ns, BPF_ANY);
    }
    return 0;
}

SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(tcp_sendmsg, struct sock *sk, struct msghdr *msg, size_t size)
{
    uint16_t sport = BPF_CORE_READ(sk, __sk_common.skc_num);
    if (target_port != 0 && sport != target_port)
        return 0;

    uint32_t zero = 0;
    uint8_t *payload = bpf_map_lookup_elem(&scratch_map, &zero);
    if (!payload) return 0;

    uint64_t timestamp_ns = bpf_ktime_get_ns();

    uint8_t iter_type = BPF_CORE_READ(msg, msg_iter.iter_type);
    size_t iov_offset = BPF_CORE_READ(msg, msg_iter.iov_offset);
    void *ubuf = BPF_CORE_READ(msg, msg_iter.ubuf);
    const struct iovec *iov = BPF_CORE_READ(msg, msg_iter.__iov);
    uint32_t nr_segs = BPF_CORE_READ(msg, msg_iter.nr_segs);

    copy_iov_iter(payload, iter_type, iov_offset, ubuf, iov, nr_segs);

    uint64_t exec_id = 0;
    uint8_t exec_type = 0;
    if (parse_tx_exec_id(payload, size, &exec_id, &exec_type)) {
        uint64_t *start_ts = bpf_map_lookup_elem(&active_requests, &exec_id);
        if (start_ts) {
            uint64_t lat = timestamp_ns - *start_ts;
            struct latency_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
            if (e) {
                e->exec_type = exec_type;
                e->exec_id = exec_id;
                e->latency_ns = lat;
                bpf_ringbuf_submit(e, 0);
            }
            bpf_map_delete_elem(&active_requests, &exec_id);
        }
    }
    return 0;
}
