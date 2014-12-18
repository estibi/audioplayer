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
count_dir_entries(char *dir_path, bool hidden, bool unsupported)
{
	DIR *dirp = NULL;
	struct dirent *ent = NULL;
	char *n;

	unsigned int amount = 0;

	dirp = opendir(dir_path);
	if (!dirp) {
		return (0);
	}

	while ((ent = readdir(dirp)) != NULL) {
		n = ent->d_name;
		// ".." - always add
		if (n[0] == '.' && n[1] == '.' && n[2] == 0) {
			amount++;
			continue;
		}

		// skip "."
		if (n[0] == '.' && n[1] == 0)
			continue;

		// filter hidden files and directories
		if (!hidden) {
			if (n[0] == '.')
				continue;
		}

		if (!unsupported && !is_directory(n)) {
			if (!is_supported(n))
				continue;
		}
		amount++;
	}

	(void) closedir(dirp);
	return (amount);
}

bool
is_supported(char *name)
{
	int i, j, ext_type, len;
	char uppercase, *extp, *ext_idx = NULL;
	const int max_ext_len = 4; // maximum file extension length
	const char *cmp_type;

	// don't support bad (short) file names
	len = strnlen(name, NAME_MAX);
	if (len < 3)
		return (false);

	// start from the end of file
	for (i = len; i > 1 ; i--) {
		if (name[i] == '.') { // this is the last '.' in the name
			ext_idx = &name[i + 1];
			break;
		}
	}

	// no file extension found
	if (ext_idx == NULL)
		return (false);

	for (ext_type = 0; ext_type < supported_files_num; ext_type++) {
		cmp_type = supported_files[ext_type];

		// skip different extension length
		len = strnlen(ext_idx, max_ext_len);
		if (len != strnlen(cmp_type, max_ext_len))
			continue;

		extp = ext_idx;
		for (j = 0; extp[j] != '\0'; j++) {
			if (extp[j] != cmp_type[j]) {
				// check upper case letter
				uppercase = cmp_type[j];
				uppercase = toupper(uppercase);
				if (extp[j] != uppercase) {
					break; // check another file
				}
			}
			if (j == len - 1)
				return (true);
		}
	}
	return (false);
}

int
scan_dir(struct dir_contents *contents, bool hidden, bool unsupported)
{
	int idx = 0;
	int amount = contents->amount;

	DIR *dirp = NULL;
	struct dirent *ent = NULL;
	char *p, *n;

	dirp = opendir(".");
	if (!dirp) {
		return (-1);
	}

	while ((ent = readdir(dirp)) != NULL) {
		if (idx == amount)
			break;

		n = ent->d_name;
		// ".." - always add
		if (n[0] == '.' && n[1] == '.' && n[2] == 0) {
			p = contents->list[idx]->name;
			snprintf(p, NAME_MAX, "%s", n);
			idx++;
			continue;
		}

		// skip "."
		if (n[0] == '.' && n[1] == 0)
			continue;

		// filter hidden files and directories
		if (!hidden) {
			if (n[0] == '.')
				continue;
		}

		p = contents->list[idx]->name;

		if (!unsupported && !is_directory(n)) {
			if (!is_supported(n))
				continue;
		}
		snprintf(p, NAME_MAX, "%s", n);
		idx++;
	}

	(void) closedir(dirp);
	return (0);
}
