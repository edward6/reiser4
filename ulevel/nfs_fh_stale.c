/* -*- C -*- */

/* cc -o nfs -O3 -Wformat -g nfs_fh_stale.c -pthread */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#define NDEBUG
#include <assert.h>

extern char *optarg;
extern int optind, opterr, optopt;

typedef enum { ok = 0,
	wrong_argc,
	pthread_create_error
} ret_t;

typedef struct stats {
	pthread_mutex_t lock;
	unsigned long opens;
	unsigned long lseeks;
	unsigned long errors;
	unsigned long naps;
	unsigned long total;
	unsigned long done;
	struct timeval start;
	struct timeval end;
	int totfreq;
} stats_t;

stats_t stats;

static void *worker(void *arg);

typedef struct params {
	int         files;
	char       *buffer;
	const char *filename;
	int         fileno;
} params_t;

static void sync_file(params_t *params);
static void read_file(params_t *params);
static void write_file(params_t *params);
static void rename_file(params_t *params);
static void unlink_file(params_t *params);
static void link_file(params_t *params);
static void sym_file(params_t *params);
static void trunc_file(params_t *params);

static void nap(int secs, int nanos);
static void _nap(int secs, int nanos);

static void orderedname(params_t *params, char *name);

int delta = 1;
int max_sleep = 0;
int max_buf_size = 4096 * 100;
int max_size = 4096 * 100 * 1000;
int verbose = 0;

/* random integer number in [0, n - 1] */
#define RND(n) ((int)(((double)(n)) * rand() / (RAND_MAX + 1.0)))

#define STRICT (0)

#if STRICT
#define STEX(e) \
	pthread_mutex_lock(&stats.lock) ; e ; pthread_mutex_unlock(&stats.lock)
#else
#define STEX(e) e
#endif

typedef struct op {
	const char *label;
	int freq;
	void (*handler)(params_t *params);
	struct {
		int subtotal;
		int ok;
		int failure;
		int missed;
		int busy;
	} result;
} op_t;

typedef enum {
	syncop,
	readop,
	writeop,
	renameop,
	unlinkop,
	linkop,
	symop,
	truncop
} op_id_t;

#define DEFOPS(aname)				\
[aname ## op ] = {				\
	.label = #aname,			\
	.freq  = 1,				\
	.handler = aname ## _file		\
}

op_t ops[] = {
	DEFOPS(sync),
	DEFOPS(read),
	DEFOPS(write),
	DEFOPS(rename),
	DEFOPS(unlink),
	DEFOPS(link),
	DEFOPS(sym),
	DEFOPS(trunc),
	{
		.label = NULL
	}
};

const char optstring[] = "p:f:d:i:s:b:M:BvF:";

static double
rate(unsigned long events, int secs)
{
	return ((double) events) / secs;
}

static void
usage(char *argv0)
{
	fprintf(stderr,
		"usage: %s %s\n"
		"\n\tRandomly creates and removes files from multiple threads."
		"\n\tCan be used to test NFS stale handle detection."
		"\n\tCompiled from " __FILE__ " at " __DATE__ "\n\n",
		argv0, optstring);
}

int benchmark = 0;

int
main(int argc, char **argv)
{
	ret_t result;
	int threads = 10;
	int files = 100;
	int i;
	int iterations = 10;
	int opt;
	op_t *op;

	result = ok;
	do {
		opt = getopt(argc, argv, optstring);
		switch (opt) {
		case '?':
			usage(argv[0]);
			return wrong_argc;
		case 'p':
			threads = atoi(optarg);
			break;
		case 'f':
			files = atoi(optarg);
			break;
		case 'd':
			delta = atoi(optarg);
			break;
		case 'i':
			iterations = atoi(optarg);
			break;
		case 's':
			max_sleep = atoi(optarg);
			break;
		case 'b':
			max_buf_size = atoi(optarg);
			if (max_buf_size < 255) {
				fprintf(stderr, "%s: max_buf_size too small\n",
					argv[0]);
				return 1;
			}
			break;
		case 'M':
			max_size = atoi(optarg);
			break;
		case 'B':
			benchmark = 1;
			break;
		case 'v':
			++verbose;
		case 'F': {
			char *eq;
			int   opfreq;

			eq = strchr(optarg, '=');
			if (eq == NULL) {
				fprintf(stderr, "%s: Use -F op=freq\n", argv[0]);
				return 1;
			}
			*eq = 0;
			opfreq = atoi(eq + 1);
			for (op = &ops[0] ; op->label ; ++ op) {
				if (!strcmp(op->label, optarg)) {
					op->freq = opfreq;
					break;
				}
			}
			if (!op->label) {
				fprintf(stderr, "%s: Unknown op: %s\n", 
					argv[0], optarg);
				return 1;
			}
			*eq = '=';
		}
		case -1:
			break;
		}
	} while (opt != -1);
	stats.total = iterations;
	stats.done = 0;

	stats.totfreq = 0;
	for (op = &ops[0] ; op->label ; ++ op)
		stats.totfreq += op->freq;

	if (gettimeofday(&stats.start, NULL) != 0) {
		perror("gettimeofday");
		return 1;
	}
	pthread_mutex_init(&stats.lock, NULL);
	fprintf(stderr,
		"%s: %i processes, %i files, delta: %i"
		"\n\titerations: %i, sleep: %i, buffer: %i, max size: %i\n",
		argv[0], threads, files, delta, iterations,
		max_sleep, max_buf_size, max_size);
	for (i = 0; i < threads; ++i) {
		int rv;
		pthread_t id;

		rv = pthread_create(&id, NULL, worker, (void *) files);
		if (rv != 0) {
			fprintf(stderr,
				"%s: pthread_create fails: %s(%i) while creating %i-%s thread\n",
				argv[0], strerror(rv), rv, i,
				(i % 10 == 1) ? "st" : (i % 10 ==
							2) ? "nd" : "th");
			return 1;
		}
	}
	for (i = 0; i < iterations; i += delta) {
		printf("\nseconds: %i\topens: %lu [%f] lseeks: %lu [%f]\n"
		       "\terrors: %lu, naps: %lu [%f]\n",
		       i,
		       stats.opens, rate(stats.opens, i),
		       stats.lseeks, rate(stats.lseeks, i),
		       stats.errors, stats.naps, rate(stats.naps, i));
		printf("op:\tok\tmiss\tbusy\terr\trate\t\tglobal rate\n");
		for (op = &ops[0] ; op->label ; ++ op) {
			int done;
			int subtotal;

			done = op->result.ok;
			subtotal = op->result.subtotal;
			op->result.subtotal = done;

			printf("%s:\t%i\t%i\t%i\t%i\t%f\t%f\n",
			       op->label,
			       done,
			       op->result.missed,
			       op->result.busy,
			       op->result.failure,
			       rate(done - subtotal, delta), rate(done, i));
		}
		_nap(delta, 0);
	}
	return result;
}

static void *
worker(void *arg)
{
	params_t params;
	char fileName[30];

	params.files    = (int) arg;
	params.buffer   = malloc(max_buf_size);
	params.filename = fileName;

	while (1) {
		op_t *op;
		int   randum;
		int   freqreached;

		params.fileno = RND(params.files);
		sprintf(fileName, "%x", params.fileno);
		randum = RND(stats.totfreq);
		freqreached = 0;
		for (op = &ops[0] ; op->label ; ++ op) {
			freqreached += op->freq;
			if (randum < freqreached) {
				op->handler(&params);
				break;
			}
		}
		assert(op->label == NULL);
		STEX(++stats.done);
		if (!benchmark)
			nap(0, RND(max_sleep));
		else if (stats.done >= stats.total) {
			pthread_mutex_lock(&stats.lock);
			gettimeofday(&stats.end, NULL);
			printf("start: %li.%li, end: %li.%li, diff: %li, %li\n",
			       stats.start.tv_sec, stats.start.tv_usec,
			       stats.end.tv_sec, stats.end.tv_usec,
			       stats.end.tv_sec - stats.start.tv_sec,
			       stats.end.tv_usec - stats.start.tv_usec);
			exit(0);
		}
	}
}

static void
sync_file(params_t *params)
{
	int fd;
	const char *fileName;

	fileName = params->filename;
	fd = open(fileName, O_WRONLY);
	if (fd == -1) {
		if (errno != ENOENT) {
			fprintf(stderr, "%s open: %s(%i)\n", fileName,
				strerror(errno), errno);
			STEX(++stats.errors);
		} else {
			STEX(++ops[syncop].result.missed);
		}
		return;
	}
	if (fsync(fd)) {
		fprintf(stderr, "%s sync: %s(%i)\n", 
			fileName, strerror(errno), errno);
		STEX(++stats.errors);
		STEX(++ops[syncop].result.failure);
		return;
	}
	STEX(++ops[syncop].result.ok);
	if (verbose) {
		printf("[%li] SYNC: %s\n", pthread_self(), fileName);
	}
	close(fd);
}

static void
read_file(params_t *params)
{
	int fd;
	char *buf;
	int bufSize;
	int offset;
	const char *fileName;

	fileName = params->filename;
	fd = open(fileName, O_CREAT | O_APPEND | O_RDWR, 0700);
	if (fd == -1) {
		fprintf(stderr, "%s open: %s(%i)\n", fileName, strerror(errno),
			errno);
		STEX(++stats.errors);
		STEX(++ops[readop].result.missed);
		return;
	}
	STEX(++stats.opens);
	nap(0, RND(max_sleep));
	if (lseek(fd, RND(max_size), SEEK_SET) == -1) {
		fprintf(stderr, "%s lseek: %s(%i)\n",
			fileName, strerror(errno), errno);
		STEX(++stats.errors);
		close(fd);
		return;
	}
	STEX(++stats.lseeks);
	nap(0, RND(max_sleep));
	bufSize = RND(max_buf_size / 3) + 30;
	offset = RND(max_buf_size / 3);
	buf = params->buffer;
	if (read(fd, buf, bufSize + offset) == -1) {
		fprintf(stderr, "%s read: %s(%i)\n", fileName, strerror(errno),
			errno);
		STEX(++stats.errors);
		STEX(++ops[readop].result.failure);
		close(fd);
		return;
	}
	STEX(++ops[readop].result.ok);
	if (verbose) {
		printf("[%li] R: %s\n", pthread_self(), fileName);
	}
	close(fd);
}

static void
write_file(params_t *params)
{
	int fd;
	char *buf;
	int bufSize;
	int offset;
	const char *fileName;

	fileName = params->filename;
	fd = open(fileName, O_CREAT | O_APPEND | O_RDWR, 0700);
	if (fd == -1) {
		fprintf(stderr, "%s open: %s(%i)\n", fileName, strerror(errno),
			errno);
		STEX(++stats.errors);
		STEX(++ops[writeop].result.missed);
		return;
	}
	STEX(++stats.opens);
	nap(0, RND(max_sleep));
	if (lseek(fd, RND(max_size), SEEK_SET) == -1) {
		fprintf(stderr, "%s lseek: %s(%i)\n",
			fileName, strerror(errno), errno);
		STEX(++stats.errors);
		close(fd);
		return;
	}
	STEX(++stats.lseeks);
	nap(0, RND(max_sleep));
	bufSize = RND(max_buf_size / 3) + 30;
	offset = RND(max_buf_size / 3);
	buf = params->buffer;
	memset(buf, 0xfe + stats.opens, max_buf_size);
	sprintf(buf + offset, "---%lx+++", time(NULL));
	if (write(fd, buf, bufSize + offset) == -1) {
		fprintf(stderr, "%s write: %s(%i)\n",
			fileName, strerror(errno), errno);
		STEX(++stats.errors);
		STEX(++ops[writeop].result.failure);
		close(fd);
		return;
	}
	STEX(++ops[writeop].result.ok);
	if (verbose) {
		printf("[%li] W: %s\n", pthread_self(), fileName);
	}
	close(fd);
}

static void
rename_file(params_t *params)
{
	char target[30];
	const char *fileName;
	int files;

	fileName = params->filename;
	files = params->files;
	orderedname(params, target);
	if (rename(fileName, target) == -1) {
		switch (errno) {
		case ENOENT:
			STEX(++ops[renameop].result.missed);
			break;
		default:
			{
				fprintf(stderr, "rename( %s, %s ): %s(%i)\n",
					fileName, target, strerror(errno),
					errno);
				STEX(++stats.errors);
				STEX(++ops[renameop].result.failure);
			}
		}
	} else {
		if (verbose) {
			printf("[%li] %s -> %s\n", pthread_self(), fileName,
			       target);
		}
		STEX(++ops[renameop].result.ok);
	}
}

static void
unlink_file(params_t *params)
{
	const char *fileName;

	fileName = params->filename;
	if (unlink(fileName) == -1) {
		switch (errno) {
		case ENOENT:
			STEX(++ops[unlinkop].result.missed);
			break;
		default:
			{
				fprintf(stderr, "%s unlink: %s(%i)\n",
					fileName, strerror(errno), errno);
				STEX(++stats.errors);
				STEX(++ops[unlinkop].result.failure);
			}
		}
	} else {
		if (verbose) {
			printf("[%li] U: %s\n", pthread_self(), fileName);
		}
		STEX(++ops[unlinkop].result.ok);
	}
}

static void
link_file(params_t *params)
{
	char target[30];
	const char *fileName;
	int files;

	fileName = params->filename;
	files = params->files;
	orderedname(params, target);
	if (link(fileName, target) == -1) {
		switch (errno) {
		case ENOENT:
			STEX(++ops[linkop].result.missed);
			break;
		case EEXIST:
			STEX(++ops[linkop].result.busy);
			break;
		default:
			{
				fprintf(stderr, "link( %s, %s ): %s(%i)\n",
					fileName, target, strerror(errno),
					errno);
				STEX(++stats.errors);
				STEX(++ops[linkop].result.failure);
			}
		}
	} else {
		if (verbose) {
			printf("[%li] %s -> %s\n", pthread_self(), fileName,
			       target);
		}
		STEX(++ops[linkop].result.ok);
	}
}

static void
sym_file(params_t *params)
{
	char target[30];
	const char *fileName;
	int files;
	int targetno;

	fileName = params->filename;
	files = params->files;
	orderedname(params, target);
	if (symlink(fileName, target) == -1) {
		switch (errno) {
		case ENOENT:
			STEX(++ops[symop].result.missed);
			break;
		case EEXIST:
			STEX(++ops[symop].result.busy);
			break;
		default:
			{
				fprintf(stderr, "link( %s, %s ): %s(%i)\n",
					fileName, target, strerror(errno),
					errno);
				STEX(++stats.errors);
				STEX(++ops[symop].result.failure);
			}
		}
	} else {
		if (verbose) {
			printf("[%li] %s -> %s\n", pthread_self(), fileName,
			       target);
		}
		STEX(++ops[symop].result.ok);
	}
}

static void
trunc_file(params_t *params)
{
	const char *fileName;

	fileName = params->filename;
	if (truncate(fileName, RND(max_size)) == -1) {
		switch (errno) {
		case ENOENT:
			STEX(++ops[truncop].result.missed);
			break;
		default:
			{
				fprintf(stderr, "%s trunc: %s(%i)\n",
					fileName, strerror(errno), errno);
				STEX(++stats.errors);
				STEX(++ops[truncop].result.failure);
			}
		}
	} else {
		if (verbose) {
			printf("[%li] T: %s\n", pthread_self(), fileName);
		}
		STEX(++ops[truncop].result.ok);
	}
}

static void
nap(int secs, int nanos)
{
	if (!benchmark)
		_nap(secs, nanos);
}

static void
_nap(int secs, int nanos)
{
	if ((secs > 0) || (nanos > 0)) {
		struct timespec delay;

		delay.tv_sec = secs;
		delay.tv_nsec = nanos;

		if (nanosleep(&delay, NULL) == -1) {
			fprintf(stderr, "nanosleep: %s(%i)\n", strerror(errno),
				errno);
		}
		STEX(++stats.naps);
	}
}

/* 
 * When renaming or linking file (through link, rename, or symlink), maintain
 * certain order, so that infinite loops of symlinks are avoided.
 */
static void orderedname(params_t *params, char *name)
{
	int targetno;

	targetno = params->fileno + RND(params->files - params->fileno - 1) + 1;
	sprintf(name, "%x", targetno);
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 79
  End:
*/
