/* SPDX-License-Identifier: MIT */
/* Just route to the libc errno header. The kernel version exposes a
 * superset (-ERESTARTSYS, -EREMOTEIO, …); for the chip-driver subset
 * we polyfill, libc's POSIX errnos are sufficient. */
#ifndef LINUXDVBKPI_LINUX_ERRNO_H
#define LINUXDVBKPI_LINUX_ERRNO_H

#include <errno.h>

/* Linux uses these in addition to POSIX. Map onto closest POSIX errno
 * so chip-driver source compiles unchanged. */
#ifndef EREMOTEIO
#define EREMOTEIO EIO
#endif
#ifndef ERESTARTSYS
#define ERESTARTSYS EINTR
#endif

#endif
