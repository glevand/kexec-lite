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

#define MEMORY_CAP (2UL * 1024 * 1024 * 1024)

static unsigned long mem_top = 0;

int arch_check_elf(const char *image, const GElf_Ehdr *ehdr)
{
	if (ehdr->e_machine != EM_PPC64) {
		fprintf(stderr, "load_kernel: %s is not a 64bit PowerPC executable\n", image);
		return -1;
	}

	return 0;
}

void arch_memory_map(void *fdt, int reserve_initrd)
{
	uint64_t start, size, end;
	int nodeoffset;
	int lpar = 0;

	kexec_map = simple_init();

	/* Work out if we are in LPAR mode */
	nodeoffset = fdt_path_offset(fdt, "/rtas");
	if (nodeoffset >= 0) {
		if (fdt_getprop(fdt, nodeoffset, "ibm,hypertas-functions", NULL))
			lpar = 1;
	}

	/* First find our memory */
	nodeoffset = fdt_path_offset(fdt, "/");
	if (nodeoffset < 0) {
		fprintf(stderr, "Device tree has no root node\n");
		exit(1);
	}

	while (1) {
		const char *name;
		int len;
		const fdt64_t *reg;

		nodeoffset = fdt_next_node(fdt, nodeoffset, NULL);
		if (nodeoffset < 0)
			break;

		name = fdt_get_name(fdt, nodeoffset, NULL);

		if (!name || strncmp(name, "memory", strlen("memory")))
			continue;

		reg = fdt_getprop(fdt, nodeoffset, "reg", &len);

		while (len) {
			start = fdt64_to_cpu(*reg++);
			size = fdt64_to_cpu(*reg++);
			len -= 2 * sizeof(uint64_t);

			if (lpar == 1) {
				/* Only use the RMA region for LPAR */
				if (start == 0) {
					if (size > MEMORY_CAP)
						size = MEMORY_CAP;
					simple_free(kexec_map, 0, size);
					mem_top = size;
				}
			} else {
				if (start >= MEMORY_CAP)
					continue;

				if ((start + size) > MEMORY_CAP)
					size = MEMORY_CAP - start;

				simple_free(kexec_map, start, size);

				if ((start + size) > mem_top)
					mem_top = start + size;
			}
		}
	}

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

	simple_alloc_at(kexec_map, start, end - start);

	/* Reserve the MMU hashtable in non LPAR mode */
	if (lpar == 0) {
		if (getprop_u64(fdt, nodeoffset, "linux,htab-base", &start) ||
		    getprop_u64(fdt, nodeoffset, "linux,htab-size", &size)) {
			fprintf(stderr, "Could not find linux,htab-base or "
				"linux,htab-size properties\n");
			exit(1);
		}

		if (start < mem_top)
			simple_alloc_at(kexec_map, start, size);
	}

	/* XXX FIXME: Reserve TCEs in kexec_map */

	if (new_style_reservation(fdt, reserve_initrd))
		return;

	/* Reserve the initrd if requested */
	if (reserve_initrd &&
            !getprop_u64(fdt, nodeoffset, "linux,initrd-start", &start) &&
	    !getprop_u64(fdt, nodeoffset, "linux,initrd-end", &end)) {

		if (start < mem_top)
			simple_alloc_at(kexec_map, start, end - start);
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

		simple_alloc_at(kexec_map, rtas_start, rtas_size);

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

		simple_alloc_at(kexec_map, opal_start, opal_size);

		if (fdt_add_mem_rsv(fdt, opal_start, opal_size))
			perror("fdt_add_mem_rsv");
	}
}

void arch_load(void)
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

	dest = simple_alloc_high(kexec_map, memsize, PAGE_SIZE_64K);

	kexec_load_addr = dest;

	add_kexec_segment("trampoline", p, size, (void *)dest, memsize);
}
