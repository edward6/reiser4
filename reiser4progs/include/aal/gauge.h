/*
    gauge.h -- progress-bar structures.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef GAUGE_H
#define GAUGE_H

typedef struct aal_gauge aal_gauge_t;

enum aal_gauge_type {
    GAUGE_PERCENTAGE,
    GAUGE_INDICATOR,
    GAUGE_SILENT
};

typedef enum aal_gauge_type aal_gauge_type_t;

enum aal_gauge_state {
    GAUGE_STARTED,
    GAUGE_RUNNING,
    GAUGE_PAUSED,
    GAUGE_DONE,
};

typedef enum aal_gauge_state aal_gauge_state_t;

typedef void (*aal_gauge_handler_t)(aal_gauge_t *);

struct aal_gauge {
    aal_gauge_type_t type;
    aal_gauge_state_t state;
    aal_gauge_handler_t handler;

    void *data;
    
    char name[256];
    uint32_t value;
};

extern errno_t aal_gauge_create(aal_gauge_type_t type, 
    const char *name, aal_gauge_handler_t handler, void *data);

extern void aal_gauge_update(uint32_t value);

extern void aal_gauge_rename(const char *name, ...) 
    __check_format__(printf, 1, 2);

extern void aal_gauge_reset(void);
extern void aal_gauge_start(void);
extern void aal_gauge_done(void);
extern void aal_gauge_touch(void);
extern void aal_gauge_free(void);
extern void aal_gauge_pause(void);

#endif

