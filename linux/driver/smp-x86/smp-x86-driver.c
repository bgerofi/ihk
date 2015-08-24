/**
 * \file smp-x86-driver.c
 * \brief
 *	IHK SMP-x86 Driver: IHK Host Driver 
 *                        for partitioning an x86 SMP chip
 * \author Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2014 Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp>
 * 
 * Code partially based on IHK Builtin driver written by 
 * Taku SHIMOSAWA <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <config.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/file.h>
#include <linux/elf.h>
#include <linux/cpu.h>
#include <linux/radix-tree.h>
#include <linux/irq.h>
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)
#include <linux/autoconf.h>
#endif
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
//#define IHK_DEBUG
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
//#include <linux/shimos.h>
//#include "builtin_dma.h"
#include <asm/ipi.h>
#include <asm/uv/uv.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/mc146818rtc.h>
#include <asm/smpboot_hooks.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#include <asm/trampoline.h>
#else
#include <asm/realmode.h>
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0) */
#include "bootparam.h"

#define BUILTIN_OS_STATUS_INITIAL  0
#define BUILTIN_OS_STATUS_LOADING  1
#define BUILTIN_OS_STATUS_LOADED   2
#define BUILTIN_OS_STATUS_BOOTING  3

//#define BUILTIN_MAX_CPUS SHIMOS_MAX_CORES

#define BUILTIN_COM_VECTOR  0xf1

#define LARGE_PAGE_SIZE	(1UL << 21)
#define LARGE_PAGE_MASK	(~((unsigned long)LARGE_PAGE_SIZE - 1))

#define MAP_ST_START	0xffff800000000000UL
#define MAP_KERNEL_START	0xffffffff80000000UL

#define PTL4_SHIFT	39
#define PTL3_SHIFT	30
#define PTL2_SHIFT	21


/*
 * IHK-SMP unexported kernel symbols
 */

/* real_mode_header has been introduced in 3.10 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#ifdef IHK_KSYM_real_mode_header
#if IHK_KSYM_real_mode_header
struct real_mode_header *real_mode_header = 
	(void *)
	IHK_KSYM_real_mode_header;
#endif
#endif
#endif

#ifdef IHK_KSYM_per_cpu__vector_irq
#if IHK_KSYM_per_cpu__vector_irq
void *_per_cpu__vector_irq = 
	(void *)
	IHK_KSYM_per_cpu__vector_irq;
#endif
#endif

#ifdef IHK_KSYM_vector_irq
#if IHK_KSYM_vector_irq
void *_vector_irq = 
	(void *)
	IHK_KSYM_vector_irq;
#endif
#endif

#ifdef IHK_KSYM_lapic_get_maxlvt
#if IHK_KSYM_lapic_get_maxlvt
typedef int (*int_star_fn_void_t)(void); 
int (*_lapic_get_maxlvt)(void) = 
	(int_star_fn_void_t)
	IHK_KSYM_lapic_get_maxlvt;
#endif
#endif

#ifdef IHK_KSYM_init_deasserted
#if IHK_KSYM_init_deasserted
atomic_t *_init_deasserted = 
	(atomic_t *)
	IHK_KSYM_init_deasserted;
#endif
#endif

#ifdef IHK_KSYM_irq_to_desc
#if IHK_KSYM_irq_to_desc
typedef struct irq_desc *(*irq_desc_star_fn_int_t)(unsigned int);
struct irq_desc *(*_irq_to_desc)(unsigned int irq) = 
	(irq_desc_star_fn_int_t)
	IHK_KSYM_irq_to_desc;
#else // exported
#include <linux/irqnr.h>
struct irq_desc *(*_irq_to_desc)(unsigned int irq) = irq_to_desc;
#endif
#endif

#ifdef IHK_KSYM_irq_to_desc_alloc_node
#if IHK_KSYM_irq_to_desc_alloc_node
typedef struct irq_desc *(*irq_desc_star_fn_int_int_t)(unsigned int, int);
struct irq_desc *(*_irq_to_desc_alloc_node)(unsigned int irq, int node) =
	(irq_desc_star_fn_int_int_t)
	IHK_KSYM_irq_to_desc_alloc_node;
#endif
#endif

#ifdef IHK_KSYM_alloc_desc
#if IHK_KSYM_alloc_desc
typedef struct irq_desc *(*irq_desc_star_fn_int_int_module_star_t)
	(int, int, struct module*);
struct irq_desc *(*_alloc_desc)(int irq, int node, struct module *owner) =
	(irq_desc_star_fn_int_int_module_star_t)
	IHK_KSYM_alloc_desc;
#endif
#endif

#ifdef IHK_KSYM_irq_desc_tree
#if IHK_KSYM_irq_desc_tree
struct radix_tree_root *_irq_desc_tree = 
	(struct radix_tree_root *)
	IHK_KSYM_irq_desc_tree;
#endif
#endif

#ifdef IHK_KSYM_dummy_irq_chip
#if IHK_KSYM_dummy_irq_chip
struct irq_chip *_dummy_irq_chip =
	(struct irq_chip *)
	IHK_KSYM_dummy_irq_chip;
#else // exported
struct irq_chip *_dummy_irq_chip = &dummy_irq_chip;
#endif
#endif

#ifdef IHK_KSYM_get_uv_system_type
#if IHK_KSYM_get_uv_system_type
typedef enum uv_system_type (*uv_system_type_star_fn_void_t)(void); 
enum uv_system_type (*_get_uv_system_type)(void) = 
	(uv_system_type_star_fn_void_t)
	IHK_KSYM_get_uv_system_type;
#endif
#else // static
#define _get_uv_system_type get_uv_system_type
#endif

#ifdef IHK_KSYM_wakeup_secondary_cpu_via_init
#if IHK_KSYM_wakeup_secondary_cpu_via_init
int (*_wakeup_secondary_cpu_via_init)(int phys_apicid, 
	unsigned long start_eip) = 
	IHK_KSYM_wakeup_secondary_cpu_via_init;
#endif
#endif

#ifdef IHK_KSYM__cpu_up
#if IHK_KSYM__cpu_up
typedef int (*int_star_fn_uint_int_t)(unsigned int, int);
int (*ihk_cpu_up)(unsigned int cpu, int tasks_frozen) =
	(int_star_fn_uint_int_t)
	IHK_KSYM__cpu_up;
#else // exported
int (*ihk_cpu_up)(unsigned int cpu, int tasks_frozen) = _cpu_up;
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#ifdef IHK_KSYM_cpu_hotplug_driver_lock 
#if IHK_KSYM_cpu_hotplug_driver_lock 
typedef void (*void_fn_void_t)(void);
void (*_cpu_hotplug_driver_lock)(void) = 
	(void_fn_void_t)
	IHK_KSYM_cpu_hotplug_driver_lock;
#else // exported
#include <linux/cpu.h>
void (*_cpu_hotplug_driver_lock)(void) = 
	cpu_hotplug_driver_lock;
#endif
#else // static
#include <linux/cpu.h>
void (*_cpu_hotplug_driver_lock)(void) = 
	cpu_hotplug_driver_lock;
#endif	

#ifdef IHK_KSYM_cpu_hotplug_driver_unlock
#if IHK_KSYM_cpu_hotplug_driver_unlock
#ifndef void_fn_void_t
typedef void (*void_fn_void_t)(void);
#endif
void (*_cpu_hotplug_driver_unlock)(void) = 
	(void_fn_void_t)
	IHK_KSYM_cpu_hotplug_driver_unlock;
#else // exported
void (*_cpu_hotplug_driver_unlock)(void) = 
	cpu_hotplug_driver_unlock;
#endif
#else // static
void (*_cpu_hotplug_driver_unlock)(void) = 
	cpu_hotplug_driver_unlock;
#endif
#endif

static unsigned long ihk_phys_start = 0;
module_param(ihk_phys_start, ulong, 0644);
MODULE_PARM_DESC(ihk_phys_start, "IHK reserved physical memory start address");

static unsigned long ihk_mem = 0;
module_param(ihk_mem, ulong, 0644);
MODULE_PARM_DESC(ihk_mem, "IHK reserved memory in MBs");

static unsigned int ihk_cores = 0;
module_param(ihk_cores, uint, 0644);
MODULE_PARM_DESC(ihk_cores, "IHK reserved CPU cores");

static unsigned int ihk_start_irq = 0;
module_param(ihk_start_irq, uint, 0644);
MODULE_PARM_DESC(ihk_start_irq, "IHK IKC IPI to be scanned from this IRQ vector");

static unsigned int ihk_ikc_irq_core = 0;
module_param(ihk_ikc_irq_core, uint, 0644);
MODULE_PARM_DESC(ihk_ikc_irq_core, "Target CPU of IHK IKC IRQ");

static unsigned long ihk_trampoline = 0;
module_param(ihk_trampoline, ulong, 0644);
MODULE_PARM_DESC(ihk_trampoline, "IHK trampoline page physical address");

#define IHK_SMP_MAXCPUS	256

#define BUILTIN_MAX_CPUS IHK_SMP_MAXCPUS

#define IHK_SMP_CPU_ONLINE		0
#define IHK_SMP_CPU_AVAILABLE	1
#define IHK_SMP_CPU_ASSIGNED	2
#define IHK_SMP_CPU_TO_OFFLINE	3
#define IHK_SMP_CPU_OFFLINED	4
#define IHK_SMP_CPU_TO_ONLINE	5

struct ihk_smp_cpu {
	int id;
	int apic_id;
	int status;
	ihk_os_t os;
};

static struct ihk_smp_cpu ihk_smp_cpus[IHK_SMP_MAXCPUS];
struct page *trampoline_page;
unsigned long trampoline_phys;
int using_linux_trampoline = 0;
char linux_trampoline_backup[4096];
void *trampoline_va;

unsigned long ident_page_table;
int ident_npages_order = 0;
unsigned long *ident_page_table_virt;

int ihk_smp_irq = 0;
int ihk_smp_irq_apicid = 0;

int ihk_smp_reset_cpu(int phys_apicid);

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE \
	roundup(ihk_smp_trampoline_end - ihk_smp_trampoline_data, PAGE_SIZE)

/* ----------------------------------------------- */

/** \brief BUILTIN boot parameter structure
 *
 * This structure contains vairous parameters both passed to the manycore 
 * kernel, and passed from the manycore kernel.
 */
struct builtin_boot_param {
	/** \brief SHIMOS-specific boot parameters. Memory start, end etc.
	 * (passed to the manycore) */
	struct shimos_boot_param bp;

	/** \brief Manycore-physical address of the kernel message buffer
	 * of the manycore kernel (filled by the manycore) */
	unsigned long msg_buffer;
	/** \brief Manycore physical address of the receive queue of 
	 * the master IKC channel (filled by the manycore) */
	unsigned long mikc_queue_recv;
	/** \brief Manycore physical address of the send queue of 
	 * the master IKC channel (filled by the manycore) */
	unsigned long mikc_queue_send;

	/** \brief Host physical address of the DMA structure
	 * (passed to the manycore) */
	unsigned long dma_address;
	/** \brief Host physical address of the identity-mapped page table
	 * (passed to the manycore) */
	unsigned long ident_table;

	unsigned long ns_per_tsc;
	unsigned long boot_sec;
	unsigned long boot_nsec;

	/** \brief Kernel command-line parameter */
	char kernel_args[256];
};

/** \brief BUILTIN driver-specific OS structure */
struct builtin_os_data {
	/** \brief Lock for this structure */
	spinlock_t lock;

	/** \brief Pointer to the device structure */
	struct builtin_device_data *dev;
	/** \brief Allocated CPU core mask */
	shimos_coreset coremaps;
	/** \brief Start address of the allocated memory region */
	unsigned long mem_start;
	/** \brief End address of the allocated memory region */
	unsigned long mem_end;

	/** \brief APIC ID of the bsp of this OS instance */
	int boot_cpu;
	/** \brief Entry point address of this OS instance */
	unsigned long boot_rip;

	/** \brief IHK Memory information */
	struct ihk_mem_info mem_info;
	/** \brief IHK Memory region information */
	struct ihk_mem_region mem_region;
	/** \brief IHK CPU information */
	struct ihk_cpu_info cpu_info;
	/** \brief APIC ID map of the CPU cores */
	int cpu_hw_ids[BUILTIN_MAX_CPUS];

	/** \brief Kernel command-line parameter.
	 *
	 * This will be copied to boot_param just before booting so that
	 * it does not change while the kernel is running.
	 */
	char kernel_args[256];

	/** \brief Boot parameter for the kernel
	 *
	 * This structure is directly accessed (read and written)
	 * by the manycore kernel. */
	struct builtin_boot_param param;

	/** \brief Status of the kernel */
	int status;
};

#define BUILTIN_DEV_STATUS_READY    0
#define BUILTIN_DEV_STATUS_BOOTING  1

/** \brief Driver-speicific device structure
 *
 * This structure is very simple because it is assumed that there is only
 * one BUILTIN device (because it uses the host machine actually!) in a machine.
 */
struct builtin_device_data {
	spinlock_t lock;
	ihk_device_t ihk_dev;
	int status;

	struct ihk_dma_channel builtin_host_channel;
};

/* Chunk denotes a memory range that is pre-reserved by IHK-SMP.
 * NOTE: chunk structures reside at the beginning of the physical memory
 * they represent!! */
struct chunk {
	struct list_head chain;
	uintptr_t addr;
	size_t size;
	int numa_id;
};

/* ihk_os_mem_chunk represents a memory range which is used by 
 * one of the OSs */
struct ihk_os_mem_chunk {
	struct list_head list;
	uintptr_t addr;
	size_t size;
	ihk_os_t os;
	int numa_id;
};

static struct list_head ihk_mem_free_chunks;
static struct list_head ihk_mem_used_chunks;

void *ihk_smp_map_virtual(unsigned long phys, unsigned long size)
{
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;

	/* look up address among used chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (phys >= os_mem_chunk->addr && 
				(phys + size) <= (os_mem_chunk->addr + 
					os_mem_chunk->size)) {

			return (phys_to_virt(os_mem_chunk->addr) 
					+ (phys - os_mem_chunk->addr));
		}
	}

	return 0;
}

void ihk_smp_unmap_virtual(void *virt)
{
	/* TODO: look up chunks and report error if not in range */
	return;	
}

/** \brief Implementation of ihk_host_get_dma_channel.
 *
 * It returns the information of the only channel in the DMA emulating core. */
static ihk_dma_channel_t smp_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                                 int channel)
{
	return NULL;
}

/** \brief Set the status member of the OS data with lock */
static void set_os_status(struct builtin_os_data *os, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	os->status = status;
	spin_unlock_irqrestore(&os->lock, flags);
}

/** \brief Set the status member of the OS data with lock */
static void set_dev_status(struct builtin_device_data *dev, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->status = status;
	spin_unlock_irqrestore(&dev->lock, flags);
}

/** \brief Create various information structure that should be provided
 * via IHK functions. */
static void __build_os_info(struct builtin_os_data *os)
{
	int i, c;

	os->mem_info.n_mappable = os->mem_info.n_available = 1;
	os->mem_info.n_fixed = 0;
	os->mem_info.available = os->mem_info.mappable = &os->mem_region;
	os->mem_info.fixed = NULL;
	os->mem_region.start = os->mem_start;
	os->mem_region.size = os->mem_end - os->mem_start;
	
	for (i = 0, c = 0; i < BUILTIN_MAX_CPUS; i++) {
		if (CORE_ISSET(i, os->coremaps)) {
			os->cpu_hw_ids[c] = i;
			c++;
		}
	}
	os->cpu_info.n_cpus = c;
	os->cpu_info.hw_ids = os->cpu_hw_ids;
}

struct ihk_smp_trampoline_header {
	unsigned long reserved;   /* jmp ins. */
	unsigned long page_table; /* ident page table */
	unsigned long next_ip;    /* the program address */
	unsigned long stack_ptr;  /* stack pointer */
	unsigned long notify_address; /* notification address */
};

static int smp_wakeup_secondary_cpu_via_init(int phys_apicid, 
		unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt, num_starts, j;
	unsigned long flags;

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	local_irq_save(flags);
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
		       phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();
	local_irq_restore(flags);

	mb();
	atomic_set(_init_deasserted, 1);

	/*
	 * Should we send STARTUP IPIs ?
	 *
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't send the STARTUP IPIs.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid]))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */
	pr_debug("#startup loops: %d.\n", num_starts);

	for (j = 1; j <= num_starts; j++) {
		pr_debug("Sending STARTUP #%d.\n", j);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		pr_debug("After apic_write.\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		/* Boot on the stack */
		/* Kick the second */
		local_irq_save(flags);
		apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12),
			       phys_apicid);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		pr_debug("Startup point 1.\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();
		local_irq_restore(flags);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	pr_debug("After Startup.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

int smp_wakeup_secondary_cpu(int apicid, unsigned long start_eip)
{
	
	if (_get_uv_system_type() != UV_NON_UNIQUE_APIC) {

		pr_debug("Setting warm reset code and vector.\n");

		smpboot_setup_warm_reset_vector(start_eip);
		/*
		 * Be paranoid about clearing APIC errors.
		 */
		if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
			apic_write(APIC_ESR, 0);
			apic_read(APIC_ESR);
		}
	}

	if (apic->wakeup_secondary_cpu) {
		return apic->wakeup_secondary_cpu(apicid, start_eip);
	}
	else {
		return smp_wakeup_secondary_cpu_via_init(apicid, start_eip);
	}
}

static unsigned long __rdtsc(void)
{
	unsigned int high, low;
	asm volatile("rdtsc" : "=a" (low), "=d" (high));
	return (unsigned long)high << 32 | low;
}


static unsigned long nanosecs_per_1000tsc(void)
{
#define TSC_NANO_LOOP	50000
	unsigned long cputs1, cputs2;
	struct timespec ts1, ts2;
	struct timespec now;
	unsigned long rdtsc_ts;
	unsigned long getnstimeofday_ts;
	unsigned long nanosecs;
	int i;
	unsigned long flags;
	uint64_t op;
	uint64_t eax, ebx, ecx, edx;

	/* Check if Invariant TSC is supported.
	 * Processor’s support for invariant TSC is indicated by
	 * CPUID.80000007H:EDX[8].
	 * See page 2498 of the Intel64 and IA-32 Architectures Software
	 * Developer’s Manual - combined */

	op = 0x80000007;
	asm volatile("cpuid" : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a" (op));

	if (!(edx & (1 << 8))) {
		return 0;
	}

	printk("IHK-SMP: invariant TSC supported: measuring nanosec/TSC ratio.\n");

	local_irq_save(flags);
	preempt_disable();

	/* Figure out how long rdtsc takes */
	cputs1 = __rdtsc();
	for (i = 0; i < TSC_NANO_LOOP; ++i) {
		__rdtsc();
	}
	cputs2 = __rdtsc();
	rdtsc_ts = (cputs2 - cputs1) / (TSC_NANO_LOOP + 1);

	/* Figure out how long getnstimeofday takes */
	cputs1 = __rdtsc();
	for (i = 0; i < (TSC_NANO_LOOP); ++i) {
		getnstimeofday(&now);
	}
	cputs2 = __rdtsc();
	getnstimeofday_ts = (cputs2 - cputs1 - rdtsc_ts) / (TSC_NANO_LOOP);

	/* Now figure out how many nanosecs elapses in 1000 TSC */
	cputs1 = __rdtsc();
	getnstimeofday(&ts1);
	for (i = 0; i < (TSC_NANO_LOOP); ++i) {
		__rdtsc();
	}
	getnstimeofday(&ts2);

	/* Between ts2 and ts1 there were (TSC_NANO_LOOP * rdtsc_ts)
	 * + getnstimeofday_ts TSCs */
	nanosecs = ((ts2.tv_sec - ts1.tv_sec) * NSEC_PER_SEC)
		+ ts2.tv_nsec - ts1.tv_nsec;

	printk("IHK-SMP: nanosecs / 1000TSC: %lu\n", nanosecs * 1000 /
		((TSC_NANO_LOOP * rdtsc_ts) + getnstimeofday_ts));

	local_irq_restore(flags);
	preempt_enable();

	return nanosecs * 1000 / ((TSC_NANO_LOOP * rdtsc_ts) + getnstimeofday_ts);
}

/** \brief Boot a kernel. */
static int smp_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct builtin_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	unsigned long flags;
	struct ihk_smp_trampoline_header *header;
	struct timespec now;

	spin_lock_irqsave(&dev->lock, flags);
#if 0
	if (dev->status != BUILTIN_DEV_STATUS_READY) {
		spin_unlock_irqrestore(&dev->lock, flags);
		printk("builtin: Device is busy booting another OS.\n");
		return -EINVAL;
	}
#endif
	dev->status = BUILTIN_DEV_STATUS_BOOTING;
	spin_unlock_irqrestore(&dev->lock, flags);
	
	__build_os_info(os);
	if (os->cpu_info.n_cpus < 1) {
		dprintf("builtin: There are no CPU to boot!\n");
		set_dev_status(dev, BUILTIN_DEV_STATUS_READY);

		return -EINVAL;
	}
	os->boot_cpu = os->cpu_info.hw_ids[0];

	if(os->status == BUILTIN_OS_STATUS_BOOTING) {
		printk("IHK: Device is busy booting another OS.\n");
		return -EINVAL;
	}

	set_os_status(os, BUILTIN_OS_STATUS_BOOTING);

	dprint_var_x4(os->boot_cpu);
	dprint_var_x8(os->boot_rip);

	memset(&os->param, 0, sizeof(os->param));
	os->param.bp.start = os->mem_start;
	os->param.bp.end = os->mem_end;
	os->param.bp.coreset = os->coremaps;
	os->param.ident_table = ident_page_table;
	strncpy(os->param.kernel_args, os->kernel_args,
	        sizeof(os->param.kernel_args));

	os->param.ns_per_tsc = nanosecs_per_1000tsc();
	getnstimeofday(&now);
	os->param.boot_sec = now.tv_sec;
	os->param.boot_nsec = now.tv_nsec;

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->coremaps.set[0],
	        os->param.dma_address
	);

	/* Make a temporary copy of the Linux trampoline */
	if (using_linux_trampoline) {
		memcpy(linux_trampoline_backup, trampoline_va, IHK_SMP_TRAMPOLINE_SIZE);
	}

	/* Prepare trampoline code */
	memcpy(trampoline_va, ihk_smp_trampoline_data, IHK_SMP_TRAMPOLINE_SIZE);

	header = trampoline_va; 
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(&os->param.bp);
	
	printk("IHK-SMP: booting OS 0x%lx, calling smp_wakeup_secondary_cpu() \n", 
		(unsigned long)ihk_os);
	udelay(300);
	
	return smp_wakeup_secondary_cpu(os->boot_cpu, trampoline_phys);
}

static int smp_ihk_os_load_file(ihk_os_t ihk_os, void *priv, const char *fn)
{
	struct builtin_os_data *os = priv;
	struct file *file;
	loff_t pos = 0;
	long r;
	mm_segment_t fs;
	unsigned long phys;
	unsigned long offset;
	unsigned long maxoffset;
	unsigned long flags;
	Elf64_Ehdr *elf64;
	Elf64_Phdr *elf64p;
	int i;
	unsigned long entry;
	unsigned long pml4_p;
	unsigned long pdp_p;
	unsigned long pde_p;
	unsigned long *pml4;
	unsigned long *pdp;
	unsigned long *pde;
	unsigned long *cr3;
	int n;
	extern char startup_data[];
	extern char startup_data_end[];
	unsigned long startup_p;
	unsigned long *startup;

	if (!CORE_ISSET_ANY(&os->coremaps) || os->mem_end - os->mem_start < 0) {
		printk("builtin: OS is not ready to boot.\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	file = filp_open(fn, O_RDONLY, 0);
	if (IS_ERR(file)) {
		printk("open failed: %s\n", fn);
		return -ENOENT;
	}
	elf64 = ihk_smp_map_virtual(os->mem_end - PAGE_SIZE, PAGE_SIZE); 
	if (!elf64) {
		printk("error: ioremap() returns NULL\n");
		return -EINVAL;
	}
	fs = get_fs();
	set_fs(get_ds());
	printk("IHK-SMP: loading ELF header for OS 0x%lx, phys=0x%lx\n", 
		(unsigned long)ihk_os, os->mem_end - PAGE_SIZE);
	r = vfs_read(file, (char *)elf64, PAGE_SIZE, &pos);
	set_fs(fs);
	if (r <= 0) {
		printk("vfs_read failed: %ld\n", r);
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		return (int)r;
	}
	if(elf64->e_ident[0] != 0x7f ||
	   elf64->e_ident[1] != 'E' ||
	   elf64->e_ident[2] != 'L' ||
	   elf64->e_ident[3] != 'F' ||
	   elf64->e_phoff + sizeof(Elf64_Phdr) * elf64->e_phnum > PAGE_SIZE){
		printk("kernel: BAD ELF\n");
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		return (int)-EINVAL;
	}
	entry = elf64->e_entry;
	elf64p = (Elf64_Phdr *)(((char *)elf64) + elf64->e_phoff);
	phys = (os->mem_start + LARGE_PAGE_SIZE * 2 - 1) & LARGE_PAGE_MASK;
	maxoffset = phys;

	for(i = 0; i < elf64->e_phnum; i++){
		unsigned long end;
		unsigned long size;
		char *buf;
		unsigned long pphys;
		unsigned long psize;

		if (elf64p[i].p_type != PT_LOAD)
			continue;
		if (elf64p[i].p_vaddr == 0)
			continue;

		offset = elf64p[i].p_vaddr - (MAP_KERNEL_START - phys);
		pphys = offset;
		psize = (elf64p[i].p_memsz + PAGE_SIZE - 1) & PAGE_MASK;
		size = elf64p[i].p_filesz;
		pos = elf64p[i].p_offset;
		end = pos + size;
		while(pos < end){
			long l = end - pos;

			if(l > PAGE_SIZE)
				l = PAGE_SIZE;
			if (offset + PAGE_SIZE > os->mem_end) {
				printk("builtin: OS is too big to load.\n");
				return -E2BIG;
			}
			buf = ihk_smp_map_virtual(offset, PAGE_SIZE); 
			fs = get_fs();
			set_fs(get_ds());
			r = vfs_read(file, buf, l, &pos);
			set_fs(fs);
			if(r != PAGE_SIZE){
				memset(buf + r, '\0', PAGE_SIZE - r);
			}
			ihk_smp_unmap_virtual(buf);
			if (r <= 0) {
				printk("vfs_read failed: %ld\n", r);
				ihk_smp_unmap_virtual(elf64);
				fput(file);
				return (int)r;
			}
			offset += PAGE_SIZE;
		}
		for(size = (size + PAGE_SIZE - 1) & PAGE_MASK; size < psize; size += PAGE_SIZE){

			if (offset + PAGE_SIZE > os->mem_end) {
				printk("builtin: OS is too big to load.\n");
				return -E2BIG;
			}
			buf = ihk_smp_map_virtual(offset, PAGE_SIZE); 
			memset(buf, '\0', PAGE_SIZE);
			ihk_smp_unmap_virtual(buf);
			offset += PAGE_SIZE;
		}
		if(offset > maxoffset)
			maxoffset = offset;
	}
	fput(file);
	ihk_smp_unmap_virtual(elf64);

	pml4_p = os->mem_end - PAGE_SIZE;
	pdp_p = pml4_p - PAGE_SIZE;
	pde_p = pdp_p - PAGE_SIZE;


	cr3 = ident_page_table_virt;
	pml4 = ihk_smp_map_virtual(pml4_p, PAGE_SIZE); 
	pdp = ihk_smp_map_virtual(pdp_p, PAGE_SIZE); 
	pde = ihk_smp_map_virtual(pde_p, PAGE_SIZE); 

	memset(pml4, '\0', PAGE_SIZE);
	memset(pdp, '\0', PAGE_SIZE);
	memset(pde, '\0', PAGE_SIZE);

	pml4[0] = cr3[0];
	pml4[(MAP_ST_START >> PTL4_SHIFT) & 511] = cr3[0];
	pml4[(MAP_KERNEL_START >> PTL4_SHIFT) & 511] = pdp_p | 3;
	pdp[(MAP_KERNEL_START >> PTL3_SHIFT) & 511] = pde_p | 3;
	n = (os->mem_end - os->mem_start) >> PTL2_SHIFT;
	if(n > 511)
		n = 511;

	for (i = 0; i < n; i++) {
		pde[i] = (phys + (i << PTL2_SHIFT)) | 0x83;
	}
	pde[511] = (os->mem_end - (2 << PTL2_SHIFT)) | 0x83;

	ihk_smp_unmap_virtual(pde);
	ihk_smp_unmap_virtual(pdp);
	ihk_smp_unmap_virtual(pml4);

	startup_p = os->mem_end - (2 << PTL2_SHIFT);
	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = trampoline_phys;
	startup[6] = (unsigned long)ihk_smp_irq | 
		((unsigned long)ihk_smp_irq_apicid << 32);
	startup[7] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return 0;
}

static int smp_ihk_os_load_mem(ihk_os_t ihk_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct builtin_os_data *os = priv;
	unsigned long phys, to_read, flags;
	void *virt;

	dprint_func_enter;

	/* We just load from the lowest address of the private memory */
	if (!CORE_ISSET_ANY(&os->coremaps) || os->mem_end - os->mem_start < 0) {
		printk("builtin: OS is not ready to boot.\n");
		return -EINVAL;
	}
	if (os->mem_start + size > os->mem_end) {
		printk("builtin: OS is too big to load.\n");
		return -E2BIG;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	offset += os->mem_start;
	phys = (offset & PAGE_MASK);
	offset -= phys;

	for (; size > 0; ) {
		virt = ihk_smp_map_virtual(phys, PAGE_SIZE);
		if (!virt) {
			dprintf("builtin: Failed to map %lx\n", phys);

			set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

			return -ENOMEM;
		}
		if ((offset & (PAGE_SIZE - 1)) + size > PAGE_SIZE) {
			to_read = PAGE_SIZE - (offset & (PAGE_SIZE - 1));
		} else {
			to_read = size;
		}
		dprintf("memcpy(%p[%lx], buf + %lx, %lx)\n",
		        virt, phys, offset, to_read);
		memcpy(virt, buf + offset, to_read);

		/* Offset is only non-aligned at the first copy */
		offset += to_read;
		size -= to_read;
		ihk_smp_unmap_virtual(virt);

		phys += PAGE_SIZE;
	}

	os->boot_rip = os->mem_start;

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	
	return 0;
}

static void add_free_mem_chunk(struct chunk *chunk)
{
	struct chunk *chunk_iter;
	int added = 0;

	list_for_each_entry(chunk_iter, &ihk_mem_free_chunks, chain) {

		if (chunk_iter->addr > chunk->addr) {

			/* Add in front of this chunk */
			list_add_tail(&chunk->chain, &chunk_iter->chain);

			added = 1;
			break;
		}
	}

	/* All chunks start on lower memory or list was empty */
	if (!added) {
		list_add_tail(&chunk->chain, &ihk_mem_free_chunks);
	}

	dprintf("IHK-SMP: free mem chunk 0x%lx - 0x%lx added\n",
			chunk->addr, 
			chunk->addr + chunk->size);
}

static void merge_mem_chunks(struct list_head *chunks)
{
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;

rerun:
	list_for_each_entry_safe(mem_chunk, mem_chunk_next, chunks, chain) {

		if (mem_chunk != mem_chunk_next &&
			mem_chunk_next->addr == mem_chunk->addr + mem_chunk->size &&
			mem_chunk_next->numa_id == mem_chunk->numa_id) {
			
			dprintf("IHK-SMP: free 0x%lx - 0x%lx and 0x%lx - 0x%lx merged\n",
				mem_chunk->addr,
				mem_chunk->addr + mem_chunk->size,
				mem_chunk_next->addr,
				mem_chunk_next->addr + mem_chunk_next->size);

			mem_chunk->size = mem_chunk->size + mem_chunk_next->size;
			list_del(&mem_chunk_next->chain);
				
			goto rerun;
		}
	}
}

static size_t max_size_mem_chunk(struct list_head *chunks)
{
	size_t max = 0;
	struct chunk *chunk_iter;

	list_for_each_entry(chunk_iter, chunks, chain) {
		if (chunk_iter->size > max) {
			max = chunk_iter->size;
		}
	}

	return max;
}

static int smp_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct builtin_os_data *os = priv;
	int i;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct chunk *mem_chunk;
	
	/* Reset CPU cores used by this OS */
	for (i = 0; i < IHK_SMP_MAXCPUS; ++i) {
		
		if (ihk_smp_cpus[i].os != ihk_os) 
			continue;

		ihk_smp_reset_cpu(ihk_smp_cpus[i].apic_id);
		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;

		printk("IHK-SMP: CPU %d has been deassigned, APIC: %d\n", 
			ihk_smp_cpus[i].id, ihk_smp_cpus[i].apic_id);
	}

	/* Drop memory chunk used by this OS */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os == ihk_os) {
			list_del(&os_mem_chunk->list);

			mem_chunk = (struct chunk*)phys_to_virt(os_mem_chunk->addr);
			mem_chunk->addr = os_mem_chunk->addr;
			mem_chunk->size = os_mem_chunk->size;
			mem_chunk->numa_id = os_mem_chunk->numa_id;
			INIT_LIST_HEAD(&mem_chunk->chain);

			printk("IHK-SMP: mem chunk: 0x%lx - 0x%lx (len: %lu) freed\n",
				mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
				mem_chunk->size);

			add_free_mem_chunk(mem_chunk);
			merge_mem_chunks(&ihk_mem_free_chunks);

			kfree(os_mem_chunk);
			break;
		}
	}
	
	os->status = BUILTIN_OS_STATUS_INITIAL;

	return 0;
}


static int smp_ihk_os_alloc_resource(ihk_os_t ihk_os, void *priv,
                                     struct ihk_resource *resource)
{
	struct builtin_os_data *os = priv;
	int i, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	/* Assign CPU cores */
	if (resource->cpu_cores) {
		int ihk_smp_nr_avail_cpus = 0;
		int ihk_smp_nr_allocated_cpus = 0;

		/* Check the number of available CPUs */
		for (i = 0; i < IHK_SMP_MAXCPUS; i++) {
			if (ihk_smp_cpus[i].status == IHK_SMP_CPU_AVAILABLE) {
				++ihk_smp_nr_avail_cpus;
			}
		}

		if (resource->cpu_cores > ihk_smp_nr_avail_cpus) {
			printk("IHK-SMP: error: %d CPUs requested, but only %d available\n",
					resource->cpu_cores, ihk_smp_nr_avail_cpus);
			return -EINVAL;
		}

		/* Assign cores */
		for (i = 0; i < IHK_SMP_MAXCPUS &&
			ihk_smp_nr_allocated_cpus < resource->cpu_cores; i++) {
			if (ihk_smp_cpus[i].status != IHK_SMP_CPU_AVAILABLE) {
				continue;
			}

			printk("IHK-SMP: CPU APIC %d assigned.\n",
					ihk_smp_cpus[i].apic_id);
			CORE_SET(ihk_smp_cpus[i].apic_id, os->coremaps);

			ihk_smp_cpus[i].status = IHK_SMP_CPU_ASSIGNED;
			ihk_smp_cpus[i].os = ihk_os;

			++ihk_smp_nr_allocated_cpus;
		}
	}

	/* Assign memory */
	if (resource->mem_size) {
		struct ihk_os_mem_chunk *os_mem_chunk;
		struct chunk *mem_chunk_leftover;
		struct chunk *mem_chunk_iter;
		os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk), GFP_KERNEL);

		if (!os_mem_chunk) {
			printk("IHK-DMP: error: allocating os_mem_chunk\n");
			return -ENOMEM;
		}

		os_mem_chunk->addr = 0;
		INIT_LIST_HEAD(&os_mem_chunk->list);

		list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks, chain) {
			if (mem_chunk_iter->size >= resource->mem_size) {

				os_mem_chunk->addr = mem_chunk_iter->addr;
				os_mem_chunk->size = resource->mem_size;
				os_mem_chunk->os = ihk_os;
				os_mem_chunk->numa_id = mem_chunk_iter->numa_id;

				list_del(&mem_chunk_iter->chain);
				break;
			}
		}

		if (!os_mem_chunk->addr) {
			printk("IHK-SMP: error: not enough memory\n");
			ret = -ENOMEM;
			goto error_drop_cores;
		}

		list_add(&os_mem_chunk->list, &ihk_mem_used_chunks);
		resource->mem_start = os_mem_chunk->addr;

		/* Split if there is any leftover */
		if (mem_chunk_iter->size > resource->mem_size) {
			mem_chunk_leftover = (struct chunk*)
				phys_to_virt(mem_chunk_iter->addr + resource->mem_size);
			mem_chunk_leftover->addr = mem_chunk_iter->addr +
				resource->mem_size;
			mem_chunk_leftover->size = mem_chunk_iter->size -
				resource->mem_size;
			mem_chunk_leftover->numa_id = mem_chunk_iter->numa_id;

			add_free_mem_chunk(mem_chunk_leftover);
		}

		os->mem_start = resource->mem_start;
		os->mem_end = os->mem_start + resource->mem_size;

		dprintf("IHK-SMP: memory 0x%lx - 0x%lx allocated.\n",
				os->mem_start, os->mem_end);
	}

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return 0;

error_drop_cores:
	/* Drop CPU cores for this OS */
	for (i = 0; i < IHK_SMP_MAXCPUS; ++i) {

		if (ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED ||
			ihk_smp_cpus[i].os != ihk_os)
			continue;

		printk("IHK-SMP: CPU APIC %d deassigned.\n",
				ihk_smp_cpus[i].apic_id);

		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;
	}

	return ret;
}

static enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct builtin_os_data *os = priv;
	int status;

	status = os->status;

	if (status == BUILTIN_OS_STATUS_BOOTING) {
		if (os->param.bp.status == 1) {
			return IHK_OS_STATUS_BOOTED;
		} else if(os->param.bp.status == 2) {
			/* Restore Linux trampoline once ready */
			if (using_linux_trampoline) {
				memcpy(trampoline_va, linux_trampoline_backup, 
						IHK_SMP_TRAMPOLINE_SIZE);
			}
			return IHK_OS_STATUS_READY;
		} else {
			return IHK_OS_STATUS_BOOTING;
		}
	} else {
		return IHK_OS_STATUS_NOT_BOOTED;
	}
}

static int smp_ihk_os_set_kargs(ihk_os_t ihk_os, void *priv, char *buf)
{
	unsigned long flags;
	struct builtin_os_data *os = priv;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	strncpy(os->kernel_args, buf, sizeof(os->kernel_args));

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

	return 0;
}

static int smp_ihk_os_wait_for_status(ihk_os_t ihk_os, void *priv,
                                      enum ihk_os_status status, 
                                      int sleepable, int timeout)
{
	enum ihk_os_status s;
	if (sleepable) {
		/* TODO: Enable notification of status change, and wait */
		return -1;
	} else {
		/* Polling */
		while ((s = smp_ihk_os_query_status(ihk_os, priv)),
		       s != status && s < IHK_OS_STATUS_SHUTDOWN 
		       && timeout > 0) {
			mdelay(100);
			timeout--;
		}
		return s == status ? 0 : -1;
	}
}

static int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                                      int cpu, int v)
{
	struct builtin_os_data *os = priv;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
	//printk("smp_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);
	//shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);
	
	__default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);

	return -EINVAL;
}

static unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int smp_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long local_phys,
                                    unsigned long size)
{
	return 0;
}

static int smp_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	struct builtin_os_data *os = priv;

	switch (type) {
	case IHK_SPADDR_KMSG:
		if (os->param.msg_buffer) {
			*addr = os->param.msg_buffer;
			*size = 8192;
			return 0;
		}
		break;

	case IHK_SPADDR_MIKC_QUEUE_RECV:
		if (os->param.mikc_queue_recv) {
			*addr = os->param.mikc_queue_recv;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	case IHK_SPADDR_MIKC_QUEUE_SEND:
		if (os->param.mikc_queue_send) {
			*addr = os->param.mikc_queue_send;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	}

	return -EINVAL;
}

static long smp_ihk_os_debug_request(ihk_os_t ihk_os, void *priv,
                                     unsigned int req, unsigned long arg)
{
	switch (req) {
	case IHK_OS_DEBUG_START:
		smp_ihk_os_issue_interrupt(ihk_os, priv, (arg >> 8),
		                           (arg & 0xff));
		return 0;
	}
	return -EINVAL;
}

static LIST_HEAD(builtin_interrupt_handlers);

static int smp_ihk_os_register_handler(ihk_os_t os, void *os_priv, int itype,
                                       struct ihk_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &builtin_interrupt_handlers);

	return 0;
}

static int smp_ihk_os_unregister_handler(ihk_os_t os, void *os_priv, int itype,
                                         struct ihk_host_interrupt_handler *h)
{
	list_del(&h->list);
	return 0;
}

static irqreturn_t builtin_irq_handler(int irq, void *data)
{
	struct ihk_host_interrupt_handler *h;

	/* XXX: Linear search? */
	list_for_each_entry(h, &builtin_interrupt_handlers, list) {
		if (h->func) {
			h->func(h->os, h->os_priv, h->priv);
		}
	}

	return IRQ_HANDLED;
}

static struct ihk_mem_info *smp_ihk_os_get_memory_info(ihk_os_t ihk_os,
                                                       void *priv)
{
	struct builtin_os_data *os = priv;

	return &os->mem_info;
}

static struct ihk_cpu_info *smp_ihk_os_get_cpu_info(ihk_os_t ihk_os, void *priv)
{
	struct builtin_os_data *os = priv;

	return &os->cpu_info;
}

static int smp_ihk_os_assign_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	struct builtin_os_data *os = priv;
	cpumask_t cpus_to_assign;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	memset(&cpus_to_assign, 0, sizeof(cpus_to_assign));

	/* Parse CPU list provided by user
	 * FIXME: validate userspace buffer */
	cpulist_parse((char *)arg, &cpus_to_assign);

	/* Check if cores to be assigned are available */
	for_each_cpu_mask(cpu, cpus_to_assign) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			printk("IHK-SMP: error: CPU core %d is not available for assignment\n", cpu);
			ret = -EINVAL;
			goto err;
		}
	}

	/* Do the assignment */
	for_each_cpu_mask(cpu, cpus_to_assign) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			printk("IHK-SMP: error: CPU core %d is not available for assignment\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		CORE_SET(ihk_smp_cpus[cpu].apic_id, os->coremaps);

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ASSIGNED;
		ihk_smp_cpus[cpu].os = ihk_os;

		printk("IHK-SMP: CPU APIC %d assigned to %p.\n",
				ihk_smp_cpus[cpu].apic_id, ihk_os);
	}

	ret = 0;

err:
	return ret;
}

static int smp_ihk_os_release_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	struct builtin_os_data *os = priv;
	cpumask_t cpus_to_release;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	memset(&cpus_to_release, 0, sizeof(cpus_to_release));

	/* Parse CPU list provided by user
	 * FIXME: validate userspace buffer */
	cpulist_parse((char *)arg, &cpus_to_release);

	/* Check if cores to be released are assigned to this OS */
	for_each_cpu_mask(cpu, cpus_to_release) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ASSIGNED ||
			ihk_smp_cpus[cpu].os != ihk_os) {
			printk("IHK-SMP: error: CPU core %d is not assigned to %p\n", 
				cpu, ihk_os);
			ret = -EINVAL;
			goto err;
		}
	}

	/* Do the release */
	for_each_cpu_mask(cpu, cpus_to_release) {

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);
		CORE_CLR(ihk_smp_cpus[cpu].apic_id, os->coremaps);

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		printk("IHK-SMP: CPU APIC %d released from 0x%p.\n",
				ihk_smp_cpus[cpu].apic_id, os);
	}

	ret = 0;

err:
	return ret;
}

char query_res[1024];

static int smp_ihk_os_query_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int cpu;
	cpumask_t cpus_assigned;

	memset(&cpus_assigned, 0, sizeof(cpus_assigned));
	memset(query_res, 0, sizeof(query_res));

	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ASSIGNED)
			continue;
		if (ihk_smp_cpus[cpu].os != ihk_os)
			continue;

		cpumask_set_cpu(cpu, &cpus_assigned);
	}

	bitmap_scnlistprintf(query_res, sizeof(query_res),
		cpumask_bits(&cpus_assigned), nr_cpumask_bits);

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}


static int smp_ihk_parse_mem(char *p, size_t *mem_size, int *numa_id)
{
	char *oldp;

	/* Parse memory string provided by the user
	 * FIXME: validate userspace buffer */
	oldp = p;
	*mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	if (!(*p)) {
		*numa_id = 0;
	}
	else {
		if (*p != '@') {
			return -EINVAL;
		}

		*numa_id = memparse(p + 1, &p);
	}

	dprintf("smp_ihk_parse_mem(): %lu @ %d parsed\n", *mem_size, *numa_id);

	return 0;
}


static int smp_ihk_os_assign_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	struct builtin_os_data *os = priv;
	unsigned long flags;
	int ret;

	struct ihk_os_mem_chunk *os_mem_chunk;
	struct chunk *mem_chunk_leftover;
	struct chunk *mem_chunk_iter;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	ret = smp_ihk_parse_mem((char *)arg, &mem_size, &numa_id);
	if (ret != 0) {
		printk("IHK-SMP: os_assign_mem: error: parsing memory string\n");
		return ret;
	}

	/* Do the assignment */
	os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk), GFP_KERNEL);

	if (!os_mem_chunk) {
		printk("IHK-SMP: error: allocating os_mem_chunk\n");
		return -ENOMEM;
	}

	os_mem_chunk->addr = 0;
	os_mem_chunk->numa_id = numa_id;
	INIT_LIST_HEAD(&os_mem_chunk->list);

	/* Find a big enough free memory chunk on this NUMA node */
	list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks, chain) {
		if (mem_chunk_iter->size >= mem_size &&
				mem_chunk_iter->numa_id == numa_id) {

			os_mem_chunk->addr = mem_chunk_iter->addr;
			os_mem_chunk->size = mem_size;
			os_mem_chunk->os = ihk_os;

			list_del(&mem_chunk_iter->chain);
			break;
		}
	}

	if (!os_mem_chunk->addr) {
		printk("IHK-SMP: error: not enough memory on ihk_mem_free_chunks\n");
		kfree(os_mem_chunk);
		ret = -ENOMEM;
		goto out;
	}

	list_add(&os_mem_chunk->list, &ihk_mem_used_chunks);

	/* Split if there is any leftover */
	if (mem_chunk_iter->size > mem_size) {
		mem_chunk_leftover = (struct chunk*)
			phys_to_virt(mem_chunk_iter->addr + mem_size);
		mem_chunk_leftover->addr = mem_chunk_iter->addr + mem_size;
		mem_chunk_leftover->size = mem_chunk_iter->size - mem_size;

		add_free_mem_chunk(mem_chunk_leftover);
	}

	os->mem_start = os_mem_chunk->addr;
	os->mem_end = os->mem_start + mem_size;

	dprintf("IHK-SMP: memory 0x%lx - 0x%lx (len: %lu) @ NUMA node %d assigned to 0x%p\n",
			os->mem_start, os->mem_end, (os->mem_end - os->mem_start),
			numa_id, ihk_os);

	ret = 0;
out:

	return ret;
}

static int smp_ihk_os_release_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	struct builtin_os_data *os = priv;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	ret = smp_ihk_parse_mem((char *)arg, &mem_size, &numa_id);
	if (ret != 0) {
		printk("IHK-SMP: os_assign_mem: error: parsing memory string\n");
		return ret;
	}

	/* Do the release */

	return ret;
}

/* FIXME: use some max NUMA domain macro */
unsigned long query_mem_per_numa[1024];

static int smp_ihk_os_query_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int i;
	struct ihk_os_mem_chunk *os_mem_chunk;

	memset(query_mem_per_numa, 0, sizeof(query_mem_per_numa));
	memset(query_res, 0, sizeof(query_res));

	/* Collect memory information */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os) 
			continue;

		query_mem_per_numa[os_mem_chunk->numa_id] += os_mem_chunk->size;
	}

	for (i = 0; i < 1024; ++i) {
		if (!query_mem_per_numa[i]) 
			continue;

		sprintf(query_res, "%lu@%d", query_mem_per_numa[i], i);
		break;
	}

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying mem string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}



static struct ihk_os_ops smp_ihk_os_ops = {
	.load_mem = smp_ihk_os_load_mem,
	.load_file = smp_ihk_os_load_file,
	.boot = smp_ihk_os_boot,
	.shutdown = smp_ihk_os_shutdown,
	.alloc_resource = smp_ihk_os_alloc_resource,
	.query_status = smp_ihk_os_query_status,
	.wait_for_status = smp_ihk_os_wait_for_status,
	.set_kargs = smp_ihk_os_set_kargs,
	.issue_interrupt = smp_ihk_os_issue_interrupt,
	.map_memory = smp_ihk_os_map_memory,
	.unmap_memory = smp_ihk_os_unmap_memory,
	.register_handler = smp_ihk_os_register_handler,
	.unregister_handler = smp_ihk_os_unregister_handler,
	.get_special_addr = smp_ihk_os_get_special_addr,
	.debug_request = smp_ihk_os_debug_request,
	.get_memory_info = smp_ihk_os_get_memory_info,
	.get_cpu_info = smp_ihk_os_get_cpu_info,
	.assign_cpu = smp_ihk_os_assign_cpu,
	.release_cpu = smp_ihk_os_release_cpu,
	.query_cpu = smp_ihk_os_query_cpu,
	.assign_mem = smp_ihk_os_assign_mem,
	.release_mem = smp_ihk_os_release_mem,
	.query_mem = smp_ihk_os_query_mem,
};	

static struct ihk_register_os_data builtin_os_reg_data = {
	.name = "builtinos",
	.flag = 0,
	.ops = &smp_ihk_os_ops,
};

static int smp_ihk_create_os(ihk_device_t ihk_dev, void *priv,
                             unsigned long arg, ihk_os_t ihk_os,
                             struct ihk_register_os_data *regdata)
{
	struct builtin_device_data *data = priv;
	struct builtin_os_data *os;

	*regdata = builtin_os_reg_data;

	os = kzalloc(sizeof(struct builtin_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		return -ENOMEM;
	}
	spin_lock_init(&os->lock);
	os->dev = data;
	regdata->priv = os;

	return 0;
}

/** \brief Map a remote physical memory to the local physical memory.
 *
 * In BUILTIN, all the kernels including the host kernel are running in the
 * same physical memory map, thus there is nothing to do. */
static unsigned long smp_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                        unsigned long remote_phys,
                                        unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size)
{
	return 0;
}



static void *smp_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	if (!virt) {
		void *ret;

		ret = ihk_smp_map_virtual(phys, size);
		if (!ret) {
			printk("WARNING: ihk_smp_map_virtual() returned NULL!\n");
		}

		return ret;
		/*
		if (phys >= ihk_phys_start) {
			return ioremap_cache(phys, size);
		}
		else
			return 0;
		//return shimos_other_os_map(phys, size);
		*/
	}
	else {
		return ihk_host_map_generic(ihk_dev, phys, virt, size, flags);
	}
}

static int smp_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size)
{
	ihk_smp_unmap_virtual(virt);
	return 0;

	/*
	if ((unsigned long)virt >= PAGE_OFFSET) {
		iounmap(virt);
		return 0;
		//return shimos_other_os_unmap(virt, size);
	} else {
		return ihk_host_unmap_generic(ihk_dev, virt, size);
	}
	return 0;
	*/
}

static long smp_ihk_debug_request(ihk_device_t ihk_dev, void *priv,
                                  unsigned int req, unsigned long arg)
{
	return -EINVAL;
}


static void smp_ihk_init_ident_page_table(void)
{
	int ident_npages = 0;
	int i, j, k, ident_npages_order;
	unsigned long maxmem = 0, *p, physaddr;
	struct page *ident_pages;

	/* 256GB */
	maxmem = (unsigned long)256 * (1024 * 1024 * 1024);

	ident_npages = (maxmem + (1UL << PUD_SHIFT) - 1) >> PUD_SHIFT;
	ident_npages_order = fls(ident_npages + 2) - 1; 
	if ((2 << ident_npages_order) != ident_npages + 2) {
		ident_npages_order++;
	}

	printk("IHK-SMP: page table pages = %d, ident_npages_order = %d\n", 
			ident_npages, ident_npages_order);

	ident_pages = alloc_pages(GFP_DMA | GFP_KERNEL, ident_npages_order);
	ident_page_table = page_to_phys(ident_pages);
	ident_page_table_virt = pfn_to_kaddr(page_to_pfn(ident_pages));

	memset(ident_page_table_virt, 0, ident_npages);

	/* First level : We consider only < 512 GB of memory */
	ident_page_table_virt[0] = (ident_page_table + PAGE_SIZE) | 0x63;

	/* Second level */
	p = ident_page_table_virt + (PAGE_SIZE / sizeof(*p));

	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (((unsigned long)i << PUD_SHIFT) < maxmem) {
			*p = (ident_page_table + PAGE_SIZE * (2 + i)) | 0x63;
		}
		else {
			break;
		}
		p++;
	}

	if (i != ident_npages) {
		printk("Something wrong for memory map. : %d vs %d\n", i, ident_npages);
	}

	/* Third level */
	p = ident_page_table_virt + (PAGE_SIZE * 2 / sizeof(*p));
	for (j = 0; j < ident_npages; j++) {
		for (k = 0; k < PTRS_PER_PMD; k++) {
			physaddr = ((unsigned long)j << PUD_SHIFT) | 
				((unsigned long)k << PMD_SHIFT);
			if (physaddr < maxmem) {
				*p = physaddr | 0xe3;
				p++;
			}
			else {
				break;
			}
		}
	}

	printk("IHK-SMP: identity page tables allocated\n");
}


static irqreturn_t smp_ihk_interrupt(int irq, void *dev_id) 
{
	ack_APIC_irq();
	builtin_irq_handler(ihk_smp_irq, NULL);
	return IRQ_HANDLED;
}

#ifdef CONFIG_SPARSE_IRQ
void (*orig_irq_flow_handler)(unsigned int irq, struct irq_desc *desc) = NULL;
void ihk_smp_irq_flow_handler(unsigned int irq, struct irq_desc *desc)
{
	if (!desc->action || !desc->action->handler) {
		printk("IHK-SMP: no handler for IRQ %d??\n", irq);
		return;
	}
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	raw_spin_lock(&desc->lock);
#else
	spin_lock(&desc->lock);
#endif

	//printk("IHK-SMP: calling handler for IRQ %d\n", irq);
	desc->action->handler(irq, NULL);
	//ack_APIC_irq();
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	raw_spin_unlock(&desc->lock);
#else
	spin_unlock(&desc->lock);
#endif
}
#endif

int shimos_nchunks = 16;

int __ihk_smp_reserve_mem(size_t ihk_mem, int numa_id)
{
	const int order = 10;		/* 4 MiB a chunk */
	size_t want;
	size_t allocated;
	struct chunk *p;
	struct chunk *q;
	void *va;
	size_t remain;
	int ret;
	struct list_head tmp_chunks;

	INIT_LIST_HEAD(&tmp_chunks);

	printk(KERN_INFO "IHK-SMP: __ihk_smp_reserve_mem: %lu bytes\n", ihk_mem);

	want = ihk_mem & ~((PAGE_SIZE << order) - 1);
	allocated = 0;

	/* Allocate and merge pages until we get a contigous area
	 * or run out of free memory. Keep the longest areas */
	while (max_size_mem_chunk(&tmp_chunks) < want) {
		struct page *pg;

		pg = alloc_pages_node(numa_id, GFP_KERNEL | __GFP_COMP, order);
		if (!pg) {
			if (allocated >= want) break;

			printk(KERN_ERR "IHK-SMP: error: __alloc_pages_node() failed\n");

			ret = -1;
			goto out;
		}

		p = page_address(pg);

		allocated += PAGE_SIZE << order;

		p->addr = virt_to_phys(p);
		p->size = PAGE_SIZE << order;
		p->numa_id = numa_id;
		INIT_LIST_HEAD(&p->chain);

		/* Insert the chunk in physical address ascending order */
		list_for_each_entry(q, &tmp_chunks, chain) {
			if (p->addr < q->addr) {
				break;
			}
		}

		if ((void *)q == &tmp_chunks) {
			list_add_tail(&p->chain, &tmp_chunks);
		}
		else {
			list_add_tail(&p->chain, &q->chain);
		}

		/* Merge adjucent chunks */
		merge_mem_chunks(&tmp_chunks);
	}

	/* Move the largest chunks to free list until we meet the required size */
	allocated = 0;
	while (allocated < want) {
		size_t max = 0;
		p = NULL;

		list_for_each_entry(q, &tmp_chunks, chain) {
			if (q->size > max) {
				p = q;
				max = p->size;
			}
		}

		if (!p) break;

		list_del(&p->chain);

		/* Insert the chunk in physical address ascending order */
		list_for_each_entry(q, &ihk_mem_free_chunks, chain) {
			if (p->addr < q->addr) {
				break;
			}
		}

		if ((void *)q == &ihk_mem_free_chunks) {
			list_add_tail(&p->chain, &ihk_mem_free_chunks);
		}
		else {
			list_add_tail(&p->chain, &q->chain);
		}

		allocated += max;
	}

	/* Merge free chunks in case this wasn't the first reservation */
	merge_mem_chunks(&ihk_mem_free_chunks);

	list_for_each_entry(p, &ihk_mem_free_chunks, chain) {
		printk(KERN_INFO "IHK-SMP: chunk 0x%lx - 0x%lx (len: %lu) @ NUMA node: %d is available\n",
				p->addr, p->addr + p->size, p->size, p->numa_id);
	}

	ret = 0;

out:
	/* Free leftover tmp_chunks */
	list_for_each_entry_safe(p, q, &tmp_chunks, chain) {
		list_del(&p->chain);

		va = phys_to_virt(p->addr);
		remain = p->size;

		while (remain > 0) {
			free_pages((uintptr_t)va, order);
			va += PAGE_SIZE << order;
			remain -= PAGE_SIZE << order;
		}
	}

	return ret;
}

static int smp_ihk_offline_cpu(int cpu_id)
{
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	struct device *dev = get_cpu_device(cpu_id);
#else
	struct sys_device *dev = get_cpu_sysdev(cpu_id);
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	ret = dev->bus->offline(dev);
	if (!ret) {
		kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		dev->offline = true;
	}
#else
	_cpu_hotplug_driver_lock();

	ret = cpu_down(cpu->sysdev.id);
	if (!ret)
		kobject_uevent(&dev->kobj, KOBJ_OFFLINE);

	_cpu_hotplug_driver_unlock();
#endif

	if (ret < 0) {
		printk("IHK-SMP: error: hot-unplugging CPU %d\n", cpu_id);
		return -EFAULT;
	}

	return 0;
}

static int smp_ihk_reserve_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int cpu;
	cpumask_t cpus_to_offline;;

	memset(&cpus_to_offline, 0, sizeof(cpus_to_offline));

	/* Parse CPU list provided by user
	 * FIXME: validate userspace buffer */
	cpulist_parse((char *)arg, &cpus_to_offline);

	/* Collect cores to be offlined */
	for_each_cpu_mask(cpu, cpus_to_offline) {

		if (cpu > IHK_SMP_MAXCPUS) {
			printk("IHK-SMP: error: CPU %d is out of limit\n", cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not present\n", cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_online(cpu)) {
			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_AVAILABLE)
				printk("IHK-SMP: error: CPU %d was reserved already\n", cpu);

			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ASSIGNED)
				printk("IHK-SMP: erro: CPU %d was assigned already\n", cpu);

			ret = -EINVAL;
			goto err_before_offline;
		}

		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ONLINE) {
			printk("IHK-SMP: error: CPU %d is in inconsistent state, skipping\n",
					cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		ihk_smp_cpus[cpu].id = cpu;
		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_OFFLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		printk("IHK-SMP: CPU %d to be offlined, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Offline CPU cores */
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_OFFLINE)
			continue;

		if ((ret = smp_ihk_offline_cpu(cpu)) != 0) {
			goto err_during_offline;
		}

		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_OFFLINED;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);

		printk("IHK-SMP: CPU %d offlined successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Offlining CPU cores went well, mark them as available */
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_OFFLINED)
			continue;
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;

		printk("IHK-SMP: CPU %d reserved successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	ret = 0;
	goto out;

err_during_offline:
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_OFFLINED)
			continue;

		/* TODO: actually online CPU core */
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
	}

err_before_offline:
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_OFFLINE)
			continue;

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
	}

out:
	return ret;
}

static int smp_ihk_online_cpu(int cpu_id)
{
	int ret;

	ret = ihk_cpu_up(cpu_id, 1);
	if (ret) {
		printk("IHK-SMP: WARNING: failed to re-enable CPU %d\n", cpu_id);
	}

	return 0;
}


static int smp_ihk_release_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int cpu;
	cpumask_t cpus_to_online;;

	memset(&cpus_to_online, 0, sizeof(cpus_to_online));

	/* Parse CPU list provided by user
	 * FIXME: validate userspace buffer */
	cpulist_parse((char *)arg, &cpus_to_online);

	/* Collect cores to be onlined */
	for_each_cpu_mask(cpu, cpus_to_online) {

		if (cpu > IHK_SMP_MAXCPUS) {
			printk("IHK-SMP: error: CPU %d is out of limit\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not valid\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		if (cpu_online(cpu)) {
			continue;
		}

		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			printk("IHK-SMP: error: CPU %d is in use\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		ihk_smp_cpus[cpu].id = cpu;
		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_ONLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		printk("IHK-SMP: CPU %d to be onlined, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Online CPU cores */
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_ONLINE)
			continue;

		if ((ret = smp_ihk_online_cpu(cpu)) != 0) {
			goto err;
		}

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		printk("IHK-SMP: CPU %d onlined successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	ret = 0;
	goto out;

err:
	/* Something went wrong, what shall we do?
	 * Mark "to be onlined" cores as available for now */
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_ONLINE)
			continue;

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;
	}

out:
	return ret;
}

static int smp_ihk_query_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int cpu;
	cpumask_t cpus_reserved;

	memset(&cpus_reserved, 0, sizeof(cpus_reserved));
	memset(query_res, 0, sizeof(query_res));

	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE &&
			ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ASSIGNED)
			continue;

		cpumask_set_cpu(cpu, &cpus_reserved);
	}

	bitmap_scnlistprintf(query_res, sizeof(query_res),
		cpumask_bits(&cpus_reserved), nr_cpumask_bits);

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int smp_ihk_reserve_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	int ret;

	ret = smp_ihk_parse_mem((char *)arg, &mem_size, &numa_id);
	if (ret != 0) {
		printk("IHK-SMP: reserve_mem: error: parsing memory string\n");
		return ret;
	}

	/* Do the reservation */
	ret = __ihk_smp_reserve_mem(mem_size, numa_id);

	return ret;
}

static int smp_ihk_release_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	//size_t mem_size;
	//int numa_id;
	int ret = 0;
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;
	unsigned long size_left;
	unsigned long va;

	/*
	ret = smp_ihk_parse_mem((char *)arg, &mem_size, &numa_id);
	if (ret != 0) {
		printk("IHK-SMP: release_mem: error: parsing memory string\n");
		return ret;
	}
	*/
	/* Find out if there is enough free memory to be released */

	/* Drop all memory for now.. */
	list_for_each_entry_safe(mem_chunk, mem_chunk_next, 
			&ihk_mem_free_chunks, chain) {
		unsigned long pa = mem_chunk->addr;
#ifdef IHK_DEBUG
		unsigned long size = mem_chunk->size;
#endif

		list_del(&mem_chunk->chain);

		va = (unsigned long)phys_to_virt(pa);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			/* NOTE: memory was allocated via __get_free_pages() in 4MB blocks */
			int order = 10;
			int order_size = 4194304;

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order); 
			size_left -= order_size;
			va += order_size;
		}

		dprintf("IHK-SMP: 0x%lx - 0x%lx freed\n", pa, pa + size);
	}

	return ret;
}

static int smp_ihk_query_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	int i;
	struct chunk *mem_chunk;
	struct ihk_os_mem_chunk *os_mem_chunk;

	memset(query_mem_per_numa, 0, sizeof(query_mem_per_numa));
	memset(query_res, 0, sizeof(query_res));

	/* Collect memory information */
	list_for_each_entry(mem_chunk, &ihk_mem_free_chunks, chain) {
		query_mem_per_numa[mem_chunk->numa_id] += mem_chunk->size;
	}

	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		query_mem_per_numa[os_mem_chunk->numa_id] += os_mem_chunk->size;
	}

	for (i = 0; i < 1024; ++i) {
		if (!query_mem_per_numa[i]) 
			continue;

		sprintf(query_res, "%lu@%d", query_mem_per_numa[i], i);
		break;
	}

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying mem string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int smp_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	int vector = IRQ15_VECTOR + 2;

	INIT_LIST_HEAD(&ihk_mem_free_chunks);
	INIT_LIST_HEAD(&ihk_mem_used_chunks);

	if (ihk_cores) {
		if (ihk_cores > (num_present_cpus() - 1)) {
			printk("IHK-SMP error: only %d CPUs in total are available\n", 
					num_present_cpus());
			return EINVAL;	
		}
	}

	if (ihk_trampoline) {
		printk("IHK-SMP: preallocated trampoline phys: 0x%lx\n", ihk_trampoline);
		
		trampoline_phys = ihk_trampoline;
		trampoline_va = ioremap_cache(trampoline_phys, PAGE_SIZE);

	}
	else {
#define TRAMP_ATTEMPTS	20
		int attempts = 0;
		struct page *bad_pages[TRAMP_ATTEMPTS];
		
		memset(bad_pages, 0, TRAMP_ATTEMPTS * sizeof(struct page *));

		/* Try to allocate trampoline page, it has to be under 1M so we can 
		 * execute real-mode AP code. If allocation fails more than 
		 * TRAMP_ATTEMPTS times, we will use Linux's one.
		 * NOTE: using Linux trampoline could potentially cause race 
		 * conditions with concurrent CPU onlining requests */
retry_trampoline:
		trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, 1);

		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
			bad_pages[attempts] = trampoline_page;
			
			if (++attempts < TRAMP_ATTEMPTS) {
				goto retry_trampoline;
			}
		}

		/* Free failed attempts.. */
		for (attempts = 0; attempts < TRAMP_ATTEMPTS; ++attempts) {
			if (!bad_pages[attempts]) {
				continue;
			}

			free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(bad_pages[attempts])), 1);
		}

		/* Couldn't allocate trampoline page, use Linux' one from real_header */
		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#ifdef IHK_KSYM_real_mode_header
#if IHK_KSYM_real_mode_header
			printk("IHK-SMP: warning: allocating trampoline_page failed, using Linux'\n");
			using_linux_trampoline = 1;

			trampoline_phys = real_mode_header->trampoline_start;
			trampoline_va = __va(trampoline_phys);
#endif
#else
			printk("IHK-SMP: error: allocating trampoline area\n");
			return ENOMEM;
#endif
#else /* LINUX_VERSION_CODE < 3.10 */
			printk("IHK-SMP: warning: allocating trampoline_page failed, using Linux'\n");
			using_linux_trampoline = 1;

			trampoline_phys = TRAMPOLINE_BASE;
			trampoline_va = __va(TRAMPOLINE_BASE);
#endif
		}
		else {
			trampoline_phys = page_to_phys(trampoline_page);
			trampoline_va = pfn_to_kaddr(page_to_pfn(trampoline_page));
		}

		printk("IHK-SMP: trampoline_page phys: 0x%lx\n", trampoline_phys);
	}

	memset(ihk_smp_cpus, 0, sizeof(ihk_smp_cpus));

	/* Find a suitable IRQ vector */
	for (vector = ihk_start_irq ? ihk_start_irq : (IRQ14_VECTOR + 2); 
			vector < 256; vector += 1) {
		struct irq_desc *desc;

		if (test_bit(vector, used_vectors)) {
			printk("IRQ vector %d: used\n", vector);
			continue;
		}
		
#ifdef CONFIG_SPARSE_IRQ
		/* If no descriptor, create one */
		desc = _irq_to_desc(vector);
		if (!desc) {
			printk("IRQ vector %d: no descriptor, allocating\n", vector);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
			desc = _alloc_desc(vector, first_online_node, THIS_MODULE);
			desc->irq_data.chip = _dummy_irq_chip;
			radix_tree_insert(_irq_desc_tree, vector, desc);
#else
			desc = _irq_to_desc_alloc_node(vector, first_online_node);
			if (!desc) {
				printk("IRQ vector %d: still no descriptor??\n", vector);	
				continue;
			}
			desc->chip = _dummy_irq_chip;
#endif
		}
		
		desc = _irq_to_desc(vector);
		if (!desc) {
			printk("IRQ vector %d: still no descriptor??\n", vector);	
			continue;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			
			printk("IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
#else
		if (desc->status & IRQ_NOREQUEST) {
			
			printk("IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status &= ~IRQ_NOREQUEST;
		}
#endif
		orig_irq_flow_handler = desc->handle_irq;
		desc->handle_irq = ihk_smp_irq_flow_handler;
#endif // CONFIG_SPARSE_IRQ

		if (request_irq(vector, 
					smp_ihk_interrupt, IRQF_DISABLED, "IHK-SMP", NULL) != 0) { 
			printk("IRQ vector %d: request_irq failed\n", vector);
			continue;
		}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)))
		/* NOTE: this is nasty, but we need to decrease the refcount because
		 * between Linux 3.0 and 3.18 request_irq holds a reference to the
		 * module. (As of 3.18 the refcounting code changed.)
		 * This causes rmmod to fail and report the module is in use when one
		 * tries to unload it. To overcome this, we drop one ref here and get
		 * an extra one before free_irq in the module's exit code */
		module_put(THIS_MODULE);
#endif
	
		/* Pretend a real external interrupt */
		{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq, 
						per_cpu_offset(ihk_ikc_irq_core)));
#else
			int *vectors = 
				(*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq, 
				per_cpu_offset(ihk_ikc_irq_core)));
#endif			
		
			if (vectors[vector] == -1) {
				printk("fixed vector_irq for %d\n", vector);
				vectors[vector] = vector;
			}
			
		}
		
		break;
	}

	if (vector >= 256) {
		printk("error: allocating IKC irq vector\n");
		return EFAULT;
	}

	ihk_smp_irq = vector;
	ihk_smp_irq_apicid = (int)per_cpu(x86_bios_cpu_apicid, 
		ihk_ikc_irq_core);
	printk("IHK-SMP: IKC irq vector: %d, CPU logical id: %u, CPU APIC id: %d\n", 
		ihk_smp_irq, ihk_ikc_irq_core, ihk_smp_irq_apicid);

	smp_ihk_init_ident_page_table();

	return 0;
}

int ihk_smp_reset_cpu(int phys_apicid) {
	unsigned long send_status;
	int maxlvt;

	printk("IHK-SMP: resetting CPU %d.\n", phys_apicid);

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)         /* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
			phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	return 0;
}

static int smp_ihk_exit(ihk_device_t ihk_dev, void *priv) 
{
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;
	unsigned long size_left;
	unsigned long va;
	int cpu;

#ifdef CONFIG_SPARSE_IRQ
	struct irq_desc *desc;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq, 
				per_cpu_offset(ihk_ikc_irq_core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq, 
				per_cpu_offset(ihk_ikc_irq_core)));
#endif	
	
	/* Release IRQ vector */
	vectors[ihk_smp_irq] = -1;

#ifdef CONFIG_SPARSE_IRQ
	desc = _irq_to_desc(ihk_smp_irq);
	desc->handle_irq = orig_irq_flow_handler;
#endif

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)))
	try_module_get(THIS_MODULE);
#endif

	free_irq(ihk_smp_irq, NULL);
	
#ifdef CONFIG_SPARSE_IRQ
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	irq_free_descs(ihk_smp_irq, 1);
#endif
#endif

	/* Re-enable CPU cores */
	for (cpu = 0; cpu < IHK_SMP_MAXCPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ONLINE)
			continue;

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);

		if (smp_ihk_online_cpu(cpu) != 0) {
			continue;
		}

		printk("IHK-SMP: CPU %d onlined successfully, APIC: %d\n", 
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
	else {
		if (!using_linux_trampoline)
			iounmap(trampoline_va);
	}

	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt, ident_npages_order);
	}

	/* Free memory */
	list_for_each_entry_safe(mem_chunk, mem_chunk_next, 
			&ihk_mem_free_chunks, chain) {

		list_del(&mem_chunk->chain);

		va = (unsigned long)phys_to_virt(mem_chunk->addr);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			/* NOTE: memory was allocated via __get_free_pages() in 4MB blocks */
			int order = 10;
			int order_size = 4194304;

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order); 
			size_left -= order_size;
			va += order_size;
		}
	}

	return 0;
}

static struct ihk_device_ops smp_ihk_device_ops = {
	.init = smp_ihk_init,
	.exit = smp_ihk_exit,
	.create_os = smp_ihk_create_os,
	.map_memory = smp_ihk_map_memory,
	.unmap_memory = smp_ihk_unmap_memory,
	.map_virtual = smp_ihk_map_virtual,
	.unmap_virtual = smp_ihk_unmap_virtual,
	.debug_request = smp_ihk_debug_request,
	.get_dma_channel = smp_ihk_get_dma_channel,
	.reserve_cpu = smp_ihk_reserve_cpu,
	.release_cpu = smp_ihk_release_cpu,
	.reserve_mem = smp_ihk_reserve_mem,
	.release_mem = smp_ihk_release_mem,
	.query_cpu = smp_ihk_query_cpu,
	.query_mem = smp_ihk_query_mem,
};	

/** \brief The driver-specific driver structure
 *
 * Since there is only one BUILTIN "device" in machine, this structure is
 * statically allocated. */
static struct builtin_device_data builtin_data;

static struct ihk_register_device_data builtin_dev_reg_data = {
	.name = "builtin",
	.flag = 0,
	.priv = &builtin_data,
	.ops = &smp_ihk_device_ops,
};

static int __init smp_module_init(void)
{
	ihk_device_t ihkd;

	printk(KERN_INFO "IHK-SMP: initializing...\n");

	spin_lock_init(&builtin_data.lock);

	if (!(ihkd = ihk_register_device(&builtin_dev_reg_data))) {
		printk(KERN_INFO "builtin: Failed to register ihk driver.\n");
		return -ENOMEM;
	}

	builtin_data.ihk_dev = ihkd;
	
	return 0;
}

static void __exit smp_module_exit(void)
{
	printk(KERN_INFO "IHK-SMP: finalizing...\n");
	ihk_unregister_device(builtin_data.ihk_dev);
}

module_init(smp_module_init);
module_exit(smp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
