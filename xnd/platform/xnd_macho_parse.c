/* xnd_macho_parse.c */
#include "xnd/xnd.h"
#include "xnd/util/path.h"
#include "macho.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *help =
"USAGE: ./xnd_macho_parse [options] file\n"
"OPTIONS:\n"
"  --bind\n"
"     Parse bind opcodes\n"
"  --weak-bind\n"
"     Parse weak bind opcodes\n"
"  --lazy-bind\n"
"     Parse lazy bind opcodes\n";

static void usage(void);

int main(int argc, char *argv[])
{
	int err, options = 0;
	char *file, path[PATH_MAX] = {0};

	xnd_log_setup();
	if (argc < 2) {
		usage();
		exit(0);
	}

	argv++;
	argc--;
	while (argc) {
		if (strcmp(argv[0], "--bind") == 0) {
			options |= BIND_TYPE_REGULAR;
			argv++;
			argc--;
		} else if (strcmp(argv[0], "--weak-bind") == 0) {
			options |= BIND_TYPE_WEAK;
			argv++;
			argc--;
		} else if (strcmp(argv[0], "--lazy-bind") == 0) {
			options |= BIND_TYPE_LAZY;
			argv++;
			argc--;
		} else if (strncmp(argv[0], "--", 2) == 0) {
			xnd_error("Unknown option: %s\n", argv[0]);
			usage();
			exit(-1);
		} else {
			file = argv[0];
			argv++;
			argc--;
		}
	}

	if (!options) {
		xnd_error("No options selected\n");
		usage();
		exit(-1);
	}

	err = access(file, X_OK);
	if (err != 0) {
		if (errno != ENOENT) {
			xnd_error("Invalid executable: %s\n", file);
			exit(-1);
		}
		err = xnd_path_find(file, path, PATH_MAX);
		if (err != 0) {
			xnd_error("Couldn't find executable: %s\n", file);
			exit(-1);
		}
	} else {
		strncpy(path, file, strlen(file) + 1);
	}

	if (macho_parse_opcodes(path, options) != 0)
		exit(-1);

	exit(0);
}

static void usage(void)
{
	xnd_printf("%s", help);
}
