/*
 * ARM64 binary image support.
 * Copyright (C) 2014 Linaro.
 */

#if !defined(__ARM64_IMAGE_HEADER_H)
#define __ARM64_IMAGE_HEADER_H

#if !defined(__KERNEL__)
#include <stdint.h>
#endif

#if !defined(__ASSEMBLY__)

/**
 * struct arm64_image_header - arm64 kernel image header.
 *
 * @pe_sig: Optional PE format 'MZ' signature.
 * @branch_code: Reserved for instructions to branch to stext.
 * @text_offset: The image load offset in LSB byte order.
 * @image_size: An estimated size of the memory image size in LSB byte order.
 * @flags: Bit flags:
 *  Bit 7.0: Image byte order, 1=MSB.
 * @reserved_1: Reserved.
 * @magic: Magic number, "ARM\x64".
 * @pe_header: Optional offset to a PE format header.
 **/

struct arm64_image_header {
	uint8_t pe_sig[2];
	uint16_t branch_code[3];
	uint64_t text_offset;
	uint64_t image_size;
	uint8_t flags[8];
	uint64_t reserved_1[3];
	uint8_t magic[4];
	uint32_t pe_header;
};

static const uint8_t arm64_image_magic[4] = {'A', 'R', 'M', 0x64U};
static const uint8_t arm64_image_pe_sig[2] = {'M', 'Z'};
static const uint64_t arm64_image_flag_7_be = 0x01U;

/**
 * arm64_header_check_magic - Helper to check the arm64 image header.
 *
 * Returns non-zero if header is OK.
 */

static inline int arm64_header_check_magic(const struct arm64_image_header *h)
{
	if (!h)
		return 0;

	if (!h->text_offset)
		return 0;

	return (h->magic[0] == arm64_image_magic[0]
		&& h->magic[1] == arm64_image_magic[1]
		&& h->magic[2] == arm64_image_magic[2]
		&& h->magic[3] == arm64_image_magic[3]);
}

/**
 * arm64_header_check_pe_sig - Helper to check the arm64 image header.
 *
 * Returns non-zero if 'MZ' signature is found.
 */

static inline int arm64_header_check_pe_sig(const struct arm64_image_header *h)
{
	if (!h)
		return 0;

	return (h->pe_sig[0] == arm64_image_pe_sig[0]
		&& h->pe_sig[1] == arm64_image_pe_sig[1]);
}

/**
 * arm64_header_check_msb - Helper to check the arm64 image header.
 *
 * Returns non-zero if the image was built as big endian.
 */

static inline int arm64_header_check_msb(const struct arm64_image_header *h)
{
	if (!h)
		return 0;

	return !!(h->flags[7] & arm64_image_flag_7_be);
}

#endif /* !defined(__ASSEMBLY__) */

#endif
