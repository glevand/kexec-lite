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

void arch_fill_map(struct free_map *map, void *fdt)
{
	return fill_map(map, fdt, no_memory_cap, no_fixed_start);
}

void arch_reserve_regions(struct free_map *map, void *fdt, int reserve_initrd)
{
	/* Reserve the kernel */
	//get load address from elf file
	//get kernel size from arm64 image header
	
	simple_alloc_at(map, start, end - start);

	/* Reserve the initrd  */
}

void arch_load(struct free_map *map)
{
	(void)map;

#warning WARNING: ARM64 not supported yet.
	assert(0 && "ARM64 not supported yet.");
}
