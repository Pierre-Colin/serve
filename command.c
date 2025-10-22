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

#if __linux__
#include <linux/vm_sockets.h>
#include <linux/x25.h>
#endif

#include "command.h"

#define DEFAULT_PORT 4869

struct af_lookup {
	const char *str;
	sa_family_t af;
};

_Bool mknonblocking(int fildes);

static char *command;
static struct sockaddr *address;
static socklen_t address_len;
static int backlog = SOMAXCONN;
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
		"usage: %s [-a address] [-b backlog] [-t type] [-p protocol] "
	        "command\n",
		cmd);
}

#ifdef __GNUC__
__attribute__((nonnull (1, 2), pure))
#endif
static int compare(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

#ifdef __GNUC__
__attribute__((nonnull (1, 2)))
#endif
static sa_family_t getdomain(const char * restrict str,
                             const char ** const rem)
{
	/* Strings must be sorted lexicographically */
	static const struct af_lookup lookup[] = {
		{ "inet", AF_INET },
		{ "inet6", AF_INET6 },
		{ "unix", AF_UNIX }
		#if __linux__
		, { "vsock", AF_VSOCK },
		{"x25", AF_X25 }
		#endif
	};
	static const size_t num_af = sizeof lookup / sizeof lookup[0];
	const struct af_lookup *beg = lookup, *end = lookup + num_af - 1;
	size_t offset = 0;
	/* Invariant: for all entries x between beg and end, x.str matches
	   the initial value of str up to the offset */
	while (*str && *str != ' ' || beg < end) {
		const char c = *str == ' ' ? 0 : *str;
		if (c < beg->str[offset] || end->str[offset] < c)
			return AF_UNSPEC;
		if (beg->str[offset] < end->str[offset]) {
			/* Should be done by binary search, but lookup is too
			   small to properly test it */
			while (beg < end && beg->str[offset] < c) ++beg;
			while (beg < end && end->str[offset] > c) --end;
		} else if (beg->str[offset] == c) {
			++offset;
			++str;
		}
	}
	if (!beg->str[offset]) {
		*rem = *str == ' ' ? ++str : str;
		return beg->af;
	}
	return AF_UNSPEC;
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

#if __linux__
static int setaddressvsock(const char * const restrict str)
{
	struct sockaddr_vm * const addr = malloc(sizeof *addr);
	if (!addr)
		return -1;
	addr->svm_family = AF_VSOCK;
	addr->svm_reserved1 = 0;
	errno = 0;
	switch (sscanf(str, " %u %u", &addr->svm_port, &addr->svm_cid)) {
	case EOF:
		if (errno) {
			perror("Could not parse VSOCK address");
			free(addr);
			return -1;
		}
		free(addr);
		fputs("VSOCK address string has no data.\n", stderr);
		return 0;
	case 0:
		free(addr);
		fputs("Could not parse VSOCK address port number.\n", stderr);
		return 0;
	case 1:
		free(addr);
		fputs("Could not parse VSOCK context identifier.\n", stderr);
		return 0;
	case 2:
		memset(addr->svm_zero, 0, sizeof addr->svm_zero);
		address = (struct sockaddr *) addr;
		address_len = sizeof (struct sockaddr_vm);
		return 1;
	default:
		free(addr);
		fputs("Invalid sscanf return value.\n", stderr);
		abort();
	}
}

static int setaddressx25(const char * const restrict str)
{
	/* Validate the address */
	size_t sz;
	for (sz = 0; str[sz]; ++sz) {
		if (sz > 15) {
			fprintf(stderr,
			        "X25 address '%s' is too long.\n",
			        str);
			return 0;
		} else if (str[sz] < '0' || str[sz] > '9') {
			fprintf(stderr,
			        "X25 address '%s' has forbidden characters.\n",
			        str);
			return 0;
		}
	}

	/* Allocate and copy */
	struct sockaddr_x25 * const addr = malloc(sizeof addr);
	if (!addr)
		return -1;
	addr->sx25_family = AF_X25;
	memcpy(addr->sx25_addr.x25_addr, str, ++sz);
	address = (struct sockaddr *) addr;
	address_len = sizeof (struct sockaddr_x25);
	return 1;
}
#endif

static int setaddress(const char *addr)
{
	free(address);
	address = NULL;
	switch (getdomain(addr, &addr)) {
	case AF_INET:
		return setaddressinet(addr);
	case AF_INET6:
		return setaddressinet6(addr);
	case AF_UNIX:
		return setaddressunix(addr);
	#if __linux__
	case AF_VSOCK:
		return setaddressvsock(addr);
	case AF_X25:
		return setaddressx25(addr);
	#endif
	default:
		errno = ENOTSUP;
		return -1;
	}
}

static bool processopt(int c)
{
	switch (c) {
		char *end;
		long bl;
	case 'a':
		if ((c = setaddress(optarg)) < 0) {
			perror("Could not set listening address");
			exit(EXIT_FAILURE);
		}
		return c == 0;
	case 'b':
		bl = strtol(optarg, &end, 10);
		if (*end) {
			fprintf(stderr,
			        "Option -b argument '%s' is not an integer.\n",
			        optarg);
			return true;
		}
		backlog = bl < 0 ? 0 : bl > SOMAXCONN ? SOMAXCONN : bl;
		return false;
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
		type = c;
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
	while ((c = getopt(argc, argv, ":a:b:p:t:")) != -1)
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
	if (listen(listener, backlog) < 0) {
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
