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
static aal_exception_streams_t default_streams;


/* Strings for all exception types */
static char *type_strings[] = {
    "Information", 
    "Warning", 
    "Error", 
    "Fatal", 
    "Bug",
    "Question"
};

/* Strings for all exception options */
static char *full_option_strings[] = {
    "",
    "Yes", 
    "No", 
    "OK", 
    "Retry", 
    "Ignore", 
    "Cancel",
    "Details"
};

/* Strings for all exception options */
static char *option_strings[] = {
    "",
    "Yy", 
    "Nn", 
    "Oo", 
    "Rr", 
    "Ii", 
    "Cc",
    "Dd"
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
    return full_option_strings[aal_log2(opt)];
}

aal_exception_option_t aal_exception_option(aal_exception_t *exception) {
    return exception->options;
}

char *aal_exception_message(aal_exception_t *exception) {
    return exception->message;
}

void *aal_exception_stream(aal_exception_t *exception) {
    return exception->stream;
}

static aal_exception_option_t aal_get_choice(aal_exception_option_t options, int c) {
    int i;
    
    if (!options)
	return 0;
	
    for (i = 0; i < aal_log2(EO_LAST); i++) {
	if (aal_test_bit(i, &options) && aal_strchr(option_strings[i], c) != NULL) 
	    return 1 << i;
    }
    
    return EO_UNHANDLED;
}

/* 
    Default exception handler. It prints exception kind (if presents), message and
    provides the possibility to answer on questions. Answer variants are in exception->
    options and the default answer in exception->def_option.
*/
static aal_exception_option_t default_handler(aal_exception_t *exception) {
    char buf[4096], confirm[255];
    int i, count = 0, prompts = 0;
    aal_exception_option_t ret;

    if (exception->type >= ET_LAST || exception->options >= EO_LAST || 
	exception->options & EO_UNHANDLED || exception->def_option >= EO_LAST || 
	exception->def_option & EO_UNHANDLED || !aal_pow_of_two(exception->def_option))
	return EO_UNHANDLED;

    /* default option must exist for mulpiple choice */
    if (!aal_pow_of_two(exception->options) && exception->def_option == 0)
	return EO_UNHANDLED;
	
    aal_memset(buf, 0, sizeof(buf));
    aal_memset(confirm, 0, sizeof(confirm));

    /* Prepare buffer with variants */
    switch (exception->options) {
	case EO_YES:
	case EO_NO:
	case EO_OK:
	case EO_RETRY:
	case EO_IGNORE:
	case EO_CANCEL:
	case EO_DETAILS:
	case 0:
	    break;
	default:
	    aal_strncat(buf, " ", 1);
	    for (i = 0; i < aal_log2(EO_LAST); i++) {
		if (aal_test_bit(i, &exception->options)) {
		    aal_strncat(buf, aal_strlen(buf) == 1 ? "( " : "| ", 2);
		    aal_strncat(buf, full_option_strings[i], aal_strlen(full_option_strings[i]));
		    aal_strncat(buf, " (", 2);
		    buf[aal_strlen(buf)] = option_strings[i][0];
		    aal_strncat(buf, ") ", 2);
		    count++;
		}
	    }
	    aal_strncat(buf, ")", 1);
	    
	    if (exception->def_option) {
		aal_strncat(buf, " [", 2);
		buf[aal_strlen(buf)] = option_strings[aal_log2(exception->def_option)][0];
		aal_strncat(buf, "]", 1);
	    }
		
	    aal_strncat(buf, ": ", 2);
	    break;
    }

    if (exception->stream == NULL) {
	return count <= 1 ? exception->options : exception->def_option;	    
    } else if (count <= 1) {
	aal_fprintf(exception->stream, "%s%s", exception->message, buf);
	return exception->options;
    }

    aal_fprintf(exception->stream, "%s%s", exception->message, buf);
   
    /* Multiple choice, get the answer. */
    while (1) {
	fgets(confirm, sizeof(confirm), stdin);
	if ((aal_strlen(confirm) == 1) && !aal_strncmp(confirm, "\n", 1))
	    return exception->def_option;
	if ((aal_strlen(confirm) == 2) && 
	    (ret = aal_get_choice(exception->options, confirm[0])) != EO_UNHANDLED)
	{
	    return ret;
	}
	if (prompts ++ < 2)
	    aal_fprintf(exception->stream, "Invalid choice. Try again: ");
	else 
	    break;
    };
            
    return EO_UNHANDLED;
}

/* 
    Sets alternative exception handler, if passed handler isn't NULL. Otherwise 
    sets exception handler into default one.
 */
void aal_exception_set_handler(aal_exception_handler_t handler) {
    exception_handler = handler ? handler : default_handler;
}

aal_exception_streams_t *aal_exception_get_streams() {
    return &default_streams;
}

void aal_exception_init_streams(){
    default_streams.info = default_streams.warn = default_streams.ask = stdout;
    default_streams.fatal = default_streams.error = default_streams.bug = stderr;
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
	
    opt = exception_handler(exception);
    aal_exception_catch(exception);
    return opt;
}

/* 
    Public function for throw exception. It creates new exception instance and 
    pass the control to aal_exception_actual_throw function for further handling.
*/
aal_exception_option_t aal_exception_throw(void *stream, aal_exception_type_t type,
    aal_exception_option_t opts, aal_exception_option_t def_opt, const char *message, ...)
{
    va_list arg_list;
    aal_exception_t *exception;

    if (fetch_count)
	return EO_UNHANDLED;

    if (!(exception = (aal_exception_t *)aal_malloc(sizeof(aal_exception_t))))
	goto error_no_exception_memory;

    if (!(exception->message = (char*)aal_malloc(4096)))
	goto error_no_msg_memory;

    aal_memset(exception->message, 0, 4096);
	
    exception->type = type;
    exception->options = opts;
    exception->def_option = def_opt;
    exception->stream = stream;

    va_start(arg_list, message);
    aal_vsnprintf(exception->message, 4096, message, arg_list);
    va_end(arg_list);
	
    return aal_exception_actual_throw(exception);
    
error_no_msg_memory:
    aal_free(exception);
error_no_exception_memory:
    aal_printf("Out of memory in exception handler!\n");
    return EO_UNHANDLED;
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

