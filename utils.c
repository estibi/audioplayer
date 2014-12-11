#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

#include "utils.h"

int
scan_dir(char *dir_path, struct dir_contents *contents)
{
	DIR *dirp = NULL;
	struct dirent *ent = NULL;

	int index = 0;

	dirp = opendir(dir_path);
	if (!dirp) {
		// printf("ERROR: Can't open '%s':\n", dir_path);
		// printf("%s\n", strerror(errno));
		return (-1);
	}

	while ((ent = readdir(dirp)) != NULL) {
		if (ent->d_name[0] == '.')
			continue; /* skip . and .. */
		snprintf((char *)&contents->list[index],
			sizeof (filename),
			"%s",
			ent->d_name);
		index++;
	}
	contents->amount = index;
	(void) closedir(dirp);
	return (0);
}
