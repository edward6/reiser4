/*
    progs.c - main common code for reiser4 programs. 
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <progs/progs.h>

int progs_verbose = 0;
FILE *progs_progress = NULL;

void progs_init() {
    progs_progress = stdout;
}

void progs_set_verbose() {
    progs_verbose = 1;
}

FILE *progs_get_progress() {
    return progs_progress;
}

void progs_set_progress(FILE *stream) {
    progs_progress = stream;
}

