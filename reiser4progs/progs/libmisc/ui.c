/*
    ui.c -- common for all progs function for work with libreadline.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <pty.h>
#include <stdio.h>
#include <aal/aal.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_LIBREADLINE

#include <readline/readline.h>
#include <readline/history.h>

#ifndef HAVE_RL_COMPLETION_MATCHES
#define rl_completion_matches completion_matches
#endif

#ifndef rl_compentry_func_t
#define rl_compentry_func_t void
#endif

static aal_list_t *possibilities = NULL;

#endif /* HAVE_LIBREADLINE */

/* This function gets user enter */
char *progs_ui_readline(
    char *prompt		/* prompt to be printed */
) {
    char *line;
    
    aal_assert("umka-1021", prompt != NULL, return NULL);
    
#ifdef HAVE_LIBREADLINE
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

/* Gets screen width */
uint16_t progs_ui_screen_width(void) {
    struct winsize winsize;
    
    if (ioctl(2, TIOCGWINSZ, &winsize))
	return 0;
    
    return winsize.ws_col;
}

void progs_ui_wipe_line(void *stream) {
    char *buff;
    int i, width = progs_ui_screen_width();
    
    if (!(buff = aal_calloc(width + 1, 0)))
	return;
    
    aal_strncat(buff, "\r", 1);
    for (i = 0; i < width - 2; i++)
	aal_strncat(buff, " ", 1);

    aal_strncat(buff, "\r", 1);

    fprintf(stream, buff);
    aal_free(buff);
}

/* Constructs exception message */
void progs_ui_print_wrap(void *stream, char *text) {
    uint16_t width;
    char *word, *line;

    aal_list_t *walk = NULL;
    aal_list_t *list = NULL;

    if (!stream || !text)
	return;
    
    line = NULL;
    width = progs_ui_screen_width();

    while ((word = aal_strsep(&text, " "))) {
	if (!line || aal_strlen(line) + aal_strlen(word) > width) {
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

    if (list) {
        list = aal_list_first(list);
    
	/* Printing message */
	aal_list_foreach_forward(walk, list) {
	    char *line = (char *)walk->item;

	    if (line && aal_strlen(line) > 0) {
		fprintf(stream, "%s\n", line);
		aal_free(line);
	    }
	}
    
	aal_list_free(list);
    }
}

#ifdef HAVE_LIBREADLINE

static void upper(char *dst, const char *src) {
    int i = 0;
    const char *s;

    s = src;
    while (*s) dst[i++] = toupper(*s++);
    dst[i] = '\0';
}

static char *progs_ui_generator(char *text, int state) {
    char *opt;
    char s[80], s1[80];
    static aal_list_t *cur = NULL;
    
    if (!state) 
	cur = possibilities;
    
    while (cur) {
	aal_memset(s, 0, sizeof(s));
	aal_memset(s1, 0, sizeof(s1));
	
	opt = (char *)cur->item;
	cur = cur->next;

	upper(s, opt); upper(s1, text);
	if (!aal_strncmp(s, s1, aal_strlen(s1)))
	    return aal_strdup(opt);
    }
    
    return NULL;
}

static char **progs_ui_complete(char *text, int start, int end) {
    return rl_completion_matches(text,
	(rl_compentry_func_t *)progs_ui_generator);
}

void progs_ui_set_possibilities(aal_list_t *list) {
    possibilities = list;
}

aal_list_t *progs_ui_get_possibilities(void) {
    return possibilities;
}

#endif

static void _init(void) __attribute__((constructor));

static void _init(void) {
#ifdef HAVE_LIBREADLINE
    rl_initialize();
    rl_attempted_completion_function = 
	(CPPFunction *)progs_ui_complete;
#endif
}

