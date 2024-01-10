/****************************************************************************
 * include/debug.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_DEBUG_H
#define __INCLUDE_DEBUG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

//#include <nuttx/config.h>
#include "nuttx_compiler.h"

#ifdef CONFIG_ARCH_DEBUG_H
#include <arch/debug.h>
#endif
#ifdef CONFIG_ARCH_CHIP_DEBUG_H
#include <arch/chip/debug.h>
#endif

#include "syslog.h"
//#include <sys/uio.h>
#include <assert.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONFIG_DEBUG_ALERT
#define PANIC() assert(false)

#if !defined(EXTRA_FMT) && !defined(EXTRA_ARG) && defined(CONFIG_HAVE_FUNCTIONNAME)
#define EXTRA_FMT "%s: "
#define EXTRA_ARG , __FUNCTION__
#endif

#ifndef EXTRA_FMT
#define EXTRA_FMT
#endif

#ifndef EXTRA_ARG
#define EXTRA_ARG
#endif

/* Debug macros will differ depending upon if the toolchain supports
 * macros with a variable number of arguments or not.
 */

#ifdef CONFIG_CPP_HAVE_VARARGS
/* don't call syslog while performing the compiler's format check. */

#define _none(format, ...)                          \
	do {                                            \
		if (0)                                      \
			syslog(LOG_ERR, format, ##__VA_ARGS__); \
	} while (0)
#else
#define _none (void)
#endif

/* The actual logger function may be overridden in arch/debug.h if needed.
 * (Currently only if the pre-processor supports variadic macros)
 */

#ifndef __arch_syslog
#define __arch_syslog syslog
#endif

#if !defined(CONFIG_DEBUG_ALERT)
#define _alert _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#define _alert(format, ...) __arch_syslog(LOG_EMERG, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_ERROR)
#define _err _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#define _err(format, ...) __arch_syslog(LOG_ERR, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_WARN)
#define _warn _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#define _warn(format, ...) __arch_syslog(LOG_WARNING, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_INFO)
#define _info _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#define _info(format, ...) __arch_syslog(LOG_INFO, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif

/* The system logging interfaces are normally accessed via the macros
 * provided above.  If the cross-compiler's C pre-processor supports a
 * variable number of macro arguments, then those macros below will map all
 * debug statements to the logging interfaces declared in syslog.h.
 *
 * If the cross-compiler's pre-processor does not support variable length
 * arguments, then these additional APIs will be built.
 */

#ifndef CONFIG_CPP_HAVE_VARARGS
#ifdef CONFIG_DEBUG_ALERT
void _alert(const char *format, ...) sysloglike(1, 2);
#endif

#ifdef CONFIG_DEBUG_ERROR
void _err(const char *format, ...) sysloglike(1, 2);
#endif

#ifdef CONFIG_DEBUG_WARN
void _warn(const char *format, ...) sysloglike(1, 2);
#endif

#ifdef CONFIG_DEBUG_INFO
void _info(const char *format, ...) sysloglike(1, 2);
#endif
#endif /* CONFIG_CPP_HAVE_VARARGS */

#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_DEBUG_H */
