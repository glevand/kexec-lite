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
 */

#if !defined(KEXEC_LITE_H)
#define KEXEC_LITE_H

#include <gelf.h>
#include <libfdt.h>

#include "arch.h"
#include "simple_allocator.h"

#include <gelf.h>

/* Arch routines. */

#if !defined(KEXEC_ARCH_SYSCALL)
# error ERROR: KEXEC_ARCH_SYSCALL not defined.
#endif

struct elf_image {
	const char *path;
	int fd;
	Elf *e;
	GElf_Ehdr ehdr;
	size_t ph_count;
};

void arch_fill_map(struct free_map *map, void *fdt);
int arch_check_elf(const char *image, const struct elf_image *elf);
void arch_reserve_regions(struct free_map *map, void *fdt, int reserve_initrd);

void arch_load_extra(struct free_map *map);
//void arch_memory_map(struct free_map *map, void *fdt, int reserve_initrd);

/* Utility routines. */

#define PAGE_SIZE_64K		0x10000

#define ALIGN_UP(VAL, SIZE)	(((VAL) + (SIZE-1)) & ~(SIZE-1))
#define ALIGN_DOWN(VAL, SIZE)	((VAL) & ~(SIZE-1))

extern unsigned long kernel_addr;
extern void *kernel_current_addr;
extern unsigned long device_tree_addr;
extern unsigned long kexec_load_addr;

void add_kexec_segment(const char *type, void *buf, unsigned long bufsize, void *dest,
		       unsigned long memsize);

#if !defined(HAVE_FDT_SETPROP_U64)

#include <stdint.h>

typedef uint64_t fdt64_t;
typedef uint32_t fdt32_t;

static inline int fdt_setprop_u64(void *fdt, int nodeoffset, const char *name,
	uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_setprop(fdt, nodeoffset, name, &tmp, sizeof(tmp));
}

#endif /* !defined(HAVE_FDT_SETPROP_U64) */

#endif
