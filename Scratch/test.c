#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <poll.h>

#include <xdp/xsk.h>
#include <linux/if_link.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#define NUM_FRAMES 4096
#define FRAME_SIZE 2048
#define BATCH_SIZE 64

struct xsk_umem_info {
    struct xsk_umem *umem;
    void *buffer;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
};

struct xsk_socket_info {
    struct xsk_socket *xsk;
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_umem_info *umem;
};

/* --- Configuration Helpers --- */

static struct xsk_umem_info *configure_umem(void) {
    struct xsk_umem_info *umem = calloc(1, sizeof(*umem));
    if (!umem) return NULL;

    /* UMEM memory must be page-aligned */
    if (posix_memalign(&umem->buffer, getpagesize(), NUM_FRAMES * FRAME_SIZE)) {
        perror("posix_memalign");
        exit(1);
    }

    struct xsk_umem_config cfg = {
        .fill_size = NUM_FRAMES,
        .comp_size = NUM_FRAMES,
        .frame_size = FRAME_SIZE,
        .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
        .flags = 0
    };

    int ret = xsk_umem__create(&umem->umem, umem->buffer, 
                               NUM_FRAMES * FRAME_SIZE, 
                               &umem->fq, &umem->cq, &cfg);
    if (ret) {
        fprintf(stderr, "UMEM create failed: %s\n", strerror(-ret));
        return NULL;
    }
    return umem;
}

static struct xsk_socket_info *configure_socket(const char *ifname, 
                                                uint32_t queue_id, 
                                                struct xsk_umem_info *umem) {
    struct xsk_socket_info *xsk = calloc(1, sizeof(*xsk));
    if (!xsk) return NULL;

    xsk->umem = umem;
    struct xsk_socket_config cfg = {
        .rx_size = NUM_FRAMES,
        .tx_size = NUM_FRAMES,
        .libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD,
        .xdp_flags = XDP_FLAGS_SKB_MODE, // Generic XDP
        .bind_flags = XDP_COPY,          // Copy mode for virtual interfaces
    };

    int ret = xsk_socket__create(&xsk->xsk, ifname, queue_id, 
                                 umem->umem, &xsk->rx, &xsk->tx, &cfg);
    if (ret) {
        fprintf(stderr, "Socket create failed: %s\n", strerror(-ret));
        return NULL;
    }
    return xsk;
}

/* --- Main Entry Point --- */

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <ifname> <queue_id>\n", argv[0]);
        return 1;
    }

    const char *ifname = argv[1];
    uint32_t queue_id = atoi(argv[2]);

    /* 1. Load the BPF object and find the map/program */
    struct bpf_object *obj = bpf_object__open_file("xdp_redirect.o", NULL);
    if (!obj || bpf_object__load(obj)) {
        fprintf(stderr, "Error: Could not load xdp_redirect.o\n");
        return 1;
    }

    int prog_fd = bpf_program__fd(bpf_object__find_program_by_name(obj, "xdp_redirect"));
    int map_fd = bpf_map__fd(bpf_object__find_map_by_name(obj, "xsks_map"));

    /* 2. Attach the XDP program to the interface */
    if (bpf_xdp_attach(if_nametoindex(ifname), prog_fd, XDP_FLAGS_SKB_MODE, NULL)) {
        fprintf(stderr, "Error: Failed to attach XDP to %s\n", ifname);
        return 1;
    }

    /* 3. Setup UMEM and Socket */
    struct xsk_umem_info *umem = configure_umem();
    struct xsk_socket_info *xsk = configure_socket(ifname, queue_id, umem);
    if (!umem || !xsk) return 1;

    /* 4. Link the socket to the BPF map */
    if (xsk_socket__update_xskmap(xsk->xsk, map_fd)) {
        fprintf(stderr, "Error: Failed to update XSKMAP\n");
        return 1;
    }

    /* 5. Initialize Fill Ring (Prime the kernel with empty buffers) */
    uint32_t idx_fq;
    int ret = xsk_ring_prod__reserve(&umem->fq, NUM_FRAMES, &idx_fq);
    if (ret > 0) {
        for (int i = 0; i < ret; i++)
            *xsk_ring_prod__fill_addr(&umem->fq, idx_fq + i) = i * FRAME_SIZE;
        xsk_ring_prod__submit(&umem->fq, ret);
    }

    printf("AF_XDP app running on %s (Queue %u)...\n", ifname, queue_id);

    /* 6. Main Processing Loop */
    while (1) {
        unsigned int rcvd, idx_rx = 0;
        
        /* Peek at the RX ring to see if packets have arrived */
        rcvd = xsk_ring_cons__peek(&xsk->rx, BATCH_SIZE, &idx_rx);
        if (!rcvd) continue;

        /* Reserve slots in the Fill Ring to recycle these buffers later */
        uint32_t idx_fq_recycle;
        int reserve_ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq_recycle);

        for (int i = 0; i < rcvd; i++) {
            const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx + i);
            
            /* desc->addr is the offset in the UMEM buffer */
            printf("Packet Received! Offset: %lu, Length: %u\n", (unsigned long)desc->addr, desc->len);

            /* Recycle the frame back to the Fill Ring */
            if (reserve_ret == rcvd) {
                *xsk_ring_prod__fill_addr(&umem->fq, idx_fq_recycle + i) = desc->addr;
            }
        }

        xsk_ring_cons__release(&xsk->rx, rcvd);
        if (reserve_ret == rcvd) {
            xsk_ring_prod__submit(&umem->fq, rcvd);
        }
    }

    return 0;
}