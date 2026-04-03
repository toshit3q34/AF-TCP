#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <iostream>
#include <mutex>
#include <memory>

#include "addr_pool.h"
#include "rss.h"
#include "debug.h"

/*----------------------------------------------------------------------------*/
struct addr_entry {
    struct sockaddr_in addr;          // holds one IP + port pair
    TAILQ_ENTRY(addr_entry) addr_link; // BSD-style doubly-linked tail queue
};
/*----------------------------------------------------------------------------*/
struct addr_map {
    struct addr_entry *addrmap[MAX_PORT]; // index: port → addr_entry pointer
};
/*----------------------------------------------------------------------------*/
struct addr_pool {
    std::unique_ptr<struct addr_entry[]> pool;    // flat contiguous array of ALL entries
    std::unique_ptr<struct addr_map[]> mapper;    // per-IP reverse lookup maps
    uint32_t addr_base;         // starting IP (host byte order)
    int num_addr;               // how many IPs are in the pool
    int num_entry;              // total (IPs × ports) slots
    int num_free;               // free slot count
    int num_used;               // used slot count
    std::mutex lock;       // protects concurrent allocation
    TAILQ_HEAD(, addr_entry) free_list; // available entries
    TAILQ_HEAD(, addr_entry) used_list; // currently in-use entries
};
/*----------------------------------------------------------------------------*/


// YOU CAN CHANGE addr_pool_t to std::unique_ptr<struct addr_pool>
addr_pool_t 
CreateAddressPool(in_addr_t addr_base, int num_addr)
{
	struct addr_pool *ap;
	int num_entry;
	int i, j, cnt;
	in_addr_t addr;
	uint32_t addr_h;

	ap = new struct addr_pool();

	/* initialize address pool */
	num_entry = num_addr * (MAX_PORT - MIN_PORT);
	ap->pool   = std::make_unique<struct addr_entry[]>(num_entry);
    ap->mapper = std::make_unique<struct addr_map[]>(num_addr);

	TAILQ_INIT(&ap->free_list);
	TAILQ_INIT(&ap->used_list);

	std::lock_guard<std::mutex> lk(ap->lock);

	ap->addr_base = ntohl(addr_base);
	ap->num_addr = num_addr;

	cnt = 0;
	for (i = 0; i < num_addr; i++) {
		addr_h = ap->addr_base + i;
		addr = htonl(addr_h);
		for (j = MIN_PORT; j < MAX_PORT; j++) {
			ap->pool[cnt].addr.sin_addr.s_addr = addr;
			ap->pool[cnt].addr.sin_port = htons(j);
			ap->mapper[i].addrmap[j] = &ap->pool[cnt];
			
			TAILQ_INSERT_TAIL(&ap->free_list, &ap->pool[cnt], addr_link);

			if ((++cnt) >= num_entry)
				break;
		}
	}
	ap->num_entry = cnt;
	ap->num_free = cnt;
	ap->num_used = 0;

	return ap;
}