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
    ET_INFO  = 1,
    ET_WARN  = 2,
    ET_ERROR = 3,
    ET_FATAL = 4,
    ET_BUG   = 5,
    ET_ASK   = 6,
    ET_LAST  = 7
};

typedef enum aal_exception_type aal_exception_type_t;

/* 
    This declaration is the exception options. The components may be composed 
    together.
*/
enum aal_exception_option {
    EO_UNHANDLED    = 1 << 0,
    EO_YES	    = 1 << 1,
    EO_NO	    = 1 << 2,
    EO_OK	    = 1 << 3,
    EO_RETRY	    = 1 << 4,
    EO_IGNORE	    = 1 << 5,
    EO_CANCEL	    = 1 << 6,
    EO_DETAILS	    = 1 << 7,
    EO_LAST	    = 1 << 8
};

typedef enum aal_exception_option aal_exception_option_t;

/* 
    This is exception structure. It contains: exception message, exception type,
    exception options. Usualy, the life cycle of exception is very short. Exception 
    instance  created by aal_exception_throw function and passed to exception handler. 
    After exception processed, it is destroyed by exception factory.
*/
struct aal_exception {
    void *stream;
    char *message;
    aal_exception_type_t type;
    aal_exception_option_t options;
    aal_exception_option_t def_option;
};

typedef struct aal_exception aal_exception_t;

struct aal_exception_streams {
    void *info;
    void *warn;
    void *fatal;
    void *error;
    void *bug;
    void *ask;
};

typedef struct aal_exception_streams aal_exception_streams_t;

typedef aal_exception_option_t (*aal_exception_handler_t) (aal_exception_t *ex);

extern char *aal_exception_type_string(aal_exception_type_t type);
extern char *aal_exception_option_string(aal_exception_option_t opt);

extern char *aal_exception_message(aal_exception_t *ex);
extern aal_exception_type_t aal_exception_type(aal_exception_t *ex);
extern aal_exception_option_t aal_exception_option(aal_exception_t *ex);

extern void aal_exception_set_handler(aal_exception_handler_t handler);

//extern void aal_exception_set_streams(aal_exception_streams_t streams);
extern void aal_exception_init_streams(aal_exception_streams_t *streams);
extern aal_exception_streams_t aal_exception_get_streams();

/* 
    Unfortunately stream cannot be hidden here by binding exception type to stream,
    because on an upper layer (like progs) one may want to send some error exceptions
    to one stream and send other to another stream. So he will just define his own 
    short macroses for his needs.
*/
extern aal_exception_option_t aal_exception_throw(void *stream, aal_exception_type_t type, 
    aal_exception_option_t opt, aal_exception_option_t def_opt, const char *message, ...) 
    __check_format__(printf, 5, 6);

extern void aal_exception_fetch_all(void);
extern void aal_exception_leave_all(void);

#define aal_throw_msg(stream, msg, list...) aal_exception_throw\
    (stream, 0, 0, 0, msg, ##list)
    
#define aal_throw_fatal(opt, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().fatal, ET_FATAL, opt, 0, msg, ##list)

#define aal_throw_error(opt, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().error, ET_ERROR, opt, 0, msg, ##list)

#define aal_throw_warning(opt, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().warn, ET_WARN, opt, 0, msg, ##list)

#define aal_throw_information(opt, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().info, ET_INFO, opt, 0, msg, ##list)

#define aal_throw_bug(opt, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().bug, ET_BUG, opt, 0, msg, ##list)

#define aal_throw_ask(opt, def, msg, list...) aal_exception_throw\
    (aal_exception_get_streams().ask, ET_ASK, opt, def, msg, ##list)

#endif

