#include <bpf/libbpf.h>
#include <iostream>
int main() {
    DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
    uprobe_opts.func_name = "_ZN8Exchange13ClientManager22process_client_requestESt10shared_ptrINS_8WSClientEEPKvm";
    // wait we need a skeleton or bpf object to test attach. Let's just modify lat-tracer.cpp to print errors
}
