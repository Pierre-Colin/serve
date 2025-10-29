/*
 * Copyright (C) 2025 Pierre Colin
 * This file is part of the serve utility.
 * 
 * The serve utility is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * The serve utility is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * the serve utility.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "qualfd.h"

static bool mknonblocking(const int fildes)
{
	const int flags = fcntl(fildes, F_GETFL);
	return flags < 0 || fcntl(fildes, F_SETFL, flags | O_NONBLOCK) < 0;
}

bool nbpipe(int fildes[const restrict static 2])
{
	/* The pipe2() function is inappropriate because it makes both file
	   descriptors nonblocking. */
	if (pipe(fildes) < 0)
		return true;
	if (mknonblocking(fildes[1])) {
		const int err = errno;
		close(fildes[0]);
		close(fildes[1]);
		errno = err;
		return true;
	}
	return false;
}

int qualsocket(const int domain, const int type, const int protocol)
{
	#if __linux__ || _POSIX_C_SOURCE >= 202405L
	return socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
	#else
	const int sock = socket(domain, type, protocol);
	if (sock < 0)
		return -1;
	if (mknonblocking(sock))
		perror("Could not make socket nonblocking");
	const int flags = fcntl(sock, F_GETFD);
	if (flags < 0 || fcntl(sock, F_SETFD, flags | FD_CLOEXEC) < 0)
		perror("Could not make socket close on execution");
	return sock;
	#endif
}
