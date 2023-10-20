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
