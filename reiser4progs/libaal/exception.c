/*
    exception.c -- exceptions handling functions. Exception mechanism is used
    in order to provide unified interface for error handling.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <stdarg.h>

static aal_exception_option_t default_handler(aal_exception_t *exception);
static aal_exception_handler_t exception_handler = default_handler;

/* Strings for all exception types */
static char *type_strings[] = {
    "Information", 
    "Warning", 
    "Error", 
    "Fatal", 
    "Bug"
};

/* Strings for all exception options */
static char *option_strings[] = {
    "Yes", 
    "No", 
    "OK", 
    "Retry", 
    "Ignore", 
    "Cancel"
};

static int fetch_count = 0;

/* 
    Helper functions for getting different exception attributes (option string, 
    type string, etc). They are used in exception handing functions.
*/
char *aal_exception_type_string(aal_exception_type_t type) {
    return type_strings[type - 1];
}

aal_exception_type_t aal_exception_type(aal_exception_t *exception) {
    return exception->type;
}

char *aal_exception_option_string(aal_exception_option_t opt) {
    return option_strings[aal_log2(opt)];
}

aal_exception_option_t aal_exception_option(aal_exception_t *exception) {
    return exception->options;
}

char *aal_exception_message(aal_exception_t *exception) {
    return exception->message;
}

/* 
    Default exception handler. It just prints exception kind and message. It is 
    possible to use an alternative handler in order to provide more smart exception 
    handing. Alternative function for instance, might be make question to user, what 
    libreiser4 should do after exception (retry, ignore, etc).
*/
static aal_exception_option_t default_handler(aal_exception_t *exception) {

    if (exception->type != EXCEPTION_BUG)
	aal_printf("%s: ", aal_exception_type_string(exception->type));
	
    aal_printf("%s\n", exception->message);

    switch (exception->options) {
        case EXCEPTION_OK:
        case EXCEPTION_CANCEL:
        case EXCEPTION_IGNORE:
            return exception->options;
	    
        default:
            return EXCEPTION_UNHANDLED;
    }
}

/* 
    Sets alternative exception handler, if passed handler isn't NULL. Otherwise 
    sets exception handler into default one.
 */
void aal_exception_set_handler(aal_exception_handler_t handler) {
    exception_handler = handler ? handler : default_handler;
}

/* Finished exception life cycle, that is destroys exception */
void aal_exception_catch(aal_exception_t *exception) {
	
    if (!exception)	
	return;
	
    aal_free(exception->message);
    aal_free(exception);
}

/* 
    The job of this function is to call current exception handler and return 
    the result of handling (for instance, retry, ignore, etc).
*/
static aal_exception_option_t aal_exception_actual_throw(aal_exception_t *exception) {
    aal_exception_option_t opt;

    if (fetch_count)
	return EXCEPTION_UNHANDLED;
	
    opt = exception_handler(exception);
    aal_exception_catch(exception);
    return opt;
}

/* 
    Public function for throw exception. It creates new exception instance and 
    pass the control to aal_exception_actual_throw function for further handling.
*/
aal_exception_option_t aal_exception_throw(aal_exception_type_t type,
    aal_exception_option_t opts, const char *message, ...)
{
    va_list arg_list;
    aal_exception_t *exception;

    if (!(exception = (aal_exception_t *)aal_malloc(sizeof(aal_exception_t))))
	goto error_no_memory;

    if (!(exception->message = (char*)aal_malloc(4096)))
	goto error_no_memory;

    aal_memset(exception->message, 0, 4096);
	
    exception->type = type;
    exception->options = opts;

    va_start(arg_list, message);
    aal_vsnprintf(exception->message, 4096, message, arg_list);
    va_end(arg_list);
	
    return aal_exception_actual_throw(exception);
    
error_no_memory:
    aal_printf("Out of memory in exception handler!\n");
    return EXCEPTION_UNHANDLED;
}

/* 
    These functions are used for switching exception factory into silent mode.
    This mode forces do not handle exceptions at all. As it is may be used few
    times while the control is walking via stack, there is counter.
*/
void aal_exception_fetch_all(void) {
    fetch_count++;
}

void aal_exception_leave_all(void) {
    if (fetch_count > 0)
	fetch_count--;
}

