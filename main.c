#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio_engine.h"
#include "ui.h"


int
init_audio_engine()
{
	int pid;
	pid = fork();
	printf("pid: %d\n", pid);
	if (pid < 0) {
		printf("fork error, exiting..\n");
		exit(1);
	}
	if (pid > 0) {
		// parent process
		return (pid);
	}

	// we are a child process
	engine_daemon();
	return (0);
}

int
main(int argc, char **argv)
{
	int daemon_pid;
	daemon_pid = init_audio_engine();

	init_curses_ui();

	// TODO: wait for audio engine

	return (0);
}
