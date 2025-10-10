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
#include <fcntl.h>
#include <locale.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "command.h"

#define DEFAULT_PORT 4869

_Bool mknonblocking(int fildes);

static char *command;
static struct sockaddr *address;
static socklen_t address_len;
static int type = SOCK_STREAM, protocol;
static int listener = -1;

static void cleanup()
{
	free(address);
	if (listener >= 0)
		close(listener);
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static void usage(const char * const restrict cmd)
{
	fprintf(stderr,
		"usage: %s [-a address] [-t type] [-p protocol] command\n",
		cmd);
}

#ifdef __GNUC__
__attribute__((nonnull (1, 2), const))
#endif
static int compare(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static sa_family_t getdomain(const char * const dom)
{
	/* s MUST be sorted lexicographically */
	static const char *s[] = {"inet", "inet6", "unix"};
	static const sa_family_t v[] = {AF_INET, AF_INET6, AF_UNIX};
	if (sizeof s / sizeof s[0] < sizeof v / sizeof v[0]) {
		fprintf(stderr,
			"Differing array sizes in function %s, in file %s\n",
			__func__, __FILE__);
		abort();
	}
	const char **x = bsearch(&dom, s, sizeof s / sizeof s[0],
		sizeof (const char *), compare);
	return x ? v[x - s] : AF_UNSPEC;
}

#ifdef __GNUC__
__attribute__((nonnull (1)))
#endif
static int gettype(const char * const type)
{
	/* s MUST be sorted lexicographically */
	static const char *s[] = {"seqpacket", "stream"};
	static const int v[] = {SOCK_SEQPACKET, SOCK_STREAM};
	if (sizeof s / sizeof s[0] < sizeof v / sizeof v[0]) {
		fprintf(stderr,
			"Differing array sizes in function %s, in file %s\n",
			__func__, __FILE__);
		abort();
	}
	const char **x = bsearch(&type, s, sizeof s / sizeof s[0],
		sizeof (const char *), compare);
	if (!x) {
		errno = ENOTSUP;
		return -1;
	}
	return v[x - s];
}

static int setaddressinet(const char * const restrict addrstr)
{
	address_len = sizeof (struct sockaddr_in);
	struct sockaddr_in * const addr = malloc(address_len);
	if (!addr)
		return -1;
	if (addrstr) {
		const char *c = strchr(addrstr, ' ');
		if (c == NULL) {
			free(addr);
			fprintf(stderr, "Invalid inet address '%s'\n",
				addrstr);
			return 0;
		}
		char *a = malloc(c - addrstr + 1);
		if (!a) {
			free(addr);
			return -1;
		}
		memcpy(a, addrstr, c - addrstr);
		a[c - addrstr] = 0;
		addr->sin_addr.s_addr = inet_addr(a);
		free(a);
		if (addr->sin_addr.s_addr == (in_addr_t) (-1)) {
			free(addr);
			fprintf(stderr, "Invalid inet address '%s'\n",
				addrstr);
			return 0;
		}
		errno = 0;
		const unsigned long p = strtoul(++c, &a, 10);
		if (*a || errno || p > 65535) {
			free(addr);
			fputs("Port number exceeds 65535\n", stderr);
			return 0;
		}
		addr->sin_family = AF_INET;
		addr->sin_port = htons(p);
	} else {
		addr->sin_family = AF_INET;
		addr->sin_port = htons(DEFAULT_PORT);
		addr->sin_addr.s_addr = INADDR_ANY;
	}
	address = (struct sockaddr *) addr;
	return 1;
}

static int setaddressinet6(const char * restrict addrstr)
{
	address_len = sizeof (struct sockaddr_in6);
	struct sockaddr_in6 * const addr = malloc(address_len);
	if (!addr)
		return -1;
	memset(addr, 0, address_len);
	addr->sin6_family = AF_INET6;
	if (addrstr) {
		const char * const comma = strchr(addrstr, ' ');
		if (!comma || comma - addrstr > 45) {
			free(addr);
			fprintf(stderr, "Invalid inet6 address '%s'\n",
				addrstr);
			return 0;
		}
		char as[46];
		memcpy(as, addrstr, comma - addrstr);
		as[comma - addrstr] = 0;
		const int r = inet_pton(AF_INET6, as, &addr->sin6_addr);
		if (r <= 0) {
			const int error = errno;
			free(addr);
			errno = error;
			return r;
		}
		addrstr += comma - addrstr + 1;
		uint_fast32_t port = 0;
		while (*addrstr) {
			if (*addrstr < '0' || *addrstr > '9') {
				free(addr);
				fputs("Port contains non-digit character\n",
					stderr);
				return 0;
			}
			port = 10 * port + *addrstr++ - '0';
			if (port > 65535) {
				free(addr);
				fputs("Port number exceeds 65535\n", stderr);
				return 0;
			}
		}
		addr->sin6_port = htons(port);
	} else {
		addr->sin6_port = htons(DEFAULT_PORT);
		addr->sin6_addr = in6addr_any;
	}
	address = (struct sockaddr *) addr;
	return 1;
}

static int setaddressunix(const char * const restrict addrstr)
{
	address_len = sizeof (struct sockaddr_un);
	struct sockaddr_un * const addr = malloc(address_len);
	if (!addr)
		return -1;
	addr->sun_family = AF_UNIX;
	if (addrstr) {
		const size_t path_len = strlen(addrstr);
		if (path_len >= sizeof addr->sun_path) {
			fprintf(stderr,
				"Unix socket path '%s' is too long.\n",
				addrstr);
			free(addr);
			return 0;
		}
		memcpy(addr->sun_path, addrstr, path_len + 1);
	} else {
		static const char default_path[] = "serve.sock";
		memcpy(addr->sun_path, default_path, sizeof default_path);
	}
	address = (struct sockaddr *) addr;
	return 1;
}

static int setaddress(const char * restrict addr)
{
	free(address);
	address = NULL;
	sa_family_t af;
	const char *s = strchr(addr, ' ');
	if (s) {
		char *domstr = malloc(s - addr + 1);
		if (!domstr)
			return -1;
		memcpy(domstr, addr, s - addr);
		domstr[s - addr] = 0;
		af = getdomain(domstr);
		free(domstr);
		addr = s + 1;
	} else {
		af = getdomain(addr);
		addr = NULL;
	}
	switch (af) {
	case AF_INET:
		return setaddressinet(addr);
	case AF_INET6:
		return setaddressinet6(addr);
	case AF_UNIX:
		return setaddressunix(addr);
	default:
		errno = ENOTSUP;
		return -1;
	}
}

static bool processopt(int c)
{
	switch (c) {
	case 'a':
		if ((c = setaddress(optarg)) < 0) {
			perror("Could not set listening address");
			exit(EXIT_FAILURE);
		}
		return c == 0;
	case 'p':
		fputs("Protocol specification unimplemented; using stream\n",
		      stderr);
		return false;
	case 't':
		if ((c = gettype(optarg)) < 0 && errno != 0) {
			fprintf(stderr, "Unsupported socket type '%s'\n",
				optarg);
			return true;
		}
		return false;
	case ':':
		fprintf(stderr, "Option -%c requires an operand\n",
			optopt);
		return true;
	default:
		fprintf(stderr, "Unrecognized option '-%c'\n", optopt);
		return true;
	}
}

#ifdef __GNUC__
__attribute__((nonnull (2)))
#endif
static void argparse(const int argc, char * const argv[])
{
	assert(command == NULL);
	if (argc < 2) {
		fputs("Missing operand\n", stderr);
		usage(argc > 0 ? argv[0] : "serve");
		exit(2);
	}
	atexit(cleanup);
	int c;
	bool error = false;
	while ((c = getopt(argc, argv, ":a:p:t:")) != -1)
		error |= processopt(c);
	if (!address && setaddressinet(NULL) < 0) {
		perror("Could not set default listening address");
		exit(EXIT_FAILURE);
	}
	if (optind >= argc) {
		fputs("Missing operand\n", stderr);
		error = true;
	} else if (optind < argc - 1) {
		fputs("Only one operand is expected\n", stderr);
		error = true;
	}
	if (error) {
		usage(argv[0]);
		exit(2);
	}
	command = argv[optind];
}

void cmdexec()
{
	assert(command != NULL);
	char *argv[4] = {"sh", "-c", command, NULL};
	execvp(argv[0], argv);
}

static void mklistener()
{
	assert(address != NULL);
	if ((listener = socket(address->sa_family, type, protocol)) < 0) {
		perror("Could not create listener socket");
		exit(EXIT_FAILURE);
	}
	if (bind(listener, address, address_len) < 0) {
		perror("Could not assign address to listener socket");
		exit(EXIT_FAILURE);
	}
	const int f = fcntl(listener, F_GETFD);
	if (f < 0)
		perror("Could not get listener socket descriptor flags");
	else if (fcntl(listener, F_SETFD, f | FD_CLOEXEC) < 0)
		perror("Could not set listener socket descriptor flags");
	if (listen(listener, SOMAXCONN) < 0) {
		perror("Could not mark listener as accepting connections");
		exit(EXIT_FAILURE);
	}
	if (!mknonblocking(listener))
		perror("Could not make listener socket nonblocking");
}

#ifdef __GNUC__
__attribute__((nonnull (2)))
#endif
void init(const int argc, char * const argv[])
{
	if (!setlocale(LC_ALL, "")) {
		fputs("Could not set the global locale.\n", stderr);
		exit(EXIT_FAILURE);
	}
	argparse(argc, argv);
	mklistener();
}

#ifdef __GNUC__
__attribute__((const))
#endif
int getlistener()
{
	return listener;
}
