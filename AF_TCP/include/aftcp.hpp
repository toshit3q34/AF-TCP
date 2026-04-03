#include "io_module.hpp"

#ifndef MAX_CPUS
#define MAX_CPUS                        16
#endif

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

struct aftcp_config
{
	/* network interface config */
	struct eth_table *eths;
	int *nif_to_eidx; // mapping physic port indexes to that of the configured port-list
	int eths_num;

	/* route config */
	struct route_table *rtable;		// routing table
	struct route_table *gateway;	
	int routes;						// # of entries

	/* arp config */
	struct arp_table arp;

	int num_cores;
	int num_mem_ch;
	int max_concurrency;
#ifndef DISABLE_DPDK
	mpz_t _cpumask;
#endif

	int max_num_buffers;
	int rcvbuf_size;
	int sndbuf_size;
	
	int tcp_timewait;
	int tcp_timeout;

	/* adding multi-process support */
	uint8_t multi_process;
	uint8_t multi_process_is_master;

#ifdef ENABLE_ONVM
	struct onvm_nf_local_ctx *nf_local_ctx;
	/* onvm specific args */
	uint16_t onvm_serv;
  	uint16_t onvm_inst;
  	uint16_t onvm_dest;
#endif
#if USE_CCP
    char     cc[CC_NAME];
#endif
};

struct aftcp_context
{
	int cpu;
};

extern struct mtcp_manager *g_mtcp[MAX_CPUS];
extern struct mtcp_config CONFIG;