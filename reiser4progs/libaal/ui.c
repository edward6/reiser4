/*
    ui.c -- callback function for using them for asking user to enter something.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

static aal_get_numeric_func_t get_numeric_handler = NULL;

void aal_ui_set_numeric_handler(aal_get_numeric_func_t func) {
    get_numeric_handler = func;
}

aal_get_numeric_func_t aal_ui_get_numeric_handler(void) {
    return get_numeric_handler;
}

int64_t aal_ui_get_numeric(const char *prompt, 
    int64_t value, aal_check_numeric_func_t check_func) 
{
    if (!get_numeric_handler)
	return ~0ll;
    
    return get_numeric_handler(prompt, value, check_func);
}

