/*
	exception.h -- exception types and defines
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef EXCEPTION_H
#define EXCEPTION_H

enum agl_exception_type {
	EXCEPTION_INFORMATION	= 1,
	EXCEPTION_WARNING 		= 2,
	EXCEPTION_ERROR 		= 3,
	EXCEPTION_FATAL 		= 4,
	EXCEPTION_BUG 			= 5,
};

typedef enum agl_exception_type agl_exception_type_t;

enum agl_exception_option {
	EXCEPTION_UNHANDLED 	= 1 << 0,
	EXCEPTION_YES 			= 1 << 1,
	EXCEPTION_NO 			= 1 << 2,
	EXCEPTION_OK 			= 1 << 3,
	EXCEPTION_RETRY 		= 1 << 4,
	EXCEPTION_IGNORE 		= 1 << 5,
	EXCEPTION_CANCEL 		= 1 << 6
};

typedef enum agl_exception_option agl_exception_option_t;

struct agl_exception {
	char *hint;
	char *message;
	agl_exception_type_t type;
	agl_exception_option_t options;
};

typedef struct agl_exception agl_exception_t;

typedef agl_exception_option_t (*agl_exception_handler_t) (agl_exception_t *ex);

extern char *agl_exception_type_string(agl_exception_type_t type);
extern char *agl_exception_option_string(agl_exception_option_t opt);

extern char *agl_exception_hint(agl_exception_t *ex);
extern char *agl_exception_message(agl_exception_t *ex);
extern agl_exception_type_t agl_exception_type(agl_exception_t *ex);
extern agl_exception_option_t agl_exception_option(agl_exception_t *ex);

extern void agl_exception_set_handler(agl_exception_handler_t handler);

extern agl_exception_option_t agl_exception_throw(agl_exception_type_t type, 
	agl_exception_option_t opt, const char *hint,  const char *message, ...);
	
extern void agl_exception_fetch_all(void);
extern void agl_exception_leave_all(void);

#endif

