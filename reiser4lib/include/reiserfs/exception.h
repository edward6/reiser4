/*
	exception.h -- exception types and defines
	Copyright (C) 1996 - 2002 Hans Reiser.
*/

#ifndef EXCEPTION_H
#define EXCEPTION_H

enum reiserfs_exception_type {
	EXCEPTION_INFORMATION	= 1,
	EXCEPTION_WARNING 		= 2,
	EXCEPTION_ERROR 		= 3,
	EXCEPTION_FATAL 		= 4,
	EXCEPTION_BUG 			= 5,
};

typedef enum reiserfs_exception_type reiserfs_exception_type_t;

enum reiserfs_exception_option {
	EXCEPTION_UNHANDLED 	= 1 << 0,
	EXCEPTION_YES 			= 1 << 1,
	EXCEPTION_NO 			= 1 << 2,
	EXCEPTION_OK 			= 1 << 3,
	EXCEPTION_RETRY 		= 1 << 4,
	EXCEPTION_IGNORE 		= 1 << 5,
	EXCEPTION_CANCEL 		= 1 << 6
};

typedef enum reiserfs_exception_option reiserfs_exception_option_t;

struct reiserfs_exception {
	char *hint;
	char *message;
	reiserfs_exception_type_t type;
	reiserfs_exception_option_t options;
};

typedef struct reiserfs_exception reiserfs_exception_t;

typedef reiserfs_exception_option_t (*reiserfs_exception_handler_t) 
	(reiserfs_exception_t *ex);

extern char *reiserfs_exception_type_string(reiserfs_exception_type_t type);
extern char *reiserfs_exception_option_string(reiserfs_exception_option_t opt);

extern char *reiserfs_exception_hint(reiserfs_exception_t *ex);
extern char *reiserfs_exception_message(reiserfs_exception_t *ex);
extern reiserfs_exception_type_t reiserfs_exception_type(reiserfs_exception_t *ex);
extern reiserfs_exception_option_t reiserfs_exception_option(reiserfs_exception_t *ex);

extern void reiserfs_exception_set_handler(reiserfs_exception_handler_t handler);

extern reiserfs_exception_option_t reiserfs_exception_throw(reiserfs_exception_type_t type, 
	reiserfs_exception_option_t opt, const char *hint,  const char *message, ...);
	
extern reiserfs_exception_option_t reiserfs_exception_rethrow(void);

extern void reiserfs_exception_catch(void);
extern void reiserfs_exception_fetch_all(void);
extern void reiserfs_exception_leave_all(void);

#endif

