/*
 * Copyright 2000, 2001, 2002 by Hans Reiser, licensing governed by
 * reiser4/README
 */

/*
 * Tracing facility. Copied from reiserfs v3.x patch, never released
 */

#if !defined( __REISER4_TRACE_H__ )
#define __REISER4_TRACE_H__

typedef enum { log_to_file, log_to_console, log_to_bucket } trace_file_type;

#if REISER4_TRACE_TREE

typedef struct {
	trace_file_type  type;
	struct file     *fd;
	char            *buf;
	int              size;
	int              used;
	struct semaphore held;
	int              disabled;
} reiser4_trace_file;

typedef enum { 
	tree_cut    = 'c',
	tree_lookup = 'l',
	tree_insert = 'i',
	tree_paste  = 'p'
} reiser4_traced_op;

extern int open_trace_file   ( struct super_block *super,
			       const char *file_name, int size, 
			       reiser4_trace_file *trace );
extern int write_trace       ( reiser4_trace_file *file, 
			       const char *format, ... ) 
__attribute__ ((format (printf, 2, 3)));

extern int write_trace_raw   ( reiser4_trace_file *file, 
			       const void *data, int len );
extern int hold_trace        ( reiser4_trace_file *file, int flag );
extern int disable_trace     ( reiser4_trace_file *file, int flag );
extern void close_trace_file ( reiser4_trace_file *file );
extern int write_trace_stamp ( reiser4_tree *tree,
			       reiser4_traced_op op, const reiser4_key *key );
#else

typedef struct {} reiser4_trace_file;

#define open_trace_file( super, file_name, size, trace ) noop
#define write_trace( file, format, ... ) noop
#define write_trace_raw( file, data, len ) noop
#define hold_trace( file, flag ) noop
#define disable_trace( file, flag ) noop
#define close_trace_file( file ) noop
#define write_trace_stamp( tree, op, key ) noop

#endif

/* __REISER4_TRACE_H__ */
#endif
