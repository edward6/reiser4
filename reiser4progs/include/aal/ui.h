/*
    ui.c -- callback function for using them for ask user enter something.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef UI_H
#define UI_H

typedef int (*aal_check_numeric_func_t) (int64_t, void *);

typedef int64_t (*aal_numeric_func_t) (const char *, 
    int64_t, aal_check_numeric_func_t, void *);

typedef int (*aal_check_alpha_func_t) (char *, void *);

typedef char *(*aal_alpha_func_t) (const char *, 
    char *, aal_check_alpha_func_t, void *);

extern void aal_ui_set_numeric_handler(aal_numeric_func_t func);
extern aal_numeric_func_t aal_ui_get_numeric_handler(void);

extern int64_t aal_ui_get_numeric(int64_t defvalue, 
    aal_check_numeric_func_t check_func, void *, 
    const char *format, ...);

extern void aal_ui_set_alpha_handler(aal_alpha_func_t func);
extern aal_alpha_func_t aal_ui_get_alpha_handler(void);

extern char *aal_ui_get_alpha(char *defvalue, 
    aal_check_alpha_func_t check_func, void *, 
    const char *format, ...);

#endif

