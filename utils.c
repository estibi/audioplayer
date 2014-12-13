#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

bool
is_directory(char *file)
{
	int err;
	struct stat file_stat;

	err = stat(file, &file_stat);
	if (!err) {
		// TODO
		return (false)
	}

	if (file_stat.st_mode & S_IFDIR)
		return (true);

	return (false);
}

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
		if (ent->d_name[0] == '.' && ent->d_name[1] == 0 )
			continue; /* skip . */
		snprintf((char *)&contents->list[index].name,
			NAME_MAX,
			"%s",
			ent->d_name);
		index++;
	}
	contents->amount = index;
	(void) closedir(dirp);
	return (0);
}
