/*
    ui.h -- common for all progs function for work with libreadline.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef PROGS_UI
#define PROGS_UI

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

extern char *progs_ui_readline(char *prompt);
extern uint16_t progs_ui_screen_width(void);
extern void progs_ui_print_wrap(void *stream, char *text);

#ifdef HAVE_LIBREADLINE
extern void progs_ui_set_possibilities(aal_list_t *list);
extern aal_list_t *progs_ui_get_possibilities(void);
#endif

#endif

