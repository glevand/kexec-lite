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
