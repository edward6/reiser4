/*
    exception.c -- common for all progs exception handler and related functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <unistd.h>

#include <aal/aal.h>
#include <misc/misc.h>

/* This function returns number of specified turned on options */
static int progs_exception_option_count(
    aal_exception_option_t options,	    /* options to be inspected */
    int start				    /* options will be inspected started from */
) {
    int i, count = 0;
    
    for (i = start; i < aal_log2(EXCEPTION_LAST); i++)
	count += ((1 << i) & options) ? 1 : 0;

    return count;
}

/* 
    This function makes search for option by its name in passed available option 
    set.
*/
static aal_exception_option_t progs_exception_oneof(
    char *name,			    /* option name to be checked */
    aal_exception_option_t options  /* aavilable options */
) {
    int i;
    
    if (!name || aal_strlen(name) == 0)
	return EXCEPTION_UNHANDLED;
    
    for (i = 0; i < aal_log2(EXCEPTION_LAST); i++) {
	if ((1 << i) & options) {
	    char *opt = aal_exception_option_string(1 << i);
	    if (aal_strncmp(opt, name, aal_strlen(name)) == 0 || 
		    (aal_strlen(name) == 1 && toupper(opt[0]) == toupper(name[0])))
		return 1 << i;
	}
    }
    
    return EXCEPTION_UNHANDLED;
}

/* Constructs exception message */
static void progs_exception_print_wrap(
    aal_exception_t *exception
) {
    char buff[4096];

    aal_memset(buff, 0, sizeof(buff));
    
    if (exception->type != EXCEPTION_BUG) {
        aal_snprintf(buff, sizeof(buff), "%s: ", 
	    aal_exception_type_string(exception->type));
    }
    
    aal_strncat(buff, exception->message, 
	aal_strlen(exception->message));

    progs_ui_print_wrap(stderr, buff);
}

/* 
    This function prints exception options awailable to be choosen, takes user 
    enter and converts it into aal_exception_option_t type.
*/
static aal_exception_option_t progs_exception_prompt(
    aal_exception_option_t options  /* exception options available to be selected */
) {
    int i;
    char *option;
    char prompt[256];

    if (progs_exception_option_count(options, 0) == 0)
	return EXCEPTION_UNHANDLED;
    
    aal_memset(prompt, 0, sizeof(prompt));
    
    aal_strncat(prompt, "(", 1);
    for (i = 1; i < aal_log2(EXCEPTION_LAST); i++) {
	    
	if ((1 << i) & options) {
	    int count = progs_exception_option_count(options, i + 1);
	    char *opt = aal_exception_option_string(1 << i);
	    
	    aal_strncat(prompt, opt, aal_strlen(opt));
	    
	    if (i < aal_log2(EXCEPTION_LAST) - 1 && count  > 0)
		aal_strncat(prompt, "/", 1);
	    else
		aal_strncat(prompt, "): ", 3);
	}
    }
    
    if (!(option = progs_ui_readline(prompt)) || aal_strlen(option) == 0)
	return EXCEPTION_UNHANDLED;
    
    return progs_exception_oneof(option, options);
}

/* Streams assigned with exception type are stored here */
static void *streams[10];

/* 
    Common exception handler for all reiser4progs. It implements exception handling 
    in "question-answer" maner and used for all communications with user.
*/
aal_exception_option_t progs_exception_handler(
    aal_exception_t *exception		/* exception to be processed */
) {
    int i;
    void *stream = stderr;
    aal_exception_option_t opt;
    aal_list_t *possibilities = NULL;
    
    if (exception->type == EXCEPTION_ERROR || 
	exception->type == EXCEPTION_FATAL ||
	exception->type == EXCEPTION_BUG)
        aal_gauge_failed(); 
    else
	aal_gauge_pause();

    if (progs_exception_option_count(exception->options, 0) == 1) {
	if (!(stream = streams[exception->type]))
	    return EXCEPTION_UNHANDLED;
    }

    if (progs_exception_option_count(exception->options, 0) == 1) {
        if (exception->type == EXCEPTION_WARNING || 
	    exception->type == EXCEPTION_INFORMATION)
	    aal_gauge_resume();
	    
	return exception->options;
    }

#ifdef HAVE_LIBREADLINE
    for (i = 1; i < aal_log2(EXCEPTION_LAST); i++) {
	if ((1 << i) & exception->options) {
	    char *name = aal_exception_option_string(1 << i);
	    possibilities = aal_list_append(possibilities, name);
	}
    }
    progs_ui_set_possibilities(aal_list_first(possibilities));
#endif
    
    progs_exception_print_wrap(exception);
    
    do {
	opt = progs_exception_prompt(exception->options);
    } while (opt == EXCEPTION_UNHANDLED && isatty(0));

#ifdef HAVE_LIBREADLINE
    aal_list_free(possibilities);
    progs_ui_set_possibilities(NULL);
#endif

    if (exception->type == EXCEPTION_WARNING || 
	exception->type == EXCEPTION_INFORMATION)
	aal_gauge_resume();
	    
    return opt;
}

/* This function sets up exception streams */
void progs_exception_set_stream(
    aal_exception_type_t type,	/* type to be assigned with stream */
    void *stream		/* stream to be assigned */
) {
    streams[type] = stream;
}

/* This function gets exception streams */
void *progs_exception_get_stream(
    aal_exception_type_t type	/* type exception stream will be obtained for */
) {
    return streams[type];
}

