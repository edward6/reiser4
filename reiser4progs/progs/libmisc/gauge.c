/*
    gauge.c -- common for all progs gauge fucntions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <stdio.h>
#include <aal/aal.h>

#define GAUGE_BITS_SIZE 4

static inline void progs_gauge_blit(void) {
    static short bitc = 0;
    static const char bits[] = "|/-\\";

    putc(bits[bitc], stderr);
    putc('\b', stderr);
    fflush(stderr);
    bitc++;
    bitc %= GAUGE_BITS_SIZE;
}

/* This functions "draws" gauge header */
static inline void progs_gauge_header(
    const char *name,		/* gauge name */
    aal_gauge_type_t type	/* gauge type */
) {
    if (name) {
	if (type != GAUGE_SILENT)
	    fprintf(stderr, "\r%s: ", name);
	else
	    fprintf(stderr, "\r%s...", name);
    }
}

/* This function "draws" gauge footer */
static inline void progs_gauge_footer(
    const char *name,	    /* footer name */
    aal_gauge_type_t type   /* gauge type */
) {
    if (name)
	fputs(name, stderr);
}

void progs_gauge_handler(aal_gauge_t *gauge) {
    if (gauge->state == GAUGE_PAUSED) {
	putc('\r', stderr);
	fflush(stderr);
	return;
    }
	
    if (gauge->state == GAUGE_STARTED)
	progs_gauge_header(gauge->name, gauge->type);
	
    switch (gauge->type) {
	case GAUGE_PERCENTAGE: {
	    unsigned int i;
	    char display[10] = {0};
		
	    sprintf(display, "%d%%", gauge->value);
	    fputs(display, stderr);
		
	    for (i = 0; i < strlen(display); i++)
		fputc('\b', stderr);
	    break;
	}
	case GAUGE_INDICATOR: {
	    progs_gauge_blit();
	    break;
	}
	case GAUGE_SILENT: break;
    }

    if (gauge->state == GAUGE_DONE)
	progs_gauge_footer("done\n", gauge->type);
    
    if (gauge->state == GAUGE_FAILED)
	progs_gauge_footer("failed\n", gauge->type);
	
    fflush(stderr);
}

