/*
 * Copyright (C) 2023, 2025 Pierre Colin
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
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUF_LEN 512

#ifdef __GNUC__
__attribute__((nonnull (2)))
#endif
static char *serializeinetg(const int af, const void * const restrict address,
	const uint16_t port, const size_t bufsize)
{
	char * const addr = malloc(bufsize);
	if (!addr || !inet_ntop(af, address, addr, bufsize)) {
		free(addr);
		return NULL;
	}
	char * const s = strchr(addr, 0);
	assert(s != NULL);
	const int n = sprintf(s, " %" PRIu16, ntohs(port));
	char * const addr2 = realloc(addr, s - addr + n + 1);
	return addr2 ? addr2 : addr;
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static char *serializeinet(const struct sockaddr_in * const restrict address)
{
	return serializeinetg(AF_INET, &address->sin_addr, address->sin_port,
		INET_ADDRSTRLEN + 7);
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static char *serializeinet6(const struct sockaddr_in6 * const restrict address)
{
	return serializeinetg(AF_INET6, &address->sin6_addr,
		address->sin6_port, INET6_ADDRSTRLEN + 7);
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static char *serializeunix(const struct sockaddr_un * const restrict address)
{
	char * const path = malloc(sizeof address->sun_path);
	if (!path)
		return NULL;
	char * const p = stpcpy(path, address->sun_path);
	char * const path2 = realloc(path, p - path + 1);
	return path2 ? path2 : path;
}

#ifdef __GNUC__
__attribute__((nonnull (2)))
#endif
int acceptremote(const int socket, char ** const restrict address)
{
	assert(sizeof (struct sockaddr_in) <= BUF_LEN);
	assert(sizeof (struct sockaddr_in6) <= BUF_LEN);
	assert(sizeof (struct sockaddr_un) <= BUF_LEN);
	char buf[BUF_LEN];
	socklen_t length = BUF_LEN;
	const int fildes = accept(socket, (struct sockaddr *) buf, &length);
	if (fildes < 0)
		return -1;
	switch (((struct sockaddr *) buf)->sa_family) {
	case AF_INET:
		if (!(*address = serializeinet((struct sockaddr_in *) buf)))
			goto fail;
		return fildes;
	case AF_INET6:
		if (!(*address = serializeinet6((struct sockaddr_in6 *) buf)))
			goto fail;
		return fildes;
	case AF_UNIX:
		if (!(*address = serializeunix((struct sockaddr_un *) buf)))
			goto fail;
		return fildes;
	default:
		close(fildes);
		errno = ENOTSUP;
		return -1;
	}

	int error;
fail:
	error = errno;
	close(fildes);
	errno = error;
	return -1;
}
