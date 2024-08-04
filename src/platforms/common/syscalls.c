/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include "general.h"
#include "platform.h"
#include "usb_serial.h"
#if ENABLE_DEBUG != 1
#include <sys/stat.h>
#include <string.h>

typedef struct stat stat_s;
#endif
#include <errno.h>

extern uint8_t heap_start;
extern uint8_t heap_end;
static uint8_t *heap_current = &heap_start;

#if ENABLE_DEBUG == 1
/*
 * newlib defines _write as a weak link'd function for user code to override.
 *
 * This function forms the root of the implementation of a variety of functions
 * that can write to stdout/stderr, including printf().
 *
 * The result of this function is the number of bytes written.
 */
/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _write(const int file, const void *const ptr, const size_t len)
{
	(void)file;
#ifdef PLATFORM_HAS_DEBUG
	if (debug_bmp)
		return debug_serial_debug_write(ptr, len);
#else
	(void)ptr;
#endif
	return len;
}

/*
 * newlib defines isatty as a weak link'd function for user code to override.
 *
 * The result of this function is always 'true'.
 */
__attribute__((used)) int isatty(const int file)
{
	(void)file;
	return true;
}

#define RDI_SYS_OPEN 0x01U

typedef struct ex_frame {
	uint32_t r0;
	const uint32_t *params;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uintptr_t lr;
	uintptr_t return_address;
} ex_frame_s;

/*
 * This implements the other half of the newlib syscall puzzle.
 * When newlib is built for ARM, various calls that do file IO
 * such as printf end up calling [_swiwrite](https://github.com/mirror/newlib-cygwin/blob/master/newlib/libc/sys/arm/syscalls.c#L317)
 * and other similar low-level implementation functions. These
 * generate `swi` instructions for the "RDI Monitor" and that lands us.. here.
 *
 * The RDI calling convention sticks the file number in r0, the buffer pointer in r1, and length in r2.
 * ARMv7-M's SWI (SVC) instruction then takes all that and maps it into an exception frame on the stack.
 */
void debug_monitor_handler(void)
{
	ex_frame_s *frame;
	__asm__("mov %[frame], sp" : [frame] "=r"(frame));

	/* Make sure to return to the instruction after the SWI/BKPT */
	frame->return_address += 2U;

	switch (frame->r0) {
	case RDI_SYS_OPEN:
		frame->r0 = 1;
		break;
	default:
		frame->r0 = UINT32_MAX;
	}
	__asm__("bx lr");
}
#else
/* This defines stubs for the newlib fake file IO layer for compatibility with GCC 12 `-specs=nosys.specs` */

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _write(const int file, const void *const buffer, const size_t length)
{
	(void)file;
	(void)buffer;
	return length;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _read(const int file, void *const buffer, const size_t length)
{
	(void)file;
	(void)buffer;
	return length;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) off_t _lseek(const int file, const off_t offset, const int direction)
{
	(void)file;
	(void)offset;
	(void)direction;
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _fstat(const int file, stat_s *stats)
{
	(void)file;
	memset(stats, 0, sizeof(*stats));
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _isatty(const int file)
{
	(void)file;
	return true;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _close(const int file)
{
	(void)file;
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) pid_t _getpid(void)
{
	return 1;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _kill(const int pid, const int signal)
{
	(void)pid;
	(void)signal;
	return 0;
}
#endif

__attribute__((used)) void *_sbrk(const ptrdiff_t alloc_size)
{
	/* Check if this allocation would exhaust the heap */
	if (heap_current + alloc_size > &heap_end) {
		errno = ENOMEM;
		return (void *)-1;
	}

	/*
	 * Everything is ok, so make a copy of the heap pointer to return then add the
	 * allocation to the heap pointer
	 */
	void *const result = heap_current;
	heap_current += alloc_size;
	return result;
}

#if ENABLE_DEBUG == 0
/* ARM EABI Personality functions for newlib-4.3.0 */
__attribute__((weak)) void __aeabi_unwind_cpp_pr0()
{
}

__attribute__((weak)) void __aeabi_unwind_cpp_pr1()
{
}

__attribute__((weak)) void __aeabi_unwind_cpp_pr2()
{
}

#else // ENABLE_DEBUG

#include <unwind.h> // GCC's internal unwinder, part of libgcc

/* Helper for -mpoke-function-name */
static const char *unwind_get_func_name(const _Unwind_Ptr address)
{
	/* Look backwards, before function start/base address */
	uint32_t flag_word = *(uint32_t *)(address - 4);
	/* Marker set, likely has valid string pointer */
	if ((flag_word & 0xff000000U) == 0xff000000U)
		return (const char *)(address - 4 - (flag_word & 0x00ffffffU));
	return "unknown";
}

/* Callback to log the formatted backtrace as it is walked */
_Unwind_Reason_Code trace_func(_Unwind_Context *ctx, void *d)
{
	int *depth = (int *)d;
	static _Unwind_Word lastPC = 0;
	const _Unwind_Word thisPC = _Unwind_GetIP(ctx);
	/* Current frame $PC is equal to virtual $PC: actual end of stack */
	if (thisPC == lastPC) {
		lastPC = 0;
		return _URC_END_OF_STACK;
	}
	const _Unwind_Word thisSP = _Unwind_GetGR(ctx, 13);
	const _Unwind_Word thisFP = _Unwind_GetGR(ctx, 7);
	const _Unwind_Ptr func_base = _Unwind_GetRegionStart(ctx);
	const char *func_name = unwind_get_func_name(func_base);
	const _Unwind_Word func_progress = thisPC - func_base;

	printf("\t#%d: %s@%08x+%u ($PC=%08x, $SP=%08x, $FP=%08x)\r\n", *depth, func_name, func_base, func_progress, thisPC,
		thisSP, thisFP);
	(*depth)++;
	/* Too deep, bail out */
	if (*depth > 256) {
		lastPC = 0;
		return _URC_END_OF_STACK;
	}

	lastPC = thisPC;
	return _URC_NO_REASON;
}

void print_backtrace_here(void)
{
	int depth = 0;
	_Unwind_Backtrace(&trace_func, &depth);
}

#include <signal.h>
__attribute__((noreturn)) void _exit(int status);

void abort(void)
{
	print_backtrace_here();
	raise(SIGABRT);
	_exit(1);
}

#endif //ENABLE_DEBUG
