#include <stdlib.h>
#include <stdio.h>

#include "config.hpp"
#include "aftcp.hpp"
#include "debug.hpp"

// Maximum length of a line in config file
#define MAX_OPTLINE_LEN 			1024

struct mtcp_manager *g_mtcp[MAX_CPUS] = 	{NULL};

// These both for what?? Check
static char port_list[MAX_OPTLINE_LEN] = 	"";
static char port_stat_list[MAX_OPTLINE_LEN] = 	"";

/*----------------------------------------------------------------------------*/
void
PrintInterfaceInfo() 
{
	int i;
		
	/* print out process start information */
	TRACE_CONFIG("Interfaces:\n");
	for (i = 0; i < CONFIG.eths_num; i++) {
			
		uint8_t *da = (uint8_t *)&CONFIG.eths[i].ip_addr;
		uint8_t *nm = (uint8_t *)&CONFIG.eths[i].netmask;

		TRACE_CONFIG("name: %s, ifindex: %d, "
				"hwaddr: %02X:%02X:%02X:%02X:%02X:%02X, "
				"ipaddr: %u.%u.%u.%u, "
				"netmask: %u.%u.%u.%u\n",
				CONFIG.eths[i].dev_name, 
				CONFIG.eths[i].ifindex, 
				CONFIG.eths[i].haddr[0],
				CONFIG.eths[i].haddr[1],
				CONFIG.eths[i].haddr[2],
				CONFIG.eths[i].haddr[3],
				CONFIG.eths[i].haddr[4],
				CONFIG.eths[i].haddr[5],
				da[0], da[1], da[2], da[3],
				nm[0], nm[1], nm[2], nm[3]);
	}
	TRACE_CONFIG("Number of NIC queues: %d\n", num_queues);
	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}
/*----------------------------------------------------------------------------*/
int
SetRoutingTable() 
{
	int i, ridx;
	unsigned int c;

	CONFIG.routes = 0;
	CONFIG.rtable = (struct route_table *)
			calloc(MAX_ROUTE_ENTRY, sizeof(struct route_table));
	if (!CONFIG.rtable) 
		exit(EXIT_FAILURE);

	/* set default routing table */
	for (i = 0; i < CONFIG.eths_num; i ++) {
		
		ridx = CONFIG.routes++;
		CONFIG.rtable[ridx].daddr = CONFIG.eths[i].ip_addr & CONFIG.eths[i].netmask;
		
		CONFIG.rtable[ridx].prefix = 0;
		c = CONFIG.eths[i].netmask;
		while ((c = (c >> 1))){
			CONFIG.rtable[ridx].prefix++;
		}
		CONFIG.rtable[ridx].prefix++;
		
		CONFIG.rtable[ridx].mask = CONFIG.eths[i].netmask;
		CONFIG.rtable[ridx].masked = CONFIG.rtable[ridx].daddr;
		CONFIG.rtable[ridx].nif = CONFIG.eths[ridx].ifindex;
	}

	/* set additional routing table */
	SetRoutingTableFromFile();

	return 0;
}
/*----------------------------------------------------------------------------*/
void
PrintRoutingTable()
{
	int i;
	uint8_t *da;
	uint8_t *m;
	uint8_t *md;

	/* print out process start information */
	TRACE_CONFIG("Routes:\n");
	for (i = 0; i < CONFIG.routes; i++) {
		da = (uint8_t *)&CONFIG.rtable[i].daddr;
		m = (uint8_t *)&CONFIG.rtable[i].mask;
		md = (uint8_t *)&CONFIG.rtable[i].masked;
		TRACE_CONFIG("Destination: %u.%u.%u.%u/%d, Mask: %u.%u.%u.%u, "
				"Masked: %u.%u.%u.%u, Route: ifdx-%d\n", 
				da[0], da[1], da[2], da[3], CONFIG.rtable[i].prefix, 
				m[0], m[1], m[2], m[3], md[0], md[1], md[2], md[3], 
				CONFIG.rtable[i].nif);
	}
	if (CONFIG.routes == 0)
		TRACE_CONFIG("(blank)\n");

	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}
/*----------------------------------------------------------------------------*/
int 
LoadARPTable()
{
#define ARP_ENTRY "ARP_ENTRY"

	FILE *fc;
	char optstr[MAX_OPTLINE_LEN];
	int numEntry = 0;
	int hasNumEntry = 0;

	TRACE_CONFIG("Loading ARP table from : %s\n", arp_file);

	InitARPTable();

	fc = fopen(arp_file, "r");
	if (fc == NULL) {
		perror("fopen");
		TRACE_CONFIG("Skip loading static ARP table\n");
		return -1;
	}

	while (1) {
		char *p;
		char *temp;

		if (fgets(optstr, MAX_OPTLINE_LEN, fc) == NULL)
			break;

		p = optstr;

		// skip comment
		if ((temp = strchr(p, '#')) != NULL)
			*temp = 0;
		// remove front and tailing spaces
		while (*p && isspace((int)*p))
			p++;
		temp = p + strlen(p) - 1;
		while (temp >= p && isspace((int)*temp))
			   *temp = 0;
		if (*p == 0) /* nothing more to process? */
			continue;

		if (!hasNumEntry && strncmp(p, ARP_ENTRY, sizeof(ARP_ENTRY)-1) == 0) {
			numEntry = GetIntValue(p + sizeof(ARP_ENTRY));
			if (numEntry <= 0) {
				fprintf(stderr, "Wrong entry in arp.conf: %s\n", p);
				exit(EXIT_FAILURE);
			}
#if 0
			CONFIG.arp.entry = (struct arp_entry *)
				calloc(numEntry + MAX_ARPENTRY, sizeof(struct arp_entry));
			if (CONFIG.arp.entry == NULL) {
				fprintf(stderr, "Wrong entry in arp.conf: %s\n", p);
				exit(EXIT_FAILURE);
			}
#endif
			hasNumEntry = 1;
		} else {
			if (numEntry <= 0) {
				fprintf(stderr, 
						"Error in arp.conf: more entries than "
						"are specifed, entry=%s\n", p);
				exit(EXIT_FAILURE);
			}
			EnrollARPTableEntry(p);
			numEntry--;
		}
	}

	fclose(fc);
	return 0;
}
/*----------------------------------------------------------------------------*/
int 
LoadConfiguration(const char *fname)
{
	FILE *fp;
	char optstr[MAX_OPTLINE_LEN];

	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
	TRACE_CONFIG("Loading mtcp configuration from : %s\n", fname);

	fp = fopen(fname, "r");
	if (fp == NULL) {
		perror("fopen");
		TRACE_CONFIG("Failed to load configuration file: %s\n", fname);
		return -1;
	}

#ifndef DISABLE_DPDK
	mpz_init(CONFIG._cpumask);
#endif
	while (1) {
		char *p;
		char *temp;

		if (fgets(optstr, MAX_OPTLINE_LEN, fp) == NULL)
			break;

		p = optstr;

		// skip comment
		if ((temp = strchr(p, '#')) != NULL)
			*temp = 0;
		// remove front and tailing spaces
		while (*p && isspace((int)*p))
			p++;
		temp = p + strlen(p) - 1;
		while (temp >= p && isspace((int)*temp))
			   *temp = 0;
		if (*p == 0) /* nothing more to process? */
			continue;

		if (ParseConfiguration(p) < 0) {
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);

	/* if rcvbuf is set but sndbuf is not, sndbuf = rcvbuf */
	if (CONFIG.sndbuf_size == -1 && CONFIG.rcvbuf_size != -1)
		CONFIG.sndbuf_size = CONFIG.rcvbuf_size;
	/* if sndbuf is set but rcvbuf is not, rcvbuf = sndbuf */
	if (CONFIG.rcvbuf_size == -1 && CONFIG.sndbuf_size != -1)
		CONFIG.rcvbuf_size = CONFIG.sndbuf_size;
	/* if sndbuf & rcvbuf are not set, rcvbuf = sndbuf = 8192 */
	if (CONFIG.rcvbuf_size == -1 && CONFIG.sndbuf_size == -1)
		CONFIG.sndbuf_size = CONFIG.rcvbuf_size = 8192;
	
	return SetNetEnv(port_list, port_stat_list);
	
	return 0;
}
/*----------------------------------------------------------------------------*/
void 
PrintConfiguration()
{
	int i;

	TRACE_CONFIG("Configurations:\n");
	TRACE_CONFIG("Number of CPU cores available: %d\n", num_cpus);
	TRACE_CONFIG("Number of CPU cores to use: %d\n", CONFIG.num_cores);
	TRACE_CONFIG("Maximum number of concurrency per core: %d\n", 
			CONFIG.max_concurrency);
	if (CONFIG.multi_process == 1) {
		TRACE_CONFIG("Multi-process support is enabled\n");
		if (CONFIG.multi_process_is_master == 1)
			TRACE_CONFIG("Current core is master (for multi-process)\n");
		else
			TRACE_CONFIG("Current core is not master (for multi-process)\n");
	}
	TRACE_CONFIG("Maximum number of preallocated buffers per core: %d\n", 
			CONFIG.max_num_buffers);
	TRACE_CONFIG("Receive buffer size: %d\n", CONFIG.rcvbuf_size);
	TRACE_CONFIG("Send buffer size: %d\n", CONFIG.sndbuf_size);

    // TIME_TICK we will see later....
	if (CONFIG.tcp_timeout > 0) {
		TRACE_CONFIG("TCP timeout seconds: %d\n", 
				USEC_TO_SEC(CONFIG.tcp_timeout * TIME_TICK));
	} else {
		TRACE_CONFIG("TCP timeout check disabled.\n");
	}
	TRACE_CONFIG("TCP timewait seconds: %d\n", 
			USEC_TO_SEC(CONFIG.tcp_timewait * TIME_TICK));
	TRACE_CONFIG("NICs to print statistics:");
	for (i = 0; i < CONFIG.eths_num; i++) {
		if (CONFIG.eths[i].stat_print) {
			TRACE_CONFIG(" %s", CONFIG.eths[i].dev_name);
		}
	}
	TRACE_CONFIG("\n");
	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}