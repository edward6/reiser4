/*
    ui.c -- callback function for using them for ask user enter something.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef UI_H
#define UI_H

typedef int (*aal_check_numeric_func_t) (int64_t);

typedef int64_t (*aal_get_numeric_func_t) (const char *, 
    int64_t, aal_check_numeric_func_t);

extern void aal_ui_set_numeric_handler(aal_get_numeric_func_t func);
extern aal_get_numeric_func_t aal_ui_get_numeric_handler(void);

extern int64_t aal_ui_get_numeric(const char *prompt, int64_t defvalue, 
    aal_check_numeric_func_t check_func);

#endif

