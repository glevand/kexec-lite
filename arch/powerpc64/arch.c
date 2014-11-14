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
