/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2013
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */

#include "config.h"

#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <syscall.h>
#include <libfdt.h>
#include <limits.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>

#include "lite.h"

#include "kexec_memory_map.h"
#include "simple_allocator.h"

#define VERSION "1.0.0"

#define PROC_DEVICE_TREE "/proc/device-tree"
#define RESERVED_REGIONS "30"
#define DEVICE_TREE_PAD (1UL * 1024 * 1024)

int debug;

#define debug_printf(A...)		\
do { 					\
	if (debug)			\
		fprintf(stderr, A);	\
} while (0)				\

struct kexec_segment {
	const char *type;
	void *buf;
	size_t bufsz;
	void *mem;
	size_t memsz;
};

#define	LINUX_REBOOT_CMD_KEXEC	0x45584543


#define FDT_ERROR(STR, ERROR)						\
do {									\
	fprintf(stderr, "%s: %s returned %s\n", __func__, (STR),	\
		fdt_strerror(ERROR));					\
} while (0)


#define MAX_KEXEC_SEGMENTS	128
static int kexec_segment_nr;
static struct kexec_segment kexec_segments[MAX_KEXEC_SEGMENTS];

unsigned long kernel_addr;
void *kernel_current_addr;
unsigned long device_tree_addr;
unsigned long kexec_load_addr;
static unsigned long initrd_start;
static unsigned long initrd_end;

static void dump_segment(FILE *stream, const char *header, unsigned int number,
	const struct kexec_segment *seg)
{
	fprintf(stream,
		"%ssegment %d: "
		"%-11s buf %016" PRIxPTR " bufsize 0x%08zx, mem %016" PRIxPTR
		", memsize 0x%08zx\n",
		header, number, seg->type, (uintptr_t)seg->buf, seg->bufsz,
		(uintptr_t)seg->mem, seg->memsz);
}

static void dump_segments(FILE *stream, const char *header)
{
	int i;

	for (i = 0; i < kexec_segment_nr; i++)
		dump_segment(stream, header, i + 1, &kexec_segments[i]);
}

void add_kexec_segment(const char *type, void *buf, unsigned long bufsize,
			      void *dest, unsigned long memsize)
{
	if (kexec_segment_nr == MAX_KEXEC_SEGMENTS) {
		fprintf(stderr, "Too many kexec segments, increase "
			"MAX_KEXEC_SEGMENTS\n");
		exit(1);
	}

	kexec_segments[kexec_segment_nr].type = type;
	kexec_segments[kexec_segment_nr].buf = buf;
	kexec_segments[kexec_segment_nr].bufsz = bufsize;
	kexec_segments[kexec_segment_nr].mem = dest;
	kexec_segments[kexec_segment_nr].memsz = memsize;

	if (debug)
		dump_segment(stderr, "add_kexec_segment: ", kexec_segment_nr,
			&kexec_segments[kexec_segment_nr]);

	kexec_segment_nr++;
}

static GElf_Shdr *getshdr(Elf *e, int idx, GElf_Shdr *shdr)
{
	if (gelf_getshdr(elf_getscn(e, idx), shdr) == NULL) {
		fprintf(stderr, "getshdr: get section failed\n");
		return (NULL);
	}

	return (shdr);
}

static GElf_Addr get_entry_addr(Elf *e, GElf_Ehdr ehdr, GElf_Addr entry)
{
	Elf_Data *data = NULL;
	GElf_Shdr shdr;
	GElf_Addr new_entry;
	unsigned int fileoff = 0;
	int i;

	for (i = 0; i < ehdr.e_shnum; i++) {
		if (getshdr(e, i, &shdr) == NULL) {
			printf("address_to_offset: getshdr failed\n");
			return (entry);
		}

		if (entry >= shdr.sh_addr &&
		    entry < shdr.sh_addr + shdr.sh_size &&
		    strncmp(".opd", elf_strptr(e, ehdr.e_shstrndx, shdr.sh_name), 4) == 0) {
			fileoff = shdr.sh_offset + (entry - shdr.sh_addr);
			break;
		}
	}

	if (fileoff == 0)
		return (entry);

	data = elf_getdata_rawchunk(e, fileoff, sizeof(new_entry), ELF_T_BYTE);

	new_entry = *(GElf_Addr *)data->d_buf;
	new_entry = be64toh(new_entry);

	return (new_entry);
}

static void elf_init(struct elf_image *elf, const char *image)
{
	memset(elf, 0, sizeof(*elf));

	elf->path = image;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "load_kernel: Could not initialise libelf\n");
		exit(1);
	}

	if ((elf->fd = open(elf->path , O_RDONLY, 0)) < 0) {
		fprintf(stderr, "load_kernel: Open of %s failed: %s\n", elf->path,
			strerror(errno));
		exit(1);
	}

	elf->e = elf_begin(elf->fd, ELF_C_READ, NULL);
	if (elf->e == NULL) {
		fprintf(stderr, "load_kernel: elf_begin failed: %s", elf_errmsg(-1));
		exit(1);
	}

	if (elf_kind(elf->e) != ELF_K_ELF) {
		fprintf(stderr, "load_kernel: %s is not a valid ELF file\n", elf->path);
		exit(1);
	}

	if (gelf_getehdr(elf->e , &elf->ehdr) == NULL) {
		fprintf(stderr, "load_kernel: gelf_getehdr failed: %s", elf_errmsg(-1));
		exit(1);
	}

	if (elf->ehdr.e_type != ET_EXEC && elf->ehdr.e_type != ET_DYN) {
		fprintf(stderr, "load_kernel: %s is not a valid executable\n", elf->path);
		exit(1);
	}

	if (elf_getphdrnum(elf->e, &elf->ph_count) != 0) {
		fprintf(stderr, "load_kernel: elf_getphdrnum failed: %s", elf_errmsg(-1));
		exit(1);
	}
}

static void elf_close(struct elf_image *elf)
{
		elf_end(elf->e);
		elf->e = 0;

		close(elf->fd);
		elf->fd = 0;
}

#if 0
static unsigned long reserve_kernel(struct elf_image *elf, struct free_map *map)
{
	size_t i;
	GElf_Phdr phdr;
	unsigned long start = ULONG_MAX;
	unsigned long end = 0;
	unsigned long kernel_size;

	/* First work out how much memory we need to reserve */
	for (i = 0; i < elf->ph_count; i++) {
		if (gelf_getphdr(elf->e , i, &phdr) != &phdr) {
			fprintf(stderr, "load_kernel: elf_getphdr failed %s",
				elf_errmsg(-1));
			exit(1);
		}

		/* Make sure we aren't trying to load a normal executable */
		if (phdr.p_type == PT_INTERP) {
			fprintf(stderr, "load_kernel: %s requires an ELF interpreter\n",
				elf->path);
			exit(1);
		}

		if (phdr.p_type == PT_LOAD) {
			unsigned long paddr = phdr.p_paddr;
			unsigned long memsize = phdr.p_memsz;

			if (paddr < start)
				start = paddr;

			if (paddr + memsize > end)
				end = paddr + memsize;
		}
	}

	kernel_size = end - start;

	/* Round up to nearest 64kB page */
	kernel_size = ALIGN_UP(kernel_size, PAGE_SIZE_64K);

	return simple_alloc_low(map, kernel_size, PAGE_SIZE_64K);
}

static void load_kernel_image(const struct elf_image *elf, uint64_t entry)
{
	size_t i;
	GElf_Phdr phdr;

	kernel_addr = entry;

	for (i = 0; i < elf->ph_count; i++) {
		if (gelf_getphdr(elf->e , i, &phdr) != &phdr) {
			fprintf(stderr, "load_kernel: elf_getphdr failed: %s",
				elf_errmsg(-1));
			exit(1);
		}

		if (phdr.p_type == PT_LOAD) {
			void *p;
			unsigned long offset = phdr.p_offset;
			unsigned long paddr = phdr.p_paddr;
			unsigned long size = phdr.p_filesz;
			unsigned long memsize = phdr.p_memsz;
			unsigned long kernel_entry = 0;
			size_t ret;

			debug_printf("kernel offset 0x%lx paddr 0x%lx "
				"filesz %ld memsz %ld\n", offset, paddr,
				size, memsize);

			p = malloc(size);
			if (!p) {
				fprintf(stderr, "load_kernel: malloc of %ld bytes "
					"failed: %s\n", size, strerror(errno));
				exit(1);
			}

			if (!kernel_current_addr)
				kernel_current_addr = p;

			lseek(elf->fd, offset, SEEK_SET);

			ret = read(elf->fd, p, size);
			if (size != ret) {
				fprintf(stderr, "load_kernel: read of %lu bytes "
					"returned %zu: %s\n", size, ret, strerror(errno));
				exit(1);
			}

			memsize = ALIGN_UP(memsize, PAGE_SIZE_64K);

			add_kexec_segment("kernel", p, size,
					  (void *)(kernel_addr + paddr - start),
					  memsize);

			/* Parse function descriptor for ELFv1 kernels */
			if ((ehdr.e_flags & 3) == 2)
				kernel_entry = ehdr.e_entry;
			else {
				kernel_entry = get_entry_addr(e, ehdr, ehdr.e_entry) - phdr.p_vaddr;
				debug_printf("Kernel entry: 0x%lx\n", kernel_entry);
			}
			kernel_addr += kernel_entry;
		}
	}

	elf_end(e);
	close(fd);
}
#endif

static void load_kernel(struct free_map *map, struct elf_image *elf)
{
	size_t i;
	GElf_Phdr phdr;
	unsigned long start = ULONG_MAX;
	unsigned long dest;
	unsigned long kernel_size;

	kernel_size = arch_kernel_size(elf);

	if (!kernel_size)
		exit(1);

	dest = simple_alloc_low(map, kernel_size, PAGE_SIZE_64K);

	/* We enter at the start of the kernel */
	kernel_addr = dest;

	for (i = 0; i < elf->ph_count; i++) {
		if (gelf_getphdr(elf->e , i, &phdr) != &phdr) {
			fprintf(stderr, "load_kernel: elf_getphdr failed: %s", elf_errmsg(-1));
			exit(1);
		}

		if (phdr.p_type == PT_LOAD) {
			void *p;
			unsigned long offset = phdr.p_offset;
			unsigned long paddr = phdr.p_paddr;
			unsigned long size = phdr.p_filesz;
			unsigned long memsize = phdr.p_memsz;
			size_t ret;

			debug_printf("kernel offset 0x%lx paddr 0x%lx "
				"filesz %ld memsz %ld\n", offset, paddr,
				size, memsize);

			p = malloc(size);
			if (!p) {
				fprintf(stderr, "load_kernel: malloc of %ld bytes "
					"failed: %s\n", size, strerror(errno));
				exit(1);
			}

			if (!kernel_current_addr)
				kernel_current_addr = p;

			lseek(elf->fd, offset, SEEK_SET);

			ret = read(elf->fd, p, size);
			if (size != ret) {
				fprintf(stderr, "load_kernel: read of %lu bytes "
					"returned %zu: %s\n", size, ret, strerror(errno));
				exit(1);
			}

			memsize = ALIGN_UP(memsize, PAGE_SIZE_64K);

			add_kexec_segment("kernel", p, size,
					  (void *)(dest + paddr - start),
					  memsize);
		}
	}
}

static void *dtc_resize(void *p, unsigned long size)
{
	void *fdt;
	int ret;
	int i;

	fdt = malloc(size);
	if (!fdt) {
		fprintf(stderr, "fdt_resize: malloc of %ld bytes failed: %s\n",
			size, strerror(errno));
	}

	ret = fdt_open_into(p, fdt, size);
	if (ret < 0) {
		FDT_ERROR("fdt_open_into", ret);
		exit(1);
	}

	/* Clear out reservation map */
	for (i = 0; i < fdt_num_mem_rsv(fdt); i++)
		fdt_del_mem_rsv(fdt, i);

	return fdt;
}

static void *initialize_fdt(char *name)
{
	int fd;
	struct stat st;
	int ret;
	void *p;
	unsigned long size;
	void *fdt;

	fd = open(name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "initialize_fdt: open of %s failed: %s\n",
			name, strerror(errno));
		exit(1);
	}

	if (fstat(fd, &st) == -1) {
		fprintf(stderr, "initialize_fdt: stat of %s failed: %s\n",
			name, strerror(errno));
		exit(1);
	}

	p = malloc(st.st_size);
	if (!p) {
		fprintf(stderr, "initialize_fdt: malloc of %ld bytes failed: %s\n",
			st.st_size, strerror(errno));
	}

	ret = read(fd, p, st.st_size);
	if (ret != st.st_size) {
		fprintf(stderr, "initialize_fdt: read of %s returned %d: %s\n",
			name, ret, strerror(errno));
		exit(1);
	}

	close(fd);

	/* Give us a buffer for making changes to the device tree */
	size = st.st_size;
	size += DEVICE_TREE_PAD;

	fdt = dtc_resize(p, size);
	free(p);

	return fdt;
}

static void *fdt_from_fs(void)
{
	int fds[2];
	pid_t pid;
	void *dtb;
	void *fdt;
	int ret;
	int size;
	siginfo_t info;

	if (pipe(fds) == -1) {
		perror("pipe");
		exit(1);
	}

	pid = fork();

	if (pid == -1) {
		perror("fork");
		exit(1);
	}

	if (!pid) {
		close(STDOUT_FILENO);
		if (!debug)
			close(STDERR_FILENO);
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		execlp("dtc", "dtc", "-I", "fs", "-O", "dtb", "-s", "-R", 
			RESERVED_REGIONS, PROC_DEVICE_TREE, NULL);
		exit(255);
	}

	close(fds[1]);

#define MAX_DTB_SIZE 8 * 1024 * 1024
	dtb = malloc(MAX_DTB_SIZE);
	if (!dtb) {
		perror("malloc");
		exit(1);
	}

	memset(dtb, 0, MAX_DTB_SIZE);

	size = 0;
	while (1) {
		ret = read(fds[0], dtb + size, MAX_DTB_SIZE-size);
		if (ret == -1) {
			perror("read");
			exit(1);
		}

		size += ret;

		if (ret == 0)
			break;
	}

	close(fds[0]);

	if (waitid(P_PID, pid, &info, WEXITED)) {
		perror("waitpid");
		exit(1);
	}

	if (info.si_status == 255) {
		fprintf(stderr, "Could not execute dtc\n");
		exit(1);
	} else if (info.si_status) {
		fprintf(stderr, "dtc returned %d\n", info.si_status);
		exit(1);
	}

	/* Give us a buffer for making changes to the device tree */
	fdt = dtc_resize(dtb, size + DEVICE_TREE_PAD);
	free(dtb);

	return fdt;
}

static void load_initrd(struct free_map *map, char *name)
{
	int fd;
	struct stat st;
	void *p;
	unsigned long size;
	size_t ret;
	unsigned long memsize;
	unsigned long dest;

	fd = open(name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "load_initrd: open of %s failed: %s\n",
			name, strerror(errno));
		exit(1);
	}

	if (fstat(fd, &st) == -1) {
		fprintf(stderr, "load_initrd: stat of %s failed: %s\n",
			name, strerror(errno));
		exit(1);
	}

	size = st.st_size;

	p = malloc(size);
	if (!p) {
		fprintf(stderr, "load_initrd: malloc of %ld bytes failed: %s\n",
			size, strerror(errno));
	}

	ret = read(fd, p, size);
	if (ret != size) {
		fprintf(stderr, "load_initrd: read of %s returned %zu: %s\n",
			name, ret, strerror(errno));
		exit(1);
	}

	close(fd);

	memsize = ALIGN_UP(size, PAGE_SIZE_64K);
	dest = simple_alloc_low(map, memsize, PAGE_SIZE_64K);

	initrd_start = dest;
	initrd_end = dest + size;

	add_kexec_segment("initrd", p, size, (void *)dest, memsize);
}

static void update_cmdline(void *fdt, char *cmdline)
{
	int nodeoffset;
	int ret;

	nodeoffset = fdt_path_offset(fdt, "/chosen");
	if (nodeoffset < 0) {
		FDT_ERROR("fdt_path_offset /chosen", nodeoffset);
		exit(1);
	}

	ret = fdt_setprop(fdt, nodeoffset, "bootargs", cmdline, strlen(cmdline) + 1);
	if (ret < 0) {
		FDT_ERROR("fdt_setprop bootargs", ret);
		exit(1);
	}
}

static void load_fdt(struct free_map *map, void *fdt, int update_initrd)
{
	unsigned long size;
	unsigned long memsize;
	unsigned long dest;
	uint64_t val64;
	int ret;

	if (update_initrd) {
		int nodeoffset;

		/* Fix up the initrd start and end properties */
		nodeoffset = fdt_path_offset(fdt, "/chosen");
		if (nodeoffset < 0) {
			FDT_ERROR("fdt_path_offset /chosen", nodeoffset);
			exit(1);
		}

		val64 = initrd_start;
		ret = fdt_setprop_u64(fdt, nodeoffset, "linux,initrd-start", val64);
		if (ret < 0) {
			FDT_ERROR("fdt_setprop linux,initrd-start", ret);
			exit(1);
		}

		val64 = initrd_end;
		ret = fdt_setprop_u64(fdt, nodeoffset, "linux,initrd-end", val64);
		if (ret < 0) {
			FDT_ERROR("fdt_setprop linux,initrd-end", ret);
			exit(1);
		}
	}

	ret = fdt_pack(fdt);
	if (ret) {
		FDT_ERROR("fdt_pack returned", ret);
		exit(1);
	}

	size = fdt_totalsize(fdt);
	memsize = ALIGN_UP(size, PAGE_SIZE_64K);

	dest = simple_alloc_high(map, memsize, PAGE_SIZE_64K);

	device_tree_addr = dest;

	add_kexec_segment("device tree", fdt, size, (void *)dest, memsize);
}


static int shutdown_interfaces(void)
{
	struct if_nameindex *ifn;
	int skt;

	ifn = if_nameindex();
	if (!ifn) {
		perror("shutdown_interfaces: if_nameindex");
		return -1;
	}

	skt = socket(AF_INET, SOCK_DGRAM, 0);
	if (skt == -1) {
		perror("shutdown_interfaces: socket");
		exit(1);
	}

	while (ifn->if_index) {
		struct ifreq ifr;

		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, ifn->if_name);

		if (ioctl(skt, SIOCGIFFLAGS, &ifr) == -1) {
			fprintf(stderr, "shutdown_interfacess: SIOCGIFFLAGS ");
			perror(ifn->if_name);
			ifn++;
			continue;
		}

		if ((ifr.ifr_flags & IFF_LOOPBACK) ||
		    !(ifr.ifr_flags & IFF_UP)) {
			ifn++;
			continue;
		}

		ifr.ifr_flags &= ~(IFF_UP);
		if (ioctl(skt, SIOCSIFFLAGS, &ifr) == -1) {
			fprintf(stderr, "shutdown_interfacess: SIOCSIFFLAGS ");
			perror(ifn->if_name);
			ifn++;
			continue;
		}

		ifn++;
	}

	return 0;
}

static long syscall_kexec_load(unsigned long entry, unsigned long nr_segments,
			       struct kexec_segment *segments)
{
	return syscall(__NR_kexec_load, entry, nr_segments, segments,
		       KEXEC_ARCH_SYSCALL);
}

static int debug_kexec_load(void)
{
	int i;
	int ret;

	/*
	 * First see if the kexec syscall is available and we have
	 * permission to use it.
	 */
	ret = syscall_kexec_load(kexec_load_addr, 0, kexec_segments);
	if (ret) {
		perror("kexec syscall failed");
		exit(1);
	}

	for (i = 1; i <= kexec_segment_nr; i++) {
		ret = syscall_kexec_load(kexec_load_addr, i, kexec_segments);

		if (ret) {
			fprintf(stderr, "kexec_load failed on segment %d: %s\n",
				i, strerror(errno));
			dump_segments(stderr, " ");
			syscall_kexec_load(0, 0, NULL);
			exit(1);
		}
	}

	return 0;
}

static int kexec_load(void)
{
	int ret;

	ret = syscall_kexec_load(kexec_load_addr, kexec_segment_nr,
				 kexec_segments);

	if (ret)
		ret = debug_kexec_load();

	return ret;
}

/*
 * Try and set our affinity to the lowest online thread. We want to avoid
 * kexecing on a secondary thread.
 */
static int set_affinity(void)
{
	cpu_set_t cpuset;
	unsigned int lowest;

	CPU_ZERO(&cpuset);

	if (sched_getaffinity(0, sizeof(cpuset), &cpuset)) {
		perror("sched_getaffinity");
		return -1;
	}

	for (lowest = 0; lowest < sizeof(cpuset) * 8; lowest++) {
		if (CPU_ISSET(lowest, &cpuset))
			break;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(lowest, &cpuset);

	if (sched_setaffinity(0, sizeof(cpuset), &cpuset)) {
		perror("sched_setaffinity");
		return -1;
	}

	return 0;
}

static void exec_kexec(void)
{
	reboot(LINUX_REBOOT_CMD_KEXEC);
	fprintf(stderr, "kexec reboot failed: %s\n", strerror(errno));
	exit(1);
}

static void usage(void)
{
	printf(
"kexec (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
"Usage: kexec [options] image\n"
"	-b|--devicetreeblob|--dtb\n"
"	-c|--command-line|--append\n"
"	-d|--debug\n"
"	-e|--exec\n"
"	-f|--force\n"
"	-h|--help\n"
"	-i|--initrd|--ramdisk\n"
"	-l|--load\n"
"	-u|--unload\n"
"	-v|--version\n"
"Send bug reports to " PACKAGE_BUGREPORT "\n");
}

int main(int argc, char *argv[])
{
	char *initrd = NULL;
	char *cmdline = NULL;
	char *devicetreeblob = NULL;
	char *kernel = NULL;
	struct free_map *map;
	int exec = 0;
	int load = 0;
	int unload = 0;
	int force = 0;
	struct option long_options[] = {
		{"devicetreeblob", required_argument, 0, 'b' },
		{"dtb", required_argument, 0, 'b' },
		{"append", required_argument, 0, 'c' },
		{"command-line", required_argument, 0, 'c' },
		{"debug", 0, 0, 'd' },
		{"exec", 0, 0, 'e' },
		{"force", 0, 0, 'f' },
		{"help", 0, 0, 'h' },
		{"initrd", required_argument, 0, 'i' },
		{"ramdisk", required_argument, 0, 'i' },
		{"load", 0, 0, 'l' },
		{"unload", 0, 0, 'u' },
		{"version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};
	void *fdt;

	while (1) {
		signed char c = getopt_long(argc, argv, "b:c:defhi:luv", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'b':
			devicetreeblob = optarg;
			break;

		case 'c':
			cmdline = optarg;
			break;

		case 'd':
			debug = 1;
			break;

		case 'e':
			exec = 1;
			break;

		case 'f':
			force = 1;
			break;

		case 'h':
		default:
			usage();
			exit(1);
		case 'i':
			initrd = optarg;
			break;

		case 'l':
			load = 1;
			break;

		case 'u':
			unload = 1;
			break;

		case 'v':
			printf(VERSION "\n");
			exit(1);
		}
	}

	if ((load || exec) && unload) {
		usage();
		exit(1);
	}

	if (load) {
		if (optind < argc) {
			kernel = argv[optind++];
		} else {
			usage();
			exit(1);
		}
	} else {
		if (optind != argc) {
			usage();
			exit(1);
		}
	}

	if (load) {
		struct elf_image elf;
		
		if (devicetreeblob)
			fdt = initialize_fdt(devicetreeblob);
		else
			fdt = fdt_from_fs();

		map = simple_init();

		if (!map)
			exit(1);

		arch_fill_map(map, fdt);

		if (debug) {
			debug_printf("free memory map:\n");
			simple_dump_free_map(map);
		}

		elf_init(&elf, kernel);

		if (arch_check_elf(&elf))
			exit(1);

		load_kernel(map, &elf);

		elf_close(&elf);

		arch_reserve_regions(map, fdt, 0);

		if (initrd)
			load_initrd(map, initrd);

		if (cmdline)
			update_cmdline(fdt, cmdline);

		load_fdt(map, fdt, 1);
		arch_load_extra(map);

		kexec_load();

		if (debug) {
			debug_printf("free memory map after loading:\n");
			simple_dump_free_map(map);
		}
	}

	if (exec) {
		set_affinity();

		if (force) {
			shutdown_interfaces();
			sync();
			exec_kexec();
		} else {
			execlp("shutdown", "shutdown", "-r", "now", NULL);
		}

		return -1;
	}

	if (unload)
		kexec_load();

	return 0;
}
