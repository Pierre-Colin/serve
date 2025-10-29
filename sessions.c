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
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "command.h"
#include "qualfd.h"
#include "remote.h"
#include "sessions.h"

typedef struct {
	pid_t pid;
	char *ebuf;
	size_t nebuf, cebuf;
} ProcessData;

static ProcessData *processes;
static struct pollfd *fds;
static size_t nproc, cproc = 0;
static int mproc = INT_MAX;

void initmaxproc(int val)
{
	const long sysmax = sysconf(_SC_OPEN_MAX) - 2;
	if (sysmax > 0 && sysmax < val)
		val = sysmax;
	mproc = val;
}

static void cleanupprocesses()
{
	for (size_t i = 0; i < nproc; i++) {
		close(fds[i + 1].fd);
		free(processes[i].ebuf);
	}
}

static void cleanup(void)
{
	cleanupprocesses();
	free(processes);
	free(fds);
}

static bool allocproc()
{
	if (nproc < cproc)
		return false;
	size_t ns = cproc == 0 ? 1 :
		cproc >= SIZE_MAX / 2 ? SIZE_MAX :
		2 * cproc;
	if (ns >= SIZE_MAX / sizeof (ProcessData))
		ns = SIZE_MAX / sizeof (ProcessData) - 1;
	if (ns >= SIZE_MAX / sizeof (struct pollfd) - 1)
		ns = SIZE_MAX / sizeof (struct pollfd) - 2;
	if (ns <= cproc) {
		errno = ENOMEM;
		return true;
	}
	void *newptr = realloc(processes, ns * sizeof (ProcessData));
	if (!newptr)
		return true;
	processes = newptr;
	newptr = realloc(fds, (ns + 1) * sizeof (struct pollfd));
	if (!newptr)
		return true;
	fds = newptr;
	cproc = ns;
	return false;
}

static bool addproc(const int sock, const char * const restrict remote)
{
	int fd[2];
	if (nbpipe(fd) < 0)
		return true;
	if (allocproc())
		goto cleanup_pipe;
	if ((processes[nproc].pid = fork()) < 0)
		goto cleanup_pipe;
	if (processes[nproc].pid == 0) {
		cleanupprocesses();
		nproc = 0;
		if (setenv("REMOTE", remote, 1) < 0)
			perror("Could not set $REMOTE in child process");
		dup2(sock, STDIN_FILENO);
		dup2(sock, STDOUT_FILENO);
		dup2(fd[1], STDERR_FILENO);
		close(sock);
		close(fd[0]);
		close(fd[1]);
		cmdexec();
		perror("Could not start child process");
		abort();
	}
	close(fd[1]);
	processes[nproc].ebuf = NULL;
	processes[nproc].nebuf = processes[nproc].cebuf = 0;
	fds[nproc + 1].fd = fd[0];
	fds[nproc + 1].events = POLLIN;
	printf("Process %ju created (%s)\n",
		(uintmax_t) processes[nproc++].pid, remote);
	return false;

cleanup_pipe:
	close(fd[0]);
	close(fd[1]);
	return true;
}

static bool tryrmproc(const size_t p)
{
	int status;
	const pid_t pid = waitpid(processes[p].pid, &status, WNOHANG);
	if (pid <= 0)
		return false;
	/* TODO: try one last time to get input before flushing */
	if (processes[p].nebuf > 0) {
		fprintf(stderr, "%ju: %s\n", (uintmax_t) processes[p].pid,
		        processes[p].ebuf);
		processes[p].ebuf[0] = 0;
		processes[p].nebuf = 0;
	}
	close(fds[p + 1].fd);
	free(processes[p].ebuf);
	processes[p] = processes[nproc - 1];
	fds[p + 1] = fds[nproc--];
	printf("Process %ju exited (%d)\n", (uintmax_t) pid, status);
	return true;
}

static bool passprocerror(const size_t p)
{
	/* strictly-conforming upper bound for error line buffer */
	static const size_t max_cebuf = 65534;
	ProcessData * const proc = &processes[p];
	if (proc->nebuf + 128 > proc->cebuf) {
		if (proc->nebuf > max_cebuf - 128) {
			errno = ENOMEM;
			return true;
		}
		proc->cebuf = proc->nebuf + 128;
		char * const newbuf = realloc(proc->ebuf, proc->cebuf + 1);
		if (!newbuf)
			return true;
		proc->ebuf = newbuf;
	}
	char * const resume = proc->ebuf + proc->nebuf;
	const ssize_t n = read(fds[p + 1].fd, resume, 128);
	if (n < 0)
		return true;
	resume[n] = 0;
	proc->nebuf += n;
	char *lf = strchr(resume, '\n');
	while (lf) {
		*lf = 0;
		printf("%ju: %s\n", (uintmax_t) proc->pid, proc->ebuf);
		proc->nebuf -= lf - proc->ebuf + 1;
		memmove(proc->ebuf, lf + 1, proc->nebuf + 1);
		lf = strchr(proc->ebuf, '\n');
	}
	return false;
}

#ifdef __GNUC__
__attribute__((const))
#endif
static int propagateacceptfailure(const int error)
{
	return error != ECONNABORTED && error != EINTR && error != EMFILE;
}

static bool passio(const size_t proc)
{
	if (fds[proc + 1].revents & POLLERR) {
		fprintf(stderr, "Process %ju has a pipe error\n",
			(uintmax_t) processes[proc].pid);
		return false;
	}

	if (fds[proc + 1].revents & POLLIN) {
		if (passprocerror(proc)) {
			fprintf(stderr,
			        "Could not pass output for process %ju: %s\n",
				(uintmax_t) processes[proc].pid,
			        strerror(errno));
			return false;
		}
		return true;
	}

	return false;
}

enum status resume()
{
	static bool setup = false;
	if (!setup) {
		if (!(fds = malloc(sizeof (struct pollfd))))
			return S_ERROR;
		fds->fd = getlistener();
		fds->events = POLLIN;
		atexit(cleanup);
		setup = true;
	}

	/* Remove processes that have halted */
	for (size_t i = 0; i < nproc; /* noop */) {
		if (!tryrmproc(i))
			i++;
	}

	/* Fetch number of events and accept incoming connection if possible */
	int nevents, sockerr = 0;
	bool action = false;
	if (nproc < mproc) {
		if ((nevents = poll(fds, nproc + 1, -1)) < 0)
			return errno == EINTR ? S_NONE : S_ERROR;
		if (fds[0].revents & POLLIN) {
			char *a;
			const int sock = acceptremote(fds[0].fd, &a);
			if (sock >= 0) {
				if (addproc(sock, a)) {
					const int err = errno;
					free(a);
					close(sock);
					errno = err;
					return -1;
				}
				free(a);
				close(sock);
				nevents--;
				action = true;
			} else
				sockerr = errno;
		}
	} else if ((nevents = poll(fds + 1, nproc, 50)) < 0)
		return errno == EINTR ? S_NONE : S_ERROR;

	/* Forward error outputs */
	if (nevents) {
		for (size_t i = 0; i < nproc; i++)
			action |= passio(i);
	}

	if (sockerr) {
		errno = sockerr;
		return propagateacceptfailure(sockerr) ? S_ERROR : S_SOME;
	}
	return action ? S_SOME : S_NONE;
}
