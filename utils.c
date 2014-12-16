#include "utils.h"

bool
is_directory(char *file)
{
	int err;
	struct stat file_stat;

	err = stat(file, &file_stat);
	if (err == -1) {
		// TODO
		return (false);
	}

	if (file_stat.st_mode & S_IFDIR)
		return (true);

	return (false);
}

int
count_dir_entries(char *dir_path)
{
	DIR *dirp = NULL;
	struct dirent *ent = NULL;

	unsigned int amount = 0;

	dirp = opendir(dir_path);
	if (!dirp) {
		return (0);
	}

	while ((ent = readdir(dirp)) != NULL) {
		if (ent->d_name[0] == '.' && ent->d_name[1] == 0 )
			continue; /* skip "." */
		amount++;
	}
	(void) closedir(dirp);
	return (amount);
}

int
scan_dir(struct dir_contents *contents, bool hidden, bool unsupported)
{
	int index = 0;
	int amount = contents->amount;

	DIR *dirp = NULL;
	struct dirent *ent = NULL;
	char *p;

	dirp = opendir(".");
	if (!dirp) {
		return (-1);
	}

	while ((ent = readdir(dirp)) != NULL) {
		if (index == amount)
			break;
		if (ent->d_name[0] == '.' && ent->d_name[1] == 0 )
			continue; /* skip "." */
		p = contents->list[index]->name;
		snprintf(p, NAME_MAX, "%s", ent->d_name);
		index++;
	}

	(void) closedir(dirp);
	return (0);
}
