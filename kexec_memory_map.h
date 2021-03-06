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

#ifndef KEXEC_MEMORY_MAP_H
#define KEXEC_MEMORY_MAP_H

static const uint64_t no_fixed_start = UINT64_MAX;
static const uint64_t no_memory_cap = UINT64_MAX;

void fill_memory_map(struct free_map *map, void *fdt, uint64_t memory_cap,
	uint64_t fixed_start);

int getprop_u32(const void *fdt, int nodeoffset, const char *name,
	uint32_t *val);
int getprop_u64(const void *fdt, int nodeoffset, const char *name,
	uint64_t *val);
int new_style_reservation(struct free_map *map, void *fdt, int reserve_initrd);

#endif
