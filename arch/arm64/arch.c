/*
 * arm64 arch support.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lite.h"

#include "image-header.h"
#include "kexec_memory_map.h"
#include "simple_allocator.h"

struct arch_arm64 {
	uint64_t text_offset;
	uint64_t image_size;
};

static struct arch_arm64 arch;

void arch_fill_map(struct free_map *map, void *fdt)
{
	fill_memory_map(map, fdt, no_memory_cap, no_fixed_start);
}

int arch_check_elf(const struct elf_image *elf)
{
	if (elf->ehdr.e_machine != EM_AARCH64) {
		fprintf(stderr, "%s: %s is not an ARM64 executable\n", __func__,
			elf->path);
		return -1;
	}
	return 0;
}

static int arm64_process_image_header(const struct arm64_image_header *h,
	uint64_t *text_offset, uint64_t *image_size)
{
#if !defined(KERNEL_IMAGE_SIZE)
# define KERNEL_IMAGE_SIZE (768 * 1024)
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
# define le64_to_cpu(val) (val)
#elif BYTE_ORDER == BIG_ENDIAN
# define le64_to_cpu(val) bswap_64(val)
#else
# error unknwon BYTE_ORDER
#endif

	if (!arm64_header_check_magic(h)) {
		*text_offset = *image_size = 0;
		return -EINVAL;
	}

	if (h->image_size) {
		*text_offset = le64_to_cpu(h->text_offset);
		*image_size = le64_to_cpu(h->image_size);
	} else {
		/* For 3.16 and older kernels. */
		*text_offset = 0x80000;
		*image_size = KERNEL_IMAGE_SIZE;
	}

	return 0;
}

uint64_t arch_kernel_size(const struct elf_image *elf)
{
	size_t i;
	int result;
	GElf_Phdr phdr;
	struct arm64_image_header h;

	for (i = 0; i < elf->ph_count; i++) {
		if (gelf_getphdr(elf->e , i, &phdr) != &phdr) {
			fprintf(stderr, "%s: elf_getphdr failed %s",
				__func__, elf_errmsg(-1));
			exit(1);
		}

		if (phdr.p_type == PT_INTERP) {
			fprintf(stderr, "%s: %s requires an ELF interpreter\n",
				__func__, elf->path);
			exit(1);
		}

		if (phdr.p_type != PT_LOAD)
			continue;

		lseek(elf->fd, phdr.p_offset, SEEK_SET);

		result = read(elf->fd, &h, sizeof(h));

		if (result != sizeof(h)) {
			fprintf(stderr, "%s: read header failed: %s\n",
				__func__, strerror(errno));
			exit(1);
		}

		result = arm64_process_image_header(&h, &arch.text_offset,
			&arch.image_size);
		
		if (result != sizeof(h)) {
			fprintf(stderr,
				"%s: arm64_process_image_header failed\n",
				__func__);
			exit(1);
		}
		
		return arch.image_size;
	}

	fprintf(stderr, "%s: bad elf file: %s\n", __func__, elf->path);
	exit(1);
}

void arch_reserve_regions(struct free_map *map, void *fdt, int reserve_initrd)
{
	(void)map;
	(void)fdt;
	(void)reserve_initrd;
}

void arch_load_extra(struct free_map *map)
{
	(void)map;

#warning WARNING: ARM64 not supported yet.
	assert(0 && "ARM64 not supported yet.");
}
