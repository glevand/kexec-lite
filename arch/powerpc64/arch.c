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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lite.h"

#include "kexec_memory_map.h"
#include "kexec_trampoline.h"
#include "simple_allocator.h"

int arch_check_elf(const char *image, const GElf_Ehdr *ehdr)
{
	if (ehdr->e_machine != EM_PPC64) {
		fprintf(stderr, "load_kernel: %s is not a 64bit PowerPC executable\n", image);
		return -1;
	}

	return 0;
}

void arch_memory_map(struct free_map *map, void *fdt, int reserve_initrd)
{
	uint64_t start, size, end;
	int nodeoffset;
	int lpar = 0;
	uint64_t mem_top;

	/* Work out if we are in LPAR mode */
	nodeoffset = fdt_path_offset(fdt, "/rtas");
	if (nodeoffset >= 0) {
		if (fdt_getprop(fdt, nodeoffset, "ibm,hypertas-functions", NULL))
			lpar = 1;
	}

	mem_top = fill_memory_map(map, fdt, lpar ? 0 : no_fixed_start);

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

	simple_alloc_at(map, start, end - start);

	/* Reserve the MMU hashtable in non LPAR mode */
	if (lpar == 0) {
		if (getprop_u64(fdt, nodeoffset, "linux,htab-base", &start) ||
		    getprop_u64(fdt, nodeoffset, "linux,htab-size", &size)) {
			fprintf(stderr, "Could not find linux,htab-base or "
				"linux,htab-size properties\n");
			exit(1);
		}

		if (start < mem_top)
			simple_alloc_at(map, start, size);
	}

	/* XXX FIXME: Reserve TCEs in map */

	if (new_style_reservation(map, fdt, reserve_initrd))
		return;

	/* Reserve the initrd if requested */
	if (reserve_initrd &&
            !getprop_u64(fdt, nodeoffset, "linux,initrd-start", &start) &&
	    !getprop_u64(fdt, nodeoffset, "linux,initrd-end", &end)) {

		if (start < mem_top)
			simple_alloc_at(map, start, end - start);
	}

	/* Reserve RTAS */
	nodeoffset = fdt_path_offset(fdt, "/rtas");
	if (nodeoffset > 0) {
		uint32_t rtas_start, rtas_size;

		if (getprop_u32(fdt, nodeoffset, "linux,rtas-base", &rtas_start)) {
			fprintf(stderr, "getprop linux,rtas-base failed\n");
			exit(1);
		}

		if (getprop_u32(fdt, nodeoffset, "rtas-size", &rtas_size)) {
			fprintf(stderr, "getprop rtas-size failed\n");
			exit(1);
		}

		simple_alloc_at(map, rtas_start, rtas_size);

		if (fdt_add_mem_rsv(fdt, rtas_start, rtas_size))
			perror("fdt_add_mem_rsv");
	}

	nodeoffset = fdt_path_offset(fdt, "/ibm,opal");
	if (nodeoffset > 0) {
		uint64_t opal_start, opal_size;

		if (getprop_u64(fdt, nodeoffset, "opal-base-address",
				&opal_start)) {
			fprintf(stderr, "getprop opal-base-address failed\n");
			exit(1);
		}

		if (getprop_u64(fdt, nodeoffset, "opal-runtime-size",
				&opal_size)) {
			fprintf(stderr, "getprop opal-runtime-size failed\n");
			exit(1);
		}

		simple_alloc_at(map, opal_start, opal_size);

		if (fdt_add_mem_rsv(fdt, opal_start, opal_size))
			perror("fdt_add_mem_rsv");
	}
}

void arch_load(struct free_map *map)
{
	unsigned long size;
	unsigned long memsize;
	unsigned long dest;
	void *p;

	size = __trampoline_end - __trampoline_start;
	memsize = ALIGN_UP(size, PAGE_SIZE_64K);

	p = malloc(size);
	if (!p) {
		fprintf(stderr, "malloc of %ld bytes failed: %s\n", size,
			strerror(errno));
	}

	memcpy(p, __trampoline_start, size);
	/*
	 * Copy the first 0x100 bytes from the final kernel
	 * except for the first instruction.
	 */
	memcpy(p+sizeof(int), kernel_current_addr+sizeof(int),
		0x100-sizeof(int));

	trampoline_set_kernel(p, kernel_addr);
	trampoline_set_device_tree(p, device_tree_addr);

	dest = simple_alloc_high(map, memsize, PAGE_SIZE_64K);

	kexec_load_addr = dest;

	add_kexec_segment("trampoline", p, size, (void *)dest, memsize);
}
