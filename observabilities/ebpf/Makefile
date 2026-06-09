CXX = g++
CLANG = clang
BPFTOOL = bpftool

CXXFLAGS = -O2 -Wall -std=c++17 -I../../include
BPF_CFLAGS = -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include -I/usr/include/x86_64-linux-gnu -I.

LIBBPF_LD = -lbpf

all: ws_monitor

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

ws_monitor.bpf.o: ws_monitor.bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

ws_monitor.skel.h: ws_monitor.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

ws_monitor: ws_monitor.cpp ws_monitor.skel.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIBBPF_LD)

clean:
	rm -f ws_monitor.bpf.o ws_monitor.skel.h ws_monitor vmlinux.h

.PHONY: all clean
