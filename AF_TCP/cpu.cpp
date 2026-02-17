#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <numa.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>

#define MAX_FILE_NAME 1024

inline int 
GetNumCPUs() 
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

pid_t 
Gettid()
{
	return syscall(__NR_gettid);
}

/**
 * find_irq_for_queue
 * @iface: The interface name (e.g., "enp1s0")
 * @queue_id: The index of the queue (0, 1, 2...)
 * * Returns the Linux IRQ number on success, -1 on failure.
 */
int find_irq_for_queue(const char *iface, int queue_id) {
    char path[PATH_MAX];
    struct dirent **namelist;
    int irq = -1;

    // Standard sysfs path for MSI-X interrupts of a network device
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/msi_irqs", iface);

    // Scan the directory for IRQ numbers
    int n = scandir(path, &namelist, NULL, alphasort);
    if (n < 0) {
        // Fallback: Some older drivers or virtual drivers use a different path
        // or might just have one 'irq' file if not using MSI-X
        snprintf(path, sizeof(path), "/sys/class/net/%s/device/irq", iface);
        FILE *fp = fopen(path, "r");
        if (fp) {
            if (fscanf(fp, "%d", &irq) != 1) irq = -1;
            fclose(fp);
            return irq;
        }
        perror("scandir msi_irqs");
        return -1;
    }

    // MSI-X directories usually contain files named after the IRQ number
    // We count 'n' files. Note: entry 0 and 1 are usually "." and ".."
    int found_count = 0;
    for (int i = 0; i < n; i++) {
        if (namelist[i]->d_name[0] != '.') {
            if (found_count == queue_id) {
                irq = atoi(namelist[i]->d_name);
            }
            found_count++;
        }
        free(namelist[i]);
    }
    free(namelist);

    if (irq == -1) {
        fprintf(stderr, "Queue ID %d not found for interface %s\n", queue_id, iface);
    }

    return irq;
}

// Helper to write the core ID to the IRQ's affinity file
int tie_queue_to_core(int irq, int core_id) {
    char path[256];
    char core_str[16];
    
    // Path: /proc/irq/[IRQ]/smp_affinity_list
    snprintf(path, sizeof(path), "/proc/irq/%d/smp_affinity_list", irq);
    snprintf(core_str, sizeof(core_str), "%d", core_id);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("Failed to tie queue (try running with sudo)");
        return -1;
    }

    fprintf(fp, "%s", core_str);
    fclose(fp);
    printf("Successfully tied IRQ %d to Core %s\n", irq, core_str);
    return 0;
}

int
aftcp_core_affinitize(int cpu){
    cpu_set_t cpus;
    size_t n;
    int ret;

    n = GetNumCPUs();

    // cpu = whichCoreID(cpu); -> Check later

    if (cpu < 0 || cpu >= (int) n) {
		errno = -EINVAL;
		return -1;
	}

    CPU_ZERO(&cpus);
	CPU_SET((unsigned)cpu, &cpus);

	struct bitmask *bmask;
	FILE *fp;
	char sysfname[MAX_FILE_NAME];
	int phy_id;
	
	ret = sched_setaffinity(Gettid(), sizeof(cpus), &cpus);

	if (numa_max_node() == 0)
		return ret;

	bmask = numa_bitmask_alloc(numa_max_node() + 1);
	assert(bmask);

	/* read physical id of the core from sys information */
	snprintf(sysfname, MAX_FILE_NAME - 1, 
			"/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
	fp = fopen(sysfname, "r");
	if (!fp) {
		perror(sysfname);
		errno = EFAULT;
		return -1;
	}
	ret = fscanf(fp, "%d", &phy_id);
	if (ret != 1) {
		fclose(fp);
		perror("Fail to read core id");
		errno = EFAULT;
		return -1;
	}

	numa_bitmask_setbit(bmask, phy_id);
	numa_set_membind(bmask);
	numa_bitmask_free(bmask);

	fclose(fp);
    
    return ret;
}

int aftcp_init_and_tie(const char *iface, int queue_id, int target_core) {
    // 1. Find the IRQ for the specific queue (from our previous logic)
    int irq = find_irq_for_queue(iface, queue_id); 
    if (irq < 0) return -1;

    // 2. TIE the queue to the core (Hardware side)
    if (tie_queue_to_core(irq, target_core) != 0) return -1;

    // 3. PIN this thread to the same core (Software side)
    // This is the function you wrote earlier!
    if (aftcp_core_affinitize(target_core) != 0) {
        return -1;
    }

    // 4. Proceed to UMEM allocation...
    return 0;
}