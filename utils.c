#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

#define	FILES_IN_DIR_LIMIT 20000
#define	NAME_MAX 255

typedef struct filename_t {
	char name[NAME_MAX];
} filename;

typedef struct dir_contents_t {
	filename list[FILES_IN_DIR_LIMIT];
	unsigned int amount;
} dir_contents;


static void
scan_dir(char *dir_path, dir_contents *contents)
{
	DIR *dirp = NULL;
	struct dirent *ent = NULL;

	int index = 0;

	dirp = opendir(dir_path);
	if (!dirp) {
		printf("ERROR: Can't open '%s':\n", dir_path);
		printf("%s\n", strerror(errno));
		return;
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
}


void
show_dir_content(dir_contents *contents)
{
	int index;
	for (index = 0; index < contents->amount; index++) {
		// printw("file: %s\n", &contents->list[index]);
	}
}
