/*
    exception.h -- exception types, structures and functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <aal/aal.h>

/* This is the type of exception */
enum aal_exception_type {
    EXCEPTION_INFORMATION   = 1,
    EXCEPTION_WARNING	    = 2,
    EXCEPTION_ERROR	    = 3,
    EXCEPTION_FATAL	    = 4,
    EXCEPTION_BUG	    = 5
};

typedef enum aal_exception_type aal_exception_type_t;

/* 
    This declaration is the exception options. The components may be composed 
    together.
*/
enum aal_exception_option {
    EXCEPTION_UNHANDLED	    = 1 << 0,
    EXCEPTION_YES	    = 1 << 1,
    EXCEPTION_NO	    = 1 << 2,
    EXCEPTION_OK	    = 1 << 3,
    EXCEPTION_RETRY	    = 1 << 4,
    EXCEPTION_IGNORE	    = 1 << 5,
    EXCEPTION_CANCEL	    = 1 << 6,
    EXCEPTION_LAST	    = 1 << 7
};

typedef enum aal_exception_option aal_exception_option_t;

#define EXCEPTION_YESNO	    (EXCEPTION_YES | EXCEPTION_NO)
#define EXCEPTION_OKCANCEL  (EXCEPTION_OK | EXCEPTION_CANCEL)

/* 
    This is exception structure. It contains: exception message, exception type,
    exception options. Usualy, the life cycle of exception is very short. Exception 
    instance  created by aal_exception_throw function and passed t exception handler. 
    After exception processed, it is destroyed by exception factory.
*/
struct aal_exception {
    char *message;
    aal_exception_type_t type;
    aal_exception_option_t options;
};

typedef struct aal_exception aal_exception_t;

typedef aal_exception_option_t (*aal_exception_handler_t) (aal_exception_t *ex);

extern char *aal_exception_type_string(aal_exception_type_t type);
extern char *aal_exception_option_string(aal_exception_option_t opt);

extern char *aal_exception_message(aal_exception_t *ex);
extern aal_exception_type_t aal_exception_type(aal_exception_t *ex);
extern aal_exception_option_t aal_exception_option(aal_exception_t *ex);

extern void aal_exception_set_handler(aal_exception_handler_t handler);

extern aal_exception_option_t aal_exception_throw(aal_exception_type_t type, 
    aal_exception_option_t opt, const char *message, ...) __check_format__(printf, 3, 4);

extern void aal_exception_fetch_all(void);
extern void aal_exception_leave_all(void);

#endif

