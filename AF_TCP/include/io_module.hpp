#include <xdp/libxdp.h>
#include <net/if.h>

#define MAX_PKT_BURST 64

class IO_module{
private:
    const char* if_name;
    int queue_id;
    struct xdp_program *prog = nullptr;

    void* packet_buffer;
    struct xsk_umem* umem;

    struct xsk_ring_prod fill_ring;
    struct xsk_ring_cons comp_ring;
    struct xsk_ring_cons rx_ring;
    struct xsk_ring_prod tx_ring;
    struct xsk_socket* xsk;
public:
    IO_module(const char* _if_name, int _queue_id) : if_name(_if_name), queue_id(_queue_id) {}
    void	  (*load_module)(void);
	void      (*init_handle)(AF_TCP_thread_context *ctx);
	int32_t   (*link_devices)(AF_TCP_thread_context *ctx);
	void      (*release_pkt)(AF_TCP_thread_context *ctx, int ifidx, unsigned char *pkt_data, int len);
	uint8_t * (*get_wptr)(AF_TCP_thread_context *ctx, int ifidx, uint16_t len);
	int32_t   (*send_pkts)(AF_TCP_thread_context *ctx, int nif);
	uint8_t * (*get_rptr)(AF_TCP_thread_context *ctx, int ifidx, int index, uint16_t *len);
	int32_t   (*recv_pkts)(AF_TCP_thread_context *ctx, int ifidx);
	int32_t	  (*select)(AF_TCP_thread_context *ctx);
	void	  (*destroy_handle)(AF_TCP_thread_context *ctx);
	int32_t	  (*dev_ioctl)(AF_TCP_thread_context *ctx, int nif, int cmd, void *argp);
};