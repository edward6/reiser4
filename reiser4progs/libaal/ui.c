/*
    ui.c -- callback function for using them for asking user to enter something.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

static aal_numeric_func_t numeric_handler = NULL;

void aal_ui_set_numeric_handler(aal_numeric_func_t func) {
    numeric_handler = func;
}

aal_numeric_func_t aal_ui_get_numeric_handler(void) {
    return numeric_handler;
}

int64_t aal_ui_get_numeric(int64_t defvalue, 
    aal_check_numeric_func_t check_func, void *data,
    const char *format, ...) 
{
    char buff[256];
    va_list arg_list;
    
    if (!numeric_handler)
	return ~0ll;
    
    aal_memset(buff, sizeof(buff), 0);
    
    va_start(arg_list, format);
    aal_vsnprintf(buff, sizeof(buff), format, arg_list);
    va_end(arg_list);
    
    return numeric_handler(buff, defvalue, check_func, data);
}

static aal_alpha_func_t alpha_handler = NULL;

void aal_ui_set_alpha_handler(aal_alpha_func_t func) {
    alpha_handler = func;
}

aal_alpha_func_t aal_ui_get_alpha_handler(void) {
    return alpha_handler;
}

char *aal_ui_get_alpha(char *defvalue, 
    aal_check_alpha_func_t check_func, void *data,
    const char *format, ...)
{
    char buff[256];
    va_list arg_list;
    
    if (!alpha_handler)
	return NULL;
    
    aal_memset(buff, sizeof(buff), 0);
    
    va_start(arg_list, format);
    aal_vsnprintf(buff, sizeof(buff), format, arg_list);
    va_end(arg_list);
    
    return alpha_handler(buff, defvalue, check_func, data);
}

