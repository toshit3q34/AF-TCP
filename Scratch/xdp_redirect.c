#define BPF_NO_PRESERVE_ACCESS_INDEX
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* The XSKMAP holds the file descriptors of AF_XDP sockets.
 * The key is the hardware queue ID, the value is the socket FD. 
 */
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} xsks_map SEC(".maps");

SEC("xdp")
int xdp_redirect(struct xdp_md *ctx)
{
    __u32 index = ctx->rx_queue_index;

    /* Redirect the packet to the socket registered for this queue.
     * If no socket is found, it returns XDP_PASS to the kernel stack. 
     */
    return bpf_redirect_map(&xsks_map, index, XDP_PASS);
}

char _license[] SEC("license") = "GPL";