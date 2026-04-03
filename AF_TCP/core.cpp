#include "config.hpp"
#include "cpu.hpp"
#include "aftcp_api.hpp"
#include "aftcp.hpp"
#include "addr_pool.hpp"
#include "debug.hpp"

static int mtcp_master = -1; // First mtcp thread to initialize
// Handles all the global operations

mctx_t 
mtcp_create_context(int cpu)
{
	mctx_t mctx;
	int ret;

	if (cpu >=  CONFIG.num_cores) {
		TRACE_ERROR("Failed initialize new mtcp context. "
			    "Requested cpu id %d exceed the number of cores %d configured to use.\n",
			    cpu, CONFIG.num_cores);
		return NULL;
	}

        /* check if mtcp_create_context() was already initialized */
        if (g_logctx[cpu] != NULL) {
                TRACE_ERROR("%s was already initialized before!\n",
                            __FUNCTION__);
                return NULL;
        }

	ret = sem_init(&g_init_sem[cpu], 0, 0);
	if (ret) {
		TRACE_ERROR("Failed initialize init_sem.\n");
		return NULL;
	}

	mctx = (mctx_t)calloc(1, sizeof(struct mtcp_context));
	if (!mctx) {
		TRACE_ERROR("Failed to allocate memory for mtcp_context.\n");
		return NULL;
	}
	mctx->cpu = cpu;

	/* initialize logger */
	g_logctx[cpu] = (struct log_thread_context *)
			calloc(1, sizeof(struct log_thread_context));
	if (!g_logctx[cpu]) {
		perror("calloc");
		TRACE_ERROR("Failed to allocate memory for log thread context.\n");
		free(mctx);
		return NULL;
	}
	InitLogThreadContext(g_logctx[cpu], cpu);
#if defined (PKTDUMP) || (DBGMSG) || (DBGFUNC) || \
	(STREAM) || (STATE) || (STAT) || (APP) || \
	(EPOLL) || (DUMP_STREAM)
	if (pthread_create(&log_thread[cpu], 
				NULL, ThreadLogMain, (void *)g_logctx[cpu])) {
		perror("pthread_create");
		TRACE_ERROR("Failed to create log thread\n");
		free(g_logctx[cpu]);
		free(mctx);
		return NULL;
	}
#endif
// #ifndef DISABLE_DPDK
// 	/* Wake up mTCP threads (wake up I/O threads) */
// 	if (current_iomodule_func == &dpdk_module_func) {
// 		int master;
// 		master = rte_get_master_lcore();
		
// 		if (master == whichCoreID(cpu)) {
// 			lcore_config[master].ret = 0;
// 			lcore_config[master].state = FINISHED;
			
// 			if (pthread_create(&g_thread[cpu], 
// 					   NULL, MTCPRunThread, (void *)mctx) != 0) {
// 				TRACE_ERROR("pthread_create of mtcp thread failed!\n");
// 				return NULL;
// 			}
// 		} else
// 			rte_eal_remote_launch(MTCPDPDKRunThread, mctx, whichCoreID(cpu));
// 	} else
// #endif
		{
			if (pthread_create(&g_thread[cpu], 
					   NULL, MTCPRunThread, (void *)mctx) != 0) {
				TRACE_ERROR("pthread_create of mtcp thread failed!\n");
				return NULL;
			}
		}

	sem_wait(&g_init_sem[cpu]);
	sem_destroy(&g_init_sem[cpu]);

	running[cpu] = TRUE;

	if (mtcp_master < 0) {
		mtcp_master = cpu;
		TRACE_INFO("CPU %d is now the master thread.\n", mtcp_master);
	}

	return mctx;
}

int 
aftcp_getconf(struct aftcp_config *conf)
{
	if (!conf)
		return -1;

// CONFIG IS WHAT ???

	conf->num_cores = CONFIG.num_cores;
	conf->max_concurrency = CONFIG.max_concurrency;

	conf->max_num_buffers = CONFIG.max_num_buffers;
	conf->rcvbuf_size = CONFIG.rcvbuf_size;
	conf->sndbuf_size = CONFIG.sndbuf_size;

	conf->tcp_timewait = CONFIG.tcp_timewait;
	conf->tcp_timeout = CONFIG.tcp_timeout;

	return 0;
}

int 
aftcp_init(const char *config_file)
{
	int i;
	int ret;

	/* getting cpu and NIC */
	/* set to max cpus only if user has not arbitrarily set it to lower # */
	num_cpus = (CONFIG.num_cores == 0) ? GetNumCPUs() : CONFIG.num_cores;
			
	assert(num_cpus >= 1);

	if (num_cpus > MAX_CPUS) {
		TRACE_ERROR("You cannot run mTCP with more than %d cores due "
			    "to your static mTCP configuration. Please disable "
			    "the last %d cores in your system.\n",
			    MAX_CPUS, num_cpus - MAX_CPUS);
		exit(EXIT_FAILURE);
	}

#if 0
	/* TODO: Enable this macro if cross-machine comm. with onvm client/server fails */
	if (num_cpus > 1) {
		TRACE_ERROR("You cannot run mTCP application with more than 1 "
			    "core when you are using ONVM driver\n");
		exit(EXIT_FAILURE);
	}
#endif

	for (i = 0; i < num_cpus; i++) {
		g_mtcp[i] = NULL;
		running[i] = FALSE;
		sigint_cnt[i] = 0;
	}

	ret = LoadConfiguration(config_file); // We have to do this in config.cpp
	if (ret) {
		TRACE_CONFIG("Error occured while loading configuration.\n");
		return -1;
	}
	PrintConfiguration(); // We have to do this in config.cpp

	for (i = 0; i < CONFIG.eths_num; i++) {
		ap[i] = CreateAddressPool(CONFIG.eths[i].ip_addr, 1); // We have to do this in addr_pool.cpp
		if (!ap[i]) {
			TRACE_CONFIG("Error occured while create address pool[%d]\n",
				     i);
			return -1;
		}
	}
	
	PrintInterfaceInfo(); // config.cpp

	ret = SetRoutingTable(); // config.cpp
	if (ret) {
		TRACE_CONFIG("Error occured while loading routing table.\n");
		return -1;
	}
	PrintRoutingTable(); // config.cpp

	LoadARPTable(); // config.cpp
	PrintARPTable(); // arp.cpp

	// signal in signal.hpp (third-party check if needed)
	if (signal(SIGUSR1, HandleSignal) == SIG_ERR) {
		perror("signal, SIGUSR1");
		return -1;
	}
	if (signal(SIGINT, HandleSignal) == SIG_ERR) {
		perror("signal, SIGINT");
		return -1;
	}
	app_signal_handler = NULL; // Please check what this is for?? Declared on top of core.cpp

	/* load system-wide io module specs */
	current_iomodule_func->load_module(); // In io_module.cpp

	return 0;
}