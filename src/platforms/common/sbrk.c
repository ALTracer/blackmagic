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

static char *heap_end = NULL;

void *_sbrk(ptrdiff_t incr)
{
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

/* Determine current stack depth of MSP */
ptrdiff_t helper_stack_used(void)
{
	register const unsigned int *stack_ptr __asm__("sp");
	const unsigned int *stack_top = &_stack;
	return stack_top - stack_ptr;
}

#include "general.h"
#include "platform.h"

/*
 * Lockup if stack smashes top of heap.
 * Assuming this is called from periodic interrupt,
 * and there's no separate interrupt stack.
 */
void platform_check_stack_overflow(void)
{
	register const char *stack_ptr __asm__("sp");
	/* Cannot call _sbrk because it detects the collision */
	char *heap_watermark = heap_end; // = _sbrk(0);
	/* Assume heap must be below the stack, panic otherwise */
	if (heap_watermark < stack_ptr) {
		return;
	}
	DEBUG_ERROR("Stack overflows the heap (at %p)\n", stack_ptr);
	while (1) {
		gpio_toggle(LED_PORT, LED_IDLE_RUN);
	}
}
