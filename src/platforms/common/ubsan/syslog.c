/*
 * syslog.c
 *
 *  Created on: Oct 19, 2022
 *      Author: mint-user
 */

#include <stdio.h>

#include "syslog.h"

uint8_t g_syslog_mask = LOG_ALL;

static char logbuf[192] = {0};

int nx_vsyslog(int priority, const char *fmt, va_list *ap) {

	int ret = vsprintf(logbuf, fmt, *ap);
	if (ret < 0) {
		;
	}
	return fputs(logbuf, stdout);
}

void vsyslog(int priority, const char *fmt, va_list ap) {
	/* Check if this priority is enabled */
	if ((g_syslog_mask & LOG_MASK(priority)) == 0) {
		/* No, skip logging */
		return;
	}

	/* NOTE:  The va_list parameter is passed by reference.
	 * That is because the va_list is a structure in some compilers and
	 * passing of structures in the NuttX syscalls does not work.
	 */

#ifdef va_copy
	va_list copy;

	va_copy(copy, ap);
	nx_vsyslog(priority, fmt, &copy);
	va_end(copy);
#else
	nx_vsyslog(priority, fmt, &ap);
#endif
}

void syslog(int priority, const char *fmt, ...) {
	va_list ap;

	/* Let vsyslog do the work */

	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
}

int setlogmask(int mask)
{
  uint8_t oldmask;

  oldmask       = g_syslog_mask;
  g_syslog_mask = (uint8_t)mask;

  return oldmask;
}
