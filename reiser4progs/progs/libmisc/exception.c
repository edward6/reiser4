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

#ifdef HAVE_LIBREADLINE

#ifdef HAVE_TERMCAP_H
#include <termcap.h>
#else
extern int tgetnum(char *key);
#endif

#include <readline/readline.h>
#include <readline/history.h>

#ifndef HAVE_RL_COMPLETION_MATCHES
#define rl_completion_matches completion_matches
#endif

#ifndef rl_compentry_func_t
#define rl_compentry_func_t void
#endif

static aal_list_t *options = NULL;

#endif /* HAVE_LIBREADLINE */

#include <pty.h>

/* Gets screen width */
static uint16_t screen_width(void) {
    struct winsize winsize;
    
    if (ioctl(2, TIOCGWINSZ, &winsize))
	return 0;
    
    return winsize.ws_col;
}

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
static aal_list_t *progs_exception_construct_message(
    aal_exception_t *exception
) {
    uint16_t width;
    aal_list_t *list = NULL;
    char buff[4096], *word, *line, *p;

    aal_memset(buff, 0, sizeof(buff));
    
    if (exception->type != EXCEPTION_BUG) {
        aal_snprintf(buff, sizeof(buff), "%s: ", 
	    aal_exception_type_string(exception->type));
    }
    
    aal_strncat(buff, exception->message, 
	aal_strlen(exception->message));
    
    width = screen_width();
    p = &buff[0]; line = NULL;

    while ((word = aal_strsep(&p, " "))) {
	if (!line || 
	    aal_strlen(line) + aal_strlen(word) > width) 
	{
	    if (line)
		list = aal_list_append(list, line);
	    
	    line = aal_calloc(width + 1, 0);
	}
	
	aal_strncat(line, word, strlen(word));

	if (aal_strlen(line) + 1 < width)
	    aal_strncat(line, " ", 1);
    }
    
    if (line && aal_strlen(line)) {
	char lc = line[aal_strlen(line) - 1];
	
	if (lc == '\040')
	    line[aal_strlen(line) - 1] = '\0';

	list = aal_list_append(list, line);
    }

    return list ? aal_list_first(list) : NULL;
}

/* This function gets user enter */
static char *progs_exception_readline(
    char *prompt		/* prompt to be printed */
) {
    char *line;
    
    aal_assert("umka-1021", prompt != NULL, return NULL);
    
#ifdef HAVE_LIBREADLINE
    /* 
	FIXME-UMKA: Here also should be check if line is unique before adding 
	it to history.
    */
    if ((line = readline(prompt)) && aal_strlen(line)) {
	HIST_ENTRY *last_entry = current_history();
	if (!last_entry || aal_strncmp(last_entry->line, line, aal_strlen(line)))
	    add_history(line);
    }
#else
    fprintf(stderr, prompt);
    
    if (!(line = aal_calloc(256, 0)))
	return NULL;
    
    fgets(line, 256, stdin);
#endif
    
    if (line) {
	uint32_t len = aal_strlen(line);
	if (len) {
	    if (line[len - 1] == '\n' || line[len - 1] == '\040')
		line[len - 1] = '\0';
	}
    }

    return line;
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
    aal_exception_option_t res;

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
    
    if (!(option = progs_exception_readline(prompt)))
	return EXCEPTION_UNHANDLED;
    
    if (aal_strlen(option) == 0) {
	res = EXCEPTION_UNHANDLED;
	goto exit;
    }
    
    res = progs_exception_oneof(option, options);
    
exit:
    aal_free(option);
    return res;
}

#ifdef HAVE_LIBREADLINE

static aal_list_t *list = NULL;

void strup(char *dst, const char *src) {
    const char *s;
    int i = 0;

    s = src;
    while (*s)
	dst[i++] = toupper(*s++);
    
    dst[i] = '\0';
}

static char *progs_exception_generator(char *text, int state) {
    char *opt;
    char s[80], s1[80];
    
    if (!state)
	list = options;
    
    while (list) {
	aal_memset(s, 0, sizeof(s));
	aal_memset(s1, 0, sizeof(s1));
	
	opt = (char *)list->item;
	list = list->next;

	strup(s, opt); strup(s1, text);
	if (!aal_strncmp(s, s1, aal_strlen(s1)))
	    return aal_strdup(opt);
    }
    
    return NULL;
}

static char **progs_exception_complete(char *text, int start, int end) {
    return rl_completion_matches(text,
	(rl_compentry_func_t *)progs_exception_generator);
}

#endif

/* Streams assigned with exception type are stored here */
static void *streams[10];

void progs_exception_init(void) {
#ifdef HAVE_LIBREADLINE
    rl_initialize();
    rl_attempted_completion_function = 
	(CPPFunction *)progs_exception_complete;
#endif
}

void progs_exception_done(void) {}

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
    aal_list_t *list, *walk = NULL;
    
    if (!(list = progs_exception_construct_message(exception)))
	return EXCEPTION_UNHANDLED;
    
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

    aal_list_foreach_forward(walk, list) {
	char *line = (char *)walk->item;

	if (line && aal_strlen(line) > 0) {
	    fprintf(stream, "%s\n", line);
	    aal_free(line);
	}
    }
    
    aal_list_free(list);

    if (progs_exception_option_count(exception->options, 0) == 1) {
        if (exception->type == EXCEPTION_WARNING || 
		exception->type == EXCEPTION_INFORMATION)
	    aal_gauge_resume();
	    
	return exception->options;
    }
	    
    for (i = 1; i < aal_log2(EXCEPTION_LAST); i++) {
	if ((1 << i) & exception->options) {
	    char *name = aal_exception_option_string(1 << i);
	    options = aal_list_append(options, name);
	}
    }
    
    if (options)
	options = aal_list_first(options);
    
    do {
	opt = progs_exception_prompt(exception->options);
    } while (opt == EXCEPTION_UNHANDLED && isatty(0));

    aal_list_free(options);
    options = NULL;
    
    if (exception->type == EXCEPTION_WARNING || 
	    exception->type == EXCEPTION_INFORMATION)
	aal_gauge_resume();
	    
    return opt;
}

