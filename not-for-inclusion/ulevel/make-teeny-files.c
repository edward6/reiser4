/*
 * make-teeny-files.c by Andrew Morton <akpm@digeo.com>
 *
 * http://www.zip.com.au/~akpm/linux/patches/stuff/make-teeny-files.c
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE	0100000
#endif

#ifndef O_DIRECT
#define O_DIRECT	040000	/* direct disk access hint */
#endif

#ifndef BLKBSZSET
#define BLKBSZSET  _IOW(0x12,113,sizeof(int))
#endif

#ifndef O_STREAMING
#define O_STREAMING    04000000        /* Streaming access */
#endif

char *ourname;
char _meg[1024*1024 + 40960];
int do_fsync = 0;
unsigned align_offset = 0;
int o_streaming;
int blocksize = 0;

static void usage(void)
{
	fprintf(stderr,
		"Usage: %s depth files-per-dir\n",
		ourname);
	exit(1);
}

static void doit(int depth, int fpd)
{
	int i;

	for (i = 0; i < fpd; i++) {
		char buf[100];
		sprintf(buf, "%08d", i);

		if (depth) {
			mkdir(buf, 0777);
			chdir(buf);
			doit(depth - 1, fpd);
			chdir("..");
		} else {
			int fd = creat(buf, 0666);
			char x[1];

			if (fd < 0) {
				perror("creat");
				exit(1);
			}
			write(fd, x, 1);
			close(fd);
		}
	}
}

int main(int argc, char *argv[])
{
	int depth = 0;
	int fpd = 0;
	int c;

	ourname = argv[0];

	if (argc < 2)
		usage();

	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		default:
			usage();
		}
	}

	if (optind == argc)
		usage();
	depth = atoi(argv[optind++]);
	if (optind == argc)
		usage();
	fpd = atoi(argv[optind++]);
	if (optind != argc)
		usage();
	doit(depth, fpd);
	exit(0);
}
