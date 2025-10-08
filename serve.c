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
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "command.h"

int resume(void);

static volatile sig_atomic_t done;

static void interrupt(const int signum)
{
	(void) signum;
	done = 1;
}

static void confsig()
{
	struct sigaction sa;
	sa.sa_handler = interrupt;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sa, NULL);
}

int main(int argc, char *argv[])
{
	init(argc, argv);
	confsig();
	while (!done) {
		const int r = resume();
		if (r < 0)
			perror("Internal error while running the executor");
		if (r <= 0)
			sched_yield();
	}
	return EXIT_SUCCESS;
}
