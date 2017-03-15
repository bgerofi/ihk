/* ihklib.h */
#ifndef INCLUDED_IHKLIB
#define INCLUDED_IHKLIB

#include <bfd.h>
#define PATH_SYS_NODE "/sys/devices/system/node"
#define PATH_DEV "/dev"
#define IHKCONFIG_CMD "/home/shirasawa/kncc19-smp-x86/freezer/mic/sbin/ihkconfig"
#define IHKOSCTL_CMD "/home/shirasawa/kncc19-smp-x86/freezer/mic/sbin/ihkosctl"

/* rusage sysfs path */
#define PATH_SYS_RSS	"/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.stat"
#define KEY_SYS_RSS "rss "
#define PATH_SYS_CACHE	"/sys/fs/cgroup/memory/memory.stat"
#define KEY_SYS_CACHE "cache "
#define PATH_SYS_RSS_HUGE "/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.stat"
#define KEY_SYS_RSS_HUGE "rss_huge "
#define PATH_SYS_MAPPED_FILE "/sys/fs/cgroup/memory/memory.stat"
#define KEY_SYS_MAPPED_FILE "mapped_file "
#define PATH_SYS_MAX_USAGE	"/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.max_usage_in_bytes"
#define PATH_SYS_KMEM_USAGE	"/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.kmem.usage_in_bytes"
#define PATH_SYS_KMAX_USAGE	"/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.kmem.max_usage_in_bytes"
#define PATH_SYS_NUM_NUMA_NODES	"/tmp/mcos/mcos%d_sys/fs/cgroup/cpu/num_numa_nodes.txt"
#define PATH_SYS_NUMA_STAT	"/tmp/mcos/mcos%d_sys/fs/cgroup/memory/memory.numa_stat"
#define KEY_SYS_NUMA_STAT	"total="
#define PATH_SYS_HUGETLB	"/tmp/mcos/mcos%d_sys/fs/cgroup/hugetlb/hugetlb.1GB.usage_in_bytes"
#define PATH_SYS_HUGETLB_MAX	"/tmp/mcos/mcos%d_sys/fs/cgroup/hugetlb/hugetlb.1GB.max_usage_in_bytes"
#define PATH_SYS_STAT_SYSTEM	"/tmp/mcos/mcos%d_sys/fs/cgroup/cpuacct/cpuacct.stat"
#define KEY_SYS_STAT_SYSTEM	"system "
#define PATH_SYS_STAT_USER	"/tmp/mcos/mcos%d_sys/fs/cgroup/cpuacct/cpuacct.stat"
#define KEY_SYS_STAT_USER	"user "
#define PATH_SYS_USAGE	"/tmp/mcos/mcos%d_sys/fs/cgroup/cpuacct/cpuacct.usage"
#define PATH_SYS_USAGE_PER_CPU	"/tmp/mcos/mcos%d_sys/fs/cgroup/cpuacct/cpuacct.usage_percpu" /* comma separeted */
#define PATH_SYS_NUM_THREADS	"/tmp/mcos/mcos%d_sys/fs/cgroup/num_threads"
#define PATH_SYS_MAX_NUM_THREADS	"/tmp/mcos/mcos%d_sys/fs/cgroup/max_num_threads"

#define PATH_SYS_OS_STATUS	"/tmp/mcos/mcos%d_sys/fs/cgroup/mck_status"

#include <ihk/affinity.h> 

typedef struct {
	unsigned long size;
	int numa_node_number;
} ihk_mem_chunk;

typedef struct {
	ihk_mem_chunk *reserved_mem_chunks;
	int num_reserved_mem_chunks;
	cpu_set_t reserved_cpu_set;
	ihk_mem_chunk *assigned_mem_chunks;
	int num_assigned_mem_chunks;
	cpu_set_t assigned_cpu_set;
	int num_os;
} ihk_info ;

enum ihk_command_type {
	IHK_CONFIG_CREATE, 
	IHK_CONFIG_DESTROY,
	IHK_CONFIG_RESERVE,
	IHK_CONFIG_RELEASE,
	IHK_CONFIG_QUERY,
	IHK_CONFIG_RESERVE_NUMA,
	IHK_CONFIG_RELEASE_NUMA
};

enum ihk_osctl_command_type {
	IHK_OSCTL_LOAD,
	IHK_OSCTL_BOOT,
	IHK_OSCTL_SHUTDOWN,
	IHK_OSCTL_ASSIGN,
	IHK_OSCTL_RELEASE,
	IHK_OSCTL_QUERY,
	IHK_OSCTL_QUERY_FREE_MEM,
	IHK_OSCTL_KARGS,
	IHK_OSCTL_KMSG,
	IHK_OSCTL_CLEAR_KMSG,
	IHK_OSCTL_ASSIGN_NUMA,
	IHK_OSCTL_RELEASE_NUMA
};

enum ihk_resource_type {
	IHK_RESOURCE_CPU,
	IHK_RESOURCE_MEM
};

enum ihk_os_status {
	IHK_STATUS_INACTIVE,
	IHK_STATUS_BOOTING,
	IHK_STATUS_RUNNING,
	IHK_STATUS_SHUTDOWN,
	IHK_STATUS_PANIC,
	IHK_STATUS_HUNGUP,
	IHK_STATUS_FREEZING,
	IHK_STATUS_FROZEN,
};

typedef struct {
	enum ihk_resource_type resource_type;
	cpu_set_t cpu_set;
	ihk_mem_chunk *mem_chunks;
	int num_mem_chunks;
	int os_index;
	int numa_node;
} ihkconfig;

typedef struct {
	enum ihk_resource_type resource_type;
	cpu_set_t cpu_set;
	ihk_mem_chunk *mem_chunks;
	int num_mem_chunks;
	int os_index;
	char* image;
	char* kargs;
	char* kmsg;
	size_t kmsg_size;
	int numa_node;
} ihkosctl;

typedef struct {
	ihk_mem_chunk *mem_chunks;
	int num_mem_chunks;
	cpu_set_t mask;
	enum ihk_os_status status;
} ihk_osinfo;

typedef struct {
	unsigned long rss;
	unsigned long cache;
	unsigned long rss_huge;
	unsigned long mapped_file;
	unsigned long max_usage;
	unsigned long kmem_usage;
	unsigned long kmax_usage;
	int num_numa_nodes;
	unsigned long* numa_stat;
	unsigned long hugetlb;
	unsigned long hugetlb_max;
	unsigned long stat_system;
	unsigned long stat_user;
	unsigned long usage;
	unsigned long usage_per_cpu[sizeof(cpu_set_t)/8];
	int num_threads;
	int max_num_threads;
} ihk_rusage;

typedef struct {
	unsigned long config; 
	unsigned disabled:1;
	unsigned pinned:1;
	unsigned exclude_user:1;
	unsigned exclude_kernel:1;
	unsigned exclude_hv:1;
	unsigned exclude_idle:1;
} ihk_perf_event_attr;

struct ihk_bfd_func {
	void (*bfd_init)(void);
	bfd *(*bfd_fopen)(const char *filename, const char *target,
	                 const char *mode, int fd);
	void (*bfd_perror)(const char *message);
	bfd_boolean (*bfd_set_format)(bfd *abfd, bfd_format format);
	asection *(*bfd_make_section_anyway)(bfd *abfd, const char *name);
	bfd_boolean (*bfd_set_section_size)(bfd *abfd, asection *sec,
	                                    bfd_size_type val);
	bfd_boolean (*bfd_set_section_flags)(bfd *abfd, asection *sec,
	                                     flagword flags);
	asection *(*bfd_get_section_by_name)(bfd *abfd, const char *name);
	bfd_boolean (*bfd_set_section_contents)(bfd *abfd, asection *section,
                                                const void *data,
	                                        file_ptr offset,
                                                bfd_size_type count);
	bfd_boolean (*bfd_close)(bfd *abfd);
};

#define ihk_makedumpfile(a, b, c, d) ({					\
struct ihk_bfd_func __ihk_bfd_func = {					\
.bfd_init = bfd_init,							\
.bfd_fopen = bfd_fopen,							\
.bfd_perror = bfd_perror,						\
.bfd_set_format = bfd_set_format,					\
.bfd_make_section_anyway = bfd_make_section_anyway,			\
.bfd_set_section_size = bfd_set_section_size,				\
.bfd_set_section_flags = bfd_set_section_flags,				\
.bfd_get_section_by_name = bfd_get_section_by_name,			\
.bfd_set_section_contents = bfd_set_section_contents,			\
.bfd_close = bfd_close,							\
};									\
__ihk_makedumpfile((a), (b), (c), (d), &__ihk_bfd_func);			\
})

int ihk_getoslist(int oslist[], int n);

int ihk_geteventfd(int index, int type);

int ihk_getihk_info (ihk_info *info);

int __ihk_makedumpfile(int index, char *dumpfile, int opt, char *vmfile, struct ihk_bfd_func *);

int ihk_config(int mcd, int comm, ihkconfig *config);
int ihk_osctl(int index, int comm, ihkosctl *ctl);

int ihk_getrusage (int index, ihk_rusage *rusage);
int ihk_getosinfo (int index, ihk_osinfo *osinfo);

int ihk_setperfevent(int index, ihk_perf_event_attr attr[], int n);
int ihk_perfctl(int index, int comm);
int ihk_getperfevent(int index, unsigned long *counter, int n);

#endif

