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
#include <sys/statfs.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <dirent.h>
/* #define NDEBUG */
#include <assert.h>
#include <signal.h>

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
	int stop;
} stats_t;

stats_t stats;

static void *worker(void *arg);

typedef struct {
	void *start;
	int   length;
} mmapped_t;

typedef struct params {
	int         dirs;
	int         files;
	char       *buffer;
	const char *filename;
	int         fileno;
	int         dirno;
	DIR       **cwd;
	int        *fd;
	mmapped_t  *mmapped;
} params_t;

static void sync_file(params_t *params);
static void read_file(params_t *params);
static void write_file(params_t *params);
static void rename_file(params_t *params);
static void unlink_file(params_t *params);
static void link_file(params_t *params);
static void sym_file(params_t *params);
static void trunc_file(params_t *params);
static void pip_file(params_t *params);
static void mmap_file(params_t *params);
static void open_file(params_t *params);
static void gc_file(params_t *params);

static void nap(int secs, int nanos);
static void _nap(int secs, int nanos);

static void orderedname(params_t *params, char *name);

#define DEFAULT_THREADS 10
#define DEFAULT_FILES 100
#define DEFAULT_DIRS 1
#define DEFAULT_ITERATIONS 10
#define DEFAULT_DELTA 1
#define DEFAULT_MAX_SLEEP 0
#define DEFAULT_BUF_SIZE 4096 * 100
#define DEFAULT_MAX_SIZE 4096 * 100 * 1000
#define DEFAULT_VERBOSE 0
#define DEFAULT_LIMIT 0
#define DEFAULT_BENCHMARK 0
#define DEFAULT_OPERATIONS 0
#define DEFAULT_OPEN_FD    10
#define DEFAULT_MMAPED_AREAS  100

int delta = DEFAULT_DELTA;
int max_sleep = DEFAULT_MAX_SLEEP;
int max_buf_size = DEFAULT_BUF_SIZE;
int max_size = DEFAULT_MAX_SIZE;
int verbose = DEFAULT_VERBOSE;
unsigned long long limit = DEFAULT_LIMIT;
unsigned long long operations = DEFAULT_OPERATIONS;
int benchmark = DEFAULT_BENCHMARK;
int files = DEFAULT_FILES;
int dirs = DEFAULT_DIRS;
int fd_num = DEFAULT_OPEN_FD;
int nr_mmapped = DEFAULT_MMAPED_AREAS;
int pgsize;
int sigs = 0;

DIR **cwds = NULL;

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
	truncop,
	pipop,
	openop,
	mmapop,
	gcop
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
	DEFOPS(pip),
	DEFOPS(mmap),
	DEFOPS(open),
	DEFOPS(gc),
	{
		.label = NULL
	}
};

const char optstring[] = "p:f:d:D:i:s:b:M:BvF:L:O:o:m:j";

static double
rate(unsigned long events, int secs)
{
	return ((double) events) / secs;
}

static void
usage(char *argv0)
{
	char *progname;
	op_t *op;

	progname = strrchr(argv0, '/');
	if (progname == NULL)
		progname = argv0;
	else
		progname ++;

	fprintf(stderr,
		"usage: %s options\n"
		"\n\tRandomly creates and removes files from multiple threads."
		"\n\tCan be used to test NFS stale handle detection."
		"\n\tCompiled from " __FILE__ " at " __DATE__ "\n\n",
		progname);
	fprintf(stderr, "options, deafults in []\n"
		"\t-p N   \tlaunch N concurrent processes [%i]\n"
		"\t-f N   \toperate on N files [%i]\n"
		"\t-d N   \tduplicate file set in N directories [%i]\n"
		"\t-D N   \titeration takes N seconds [%i]\n"
		"\t-i N   \tperform N iterations [%i]\n"
		"\t-O N   \tperform N operations [%i]\n"
		"\t-s N   \tsleep [0 .. N] nanoseconds between operations [%i]\n"
		"\t-b N   \tmaximal buffer size is N bytes [%i]\n"
		"\t-M N   \tmaximal file size is N bytes [%i]\n"
		"\t-o N   \tkeep N file descriptors open [%i]\n"
		"\t-m N   \tkeep N memory areas mmapped [%i]\n"
		"\t-B     \tbenchmark mode: don't ever sleep [%i]\n"
		"\t-v     \tincrease verbosity\n"
		"\t-F op=N\tset relative frequence of op (see below) to N\n"
		"\t-L N   \tlimit amount of used disk space to N kbytes [%i]\n"
		"\t-j     \twait for the completion of all spawned threads\n",

		DEFAULT_THREADS,
		DEFAULT_FILES,
		DEFAULT_DIRS,
		DEFAULT_DELTA,
		DEFAULT_ITERATIONS,
		DEFAULT_OPERATIONS,
		DEFAULT_MAX_SLEEP,
		DEFAULT_BUF_SIZE,
		DEFAULT_MAX_SIZE,
		DEFAULT_OPEN_FD,
		DEFAULT_MMAPED_AREAS,
		DEFAULT_BENCHMARK,
		DEFAULT_LIMIT);

	fprintf(stderr, "\noperations with default frequencies\n");
	for (op = &ops[0] ; op->label ; ++ op) {
		fprintf(stderr, "\t%s\t%i\n", op->label, op->freq);
	}
}

static void
sighandler(int signo)
{
	++ sigs;
}

static unsigned long long
getavail()
{
	int result;

	struct statfs buf;

	result = statfs(".", &buf);
	if (result != 0) {
		perror("statfs");
		exit(1);
	}
	return buf.f_bsize * (buf.f_bavail >> 10);
}

int
main(int argc, char **argv)
{
	ret_t result;
	int threads = DEFAULT_THREADS;
	int i;
	int iterations = DEFAULT_ITERATIONS;
	unsigned long long operations = DEFAULT_OPERATIONS;
	int opt;
	char dname[30];
	unsigned long long initiallyavail;
	unsigned long long used;
	op_t *op;
	pthread_t *id;
	int join;

	result = ok;
	ops[gcop].freq = 0;
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
			dirs = atoi(optarg);
			break;
		case 'D':
			delta = atoi(optarg);
			break;
		case 'i':
			iterations = atoi(optarg);
			break;
		case 'O':
			operations = atoll(optarg);
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
		case 'o':
			fd_num = atoi(optarg);
			break;
		case 'm':
			nr_mmapped = atoi(optarg);
			break;
		case 'B':
			benchmark = 1;
			break;
		case 'v':
			++verbose;
			break;
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
			break;
		case 'L':
			limit = atoll(optarg);
			break;
		case -1:
			break;
		}
	} while (opt != -1);
	stats.total = operations;
	stats.done = 0;
	stats.stop = 0;

	stats.totfreq = 0;
	for (op = &ops[0] ; op->label ; ++ op)
		stats.totfreq += op->freq;

	if (gettimeofday(&stats.start, NULL) != 0) {
		perror("gettimeofday");
		return 1;
	}
	pthread_mutex_init(&stats.lock, NULL);

	pgsize = getpagesize();
	initiallyavail = getavail();

	signal(SIGBUS, sighandler);
	signal(SIGSEGV, sighandler);

	cwds = calloc(dirs, sizeof cwds[0]);
	if (cwds == NULL) {
		perror("calloc");
		return 1;
	}

	for (i = 0; i < dirs; ++i) {
		sprintf(dname, "d%x", i);
		if (mkdir(dname, 0700) == -1 && errno != EEXIST) {
			perror("mkdir");
			return 1;
		}
	}

	fprintf(stderr,
		"%s: %i processes, %i files, delta: %i"
		"\n\titerations: %i, sleep: %i, buffer: %i, max size: %i\n",
		argv[0], threads, files, delta, iterations,
		max_sleep, max_buf_size, max_size);

	id = calloc(threads, sizeof id[0]);
	if (id == NULL) {
		perror("calloc");
		return 1;
	}

	for (i = 0; i < threads; ++i) {
		int rv;

		rv = pthread_create(&id[i], NULL, worker, NULL);
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
		used = initiallyavail - getavail();
		printf("\nseconds: %i\topens: %lu [%f] lseeks: %lu [%f]\n"
		       "\terrors: %lu, sigs: %i, "
		       "used: %lli, gc: %i\n",
		       i,
		       stats.opens, rate(stats.opens, i),
		       stats.lseeks, rate(stats.lseeks, i),
		       stats.errors, sigs, used, ops[gcop].freq);
		printf("op:\tok\tmiss\tbusy\terr\tdone\trate\tglobal rate\n");
		for (op = &ops[0] ; op->label ; ++ op) {
			int done;
			int subtotal;
			int ratio;

			done = op->result.ok;
			subtotal = op->result.subtotal;
			op->result.subtotal = done;
			if (op->freq != 0)
				ratio = stats.totfreq / op->freq;
			else
				ratio = 0;

			printf("%s:\t%i\t%i\t%i\t%i\t%i\t%.1f\t%.1f\n",
			       op->label,
			       done,
			       op->result.missed,
			       op->result.busy,
			       op->result.failure,
			       done - subtotal,
			       rate((done - subtotal) * ratio, delta),
			       rate(done * ratio, i));
		}
		fflush(stdout);
		if (limit != 0) {
			int origfreq;

			origfreq = ops[gcop].freq;
			if (used > limit / 2) {
				if (used > limit)
					ops[gcop].freq *= 2;
				else
					ops[gcop].freq = (used - limit / 2) * 100 / (limit / 2);
			} else
				ops[gcop].freq = 0;
			stats.totfreq += (ops[gcop].freq - origfreq);
		}
		_nap(delta, 0);
	}
	stats.stop = 1;
	if (join) {
		for (i = 0; i < threads; ++i) {
			int ret;

			ret = pthread_join(id[i], NULL);
			if (ret != 0)
				fprintf(stderr, "pthread_join: %i\n", ret);
		}
	}
	return result;
}

static void *
worker(void *arg)
{
	params_t params;
	char fileName[30];

	if (signal(SIGBUS, sighandler) == SIG_ERR) {
		perror("signal(SIGBUS)");
		return NULL;
	}
	if (signal(SIGSEGV, sighandler) == SIG_ERR) {
		perror("signal(SIGSEGV)");
		return NULL;
	}

	memset(&params, 0, sizeof params);
	params.files    = files;
	params.dirs     = dirs;
	params.buffer   = malloc(max_buf_size);
	params.filename = fileName;
	params.cwd      = cwds;
	params.fd       = calloc(fd_num, sizeof params.fd[0]);
	params.mmapped  = calloc(nr_mmapped, sizeof params.mmapped[0]);

	while (1) {
		op_t *op;
		int   randum;
		int   freqreached;

		params.fileno = RND(params.files);
		params.dirno = RND(params.dirs);
		sprintf(fileName, "d%x/%x", params.dirno, params.fileno);
		randum = RND(stats.totfreq);
		freqreached = 0;
		for (op = &ops[0] ; op->label ; ++ op) {
			freqreached += op->freq;
			if (randum < freqreached) {
				op->handler(&params);
				break;
			}
		}
		STEX(++stats.done);
		if (!benchmark)
			nap(0, RND(max_sleep));
		else if (stats.total && stats.done >= stats.total) {
			pthread_mutex_lock(&stats.lock);
			gettimeofday(&stats.end, NULL);
			printf("start: %li.%li, end: %li.%li, diff: %li, %li\n",
			       stats.start.tv_sec, stats.start.tv_usec,
			       stats.end.tv_sec, stats.end.tv_usec,
			       stats.end.tv_sec - stats.start.tv_sec,
			       stats.end.tv_usec - stats.start.tv_usec);
			break;
		}
		if (stats.stop)
			break;
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
			fprintf(stderr, "%s open/sync: %s(%i)\n", fileName,
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
		fprintf(stderr, "%s open/read: %s(%i)\n", fileName, strerror(errno),
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
		fprintf(stderr, "%s open/write: %s(%i)\n", fileName, strerror(errno),
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
	char source[30];
	const char *fileName;
	int files;

	fileName = params->filename;
	files = params->files;
	orderedname(params, target);
	sprintf(source, "../%s", fileName);
	if (symlink(source, target) == -1) {
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
pip_file(params_t *params)
{
	struct dirent  entry;
	struct dirent *ptr;
	int result;
	int dirno;
	int dogc;

	dirno = RND(params->dirs);

	if (params->cwd[dirno] == NULL) {
		char dname[30];

		sprintf(dname, "d%x", dirno);
		params->cwd[dirno] = opendir(dname);
		if (params->cwd[dirno] == NULL) {
			perror("opendir");
			exit(1);
		}
	}

	dogc = (params->buffer[0] == 0x66);
	errno = 0;
	result = readdir_r(params->cwd[dirno], &entry, &ptr);
	if (result == 0 && errno == 0 && ptr == &entry) {
		char fname[100];

		sprintf(fname, "d%x/%s", dirno, entry.d_name);
		if (dogc) {
			if (unlink(fname) == -1) {
				switch (errno) {
				case ENOENT:
					STEX(++ops[gcop].result.missed);
					break;
				case EISDIR:
					break;
				default:
					STEX(++stats.errors);
					STEX(++ops[gcop].result.failure);
				}
			} else
				STEX(++ops[gcop].result.ok);
		} else {
			STEX(++ops[pipop].result.ok);
			if (verbose)
				printf("[%li] P: %s\n", pthread_self(), fname);
		}
	} else if (errno == ENOENT || ptr == NULL)
		rewinddir(params->cwd[dirno]);
	else if (verbose) {
		printf("[%li] P: %i, %i, %p, %s\n",
		       pthread_self(), result, errno, ptr, entry.d_name);
		STEX(++ops[dogc ? gcop : pipop].result.failure);
	}
}

static void
open_file(params_t *params)
{
	int fdno;

	fdno = RND(fd_num);
	if (params->fd[fdno] == 0) {
		int fd;
		const char *fileName;

		fileName = params->filename;
		fd = open(fileName, O_CREAT | O_APPEND | O_RDWR, 0700);
		if (fd == -1) {
			fprintf(stderr, "%s open/open: %s(%i)\n",
				fileName, strerror(errno), errno);
			STEX(++stats.errors);
			STEX(++ops[openop].result.missed);
			return;
		}
		params->fd[fdno] = fd;
		STEX(++stats.opens);
		STEX(++ops[openop].result.ok);
	} else {
		close(params->fd[fdno]);
		params->fd[fdno] = 0;
	}
}

static char *minp(char *p1, char *p2)
{
	if (p1 < p2)
		return p1;
	else
		return p2;
}

static roundtopage(unsigned long val)
{
	return (val + pgsize - 1) / pgsize * pgsize;
}

static void
mmap_file(params_t *params)
{
	int areano;

	areano = RND(nr_mmapped);
	if (params->mmapped[areano].start == NULL) {
		int fd;

		fd = params->fd[RND(fd_num)];
		if (fd != 0) {
			int result;
			size_t length;
			int flags;
			off_t offset;
			void *addr;

			length = roundtopage(RND(max_buf_size) + 30);
			offset = roundtopage(RND(max_size));
			flags  = RND(1) == 0 ? MAP_SHARED : MAP_PRIVATE;
			result = ftruncate(fd, offset + length + 1);
			if (result == -1) {
				fprintf(stderr,
					"%i mmap/truncate: %s(%i)\n",
					fd, strerror(errno), errno);
				STEX(++stats.errors);
				STEX(++ops[mmapop].result.missed);
			} else {
				addr = mmap(NULL, length, PROT_READ | PROT_WRITE,
					    flags, fd, offset);
				if (addr == MAP_FAILED) {
					fprintf(stderr, "%i mmap: %s(%i)\n",
						fd, strerror(errno), errno);
					STEX(++stats.errors);
					STEX(++ops[mmapop].result.missed);
				} else {
					params->mmapped[areano].start  = addr;
					params->mmapped[areano].length = length;
				}
			}
			STEX(++ops[mmapop].result.ok);
		}
	} else if (RND(100) == 0) {
		munmap(params->mmapped[areano].start,
		       params->mmapped[areano].length);
		params->mmapped[areano].start = NULL;
		STEX(++ops[mmapop].result.ok);
	} else {
		char *scan;
		char *end;
		int length;
		int sigs0;

		scan = params->mmapped[areano].start;
		scan += RND(params->mmapped[areano].length);
		end = minp(scan + RND(max_buf_size) + 30,
			   params->mmapped[areano].start +
			   params->mmapped[areano].length - 1);
		sigs0 = sigs;
		if (RND(1) == 0) {
			char x;

			x = 0;
			for (;scan < end && sigs == sigs0; ++scan)
				x += *scan;
		} else {
			for (;scan < end && sigs == sigs0; ++scan)
				*scan = ((int)scan) ^ (*scan);
		}
		STEX(++ops[mmapop].result.ok);
	}
}

static void
gc_file(params_t *params)
{
	params->buffer[0] = 0x66;
	pip_file(params);
	params->buffer[0] = 0x00;
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
	int dirno;

	targetno = params->fileno +
		RND(params->files - params->fileno - 1) + 1;

	dirno = RND(params->dirs);
	sprintf(name, "d%x/%x", dirno, targetno);
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
