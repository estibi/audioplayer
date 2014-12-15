#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "audio_engine.h"
#include "ui.h"


int
init_audio_engine()
{
	int pid, err;
	pid = fork();
	if (pid < 0) {
		printf("fork error, exiting..\n");
		exit(1);
	}
	if (pid > 0) {
		// parent process
		printf("audio engine pid: %d\n", pid);
		return (pid);
	}

	// we are a child process
	err = engine_daemon();
	if (!err) {
		exit(0);
	}
	printf("audio daemon exited with error\n");
	exit(1);
}

void
handler(int signal)
{
	switch (signal) {
	case SIGUSR1:
	printf("received SIGUSR1..\n");
		break;
	default:
		;;
	}
}

int
main(int argc, char *argv[])
{
	int daemon_pid, status, err;
	struct sigaction sa;

	sa.sa_handler = &handler;
	sa.sa_flags = SA_RESTART;
	err = sigaction(SIGUSR1, &sa, NULL);
	if (err == -1) {
		printf("sigaction error:\n");
		printf("%s\n", strerror(errno));
		return (-1);
	}

	daemon_pid = init_audio_engine();

	printf("waiting for audio engine..\n");
	// TODO: handle child exit
	pause();

	printf("starting curses..\n");
	err = curses_ui();

	// wait for audio engine
	err = wait(&status);
	if (err == -1) {
		printf("wait error:\n");
		printf("%s\n", strerror(errno));
		printf("wait status: %d\n", status);
	}

	return (0);
}
