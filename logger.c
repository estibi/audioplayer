#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// TODO: queue/socket based logger (async)

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *file;

#define	log_filename "./engine.log"

void
logger(char *fmt, ...)
{
	int i;
	char *p, *sp;

	va_list list;

	pthread_mutex_lock(&log_mutex);
	file = fopen(log_filename, "a+");
	if (!file) {
		printf("LOG OPEN ERROR\n");
		printf("%s\n", strerror(errno));
		pthread_mutex_unlock(&log_mutex);
		return;
	}

	va_start(list, fmt);
	for (p = fmt; *p; p++) {
		if (*p != '%') {
			fputc(*p, file);
		} else {
			switch (*++p) {
			case 's':
				sp = va_arg(list, char *);
				fprintf(file, "%s", sp);
				continue;
			case 'd':
				i = va_arg(list, int);
				fprintf(file, "%d", i);
				continue;
			}
		}
	}
	va_end(list);

	if (fclose(file) != 0) {
		printf("LOG CLOSE ERROR\n");
		printf("%s\n", strerror(errno));
		pthread_mutex_unlock(&log_mutex);
		return;
	}
	pthread_mutex_unlock(&log_mutex);
}
