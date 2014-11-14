/*
 * arm64 arch support.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lite.h"

#include "kexec_memory_map.h"
#include "simple_allocator.h"

int arch_check_elf(const char *image, const GElf_Ehdr *ehdr)
{
	if (ehdr->e_machine != EM_AARCH64) {
		fprintf(stderr, "%s: %s is not an ARM64 executable\n", __func__,
			image);
		return -1;
	}
	return 0;
}

void arch_memory_map(struct free_map *map, void *fdt, int reserve_initrd)
{
	uint64_t start, size, end;
	int nodeoffset;
	uint64_t mem_top;

	mem_top = fill_memory_map(map, fdt, no_fixed_start);

	/* Reserve the kernel */
	nodeoffset = fdt_path_offset(fdt, "/chosen");
	if (nodeoffset < 0) {
		fprintf(stderr, "Device tree has no chosen node\n");
		exit(1);
	}

	/*
	 * XXX FIXME: Need to add linux,kernel-start property to the
	 * kernel to handle relocatable kernels.
	 */
	start = 0;
	if (getprop_u64(fdt, nodeoffset, "linux,kernel-end", &end)) {
		fprintf(stderr, "getprop linux,kernel-end failed\n");
		exit(1);
	}
}

void arch_load(struct free_map *map)
{
	(void)map;

#warning WARNING: ARM64 not supported yet.
	assert(0 && "ARM64 not supported yet.");
}
