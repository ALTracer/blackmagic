/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Denis Tolstov <tolstov_den@mail.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <errno.h>
#include <libopencm3/cm3/vector.h>

void *_sbrk(ptrdiff_t incr)
{
	static char *heap_end = NULL;
	char *prev_heap_end;

	/* Put heap base after .bss on first call */
	if (heap_end == NULL)
		heap_end = (char *)&_ebss;

	/* Retrieve current (Main) stack pointer */
	register const char *stack_ptr __asm__("sp");
	/* Avoid growing heap above current MSP. No other limits. */
	if (heap_end + incr > stack_ptr) {
#ifdef ENABLE_DEBUG
		_write(stdout, "_sbrk: Heap and stack collision\n", 32);
		abort();
#else
		errno = ENOMEM;
		return (void *)-1;
#endif
	}

	prev_heap_end = heap_end;
	heap_end += incr;
	return (void *)prev_heap_end;
}
