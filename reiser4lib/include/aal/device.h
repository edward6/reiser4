/*
    device.h -- device independent interface and block-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DEVICE_H
#define DEVICE_H

#include <aal/aal.h>

typedef unsigned long blk_t;
typedef unsigned long count_t;

struct aal_device_ops;

struct aal_device {
    int flags;
    void *data;
    void *entity;
    char name[256];
    uint32_t blocksize;
    struct aal_device_ops *ops;
};

typedef struct aal_device aal_device_t;

struct aal_device_ops {
    int (*read)(aal_device_t *, void *, blk_t, count_t);
    int (*write)(aal_device_t *, void *, blk_t, count_t);
    int (*sync)(aal_device_t *);
    int (*flags)(aal_device_t *);
    int (*equals)(aal_device_t *, aal_device_t *);
    uint32_t (*stat)(aal_device_t *);
    count_t (*len)(aal_device_t *);
};

struct aal_device_block {
    int flags;
    void *data;
    uint64_t offset;
    aal_device_t *device;
};

typedef struct aal_device_block aal_device_block_t;

extern aal_device_t *aal_device_open(struct aal_device_ops *ops, uint32_t blocksize, 
    int flags, void *data);

extern void aal_device_close(aal_device_t *device);

extern int aal_device_set_blocksize(aal_device_t *device, uint32_t blocksize);
extern uint32_t aal_device_get_blocksize(aal_device_t *device);

extern int aal_device_read(aal_device_t *device, void *buff, blk_t block, count_t count);
extern int aal_device_write(aal_device_t *device, void *buff, blk_t block, count_t count);
extern int aal_device_sync(aal_device_t *device);
extern int aal_device_flags(aal_device_t *device);
extern int aal_device_equals(aal_device_t *device1, aal_device_t *device2);
extern uint32_t aal_device_stat(aal_device_t *device);
extern count_t aal_device_len(aal_device_t *device);
extern char *aal_device_name(aal_device_t *device);

/* Block-working functions */
extern aal_device_block_t *aal_device_alloc_block(aal_device_t *device, blk_t blk, char c);
extern aal_device_block_t *aal_device_read_block(aal_device_t *device, blk_t blk);
extern int aal_device_write_block(aal_device_t *device, aal_device_block_t *block);
extern blk_t aal_device_get_block_location(aal_device_block_t *block);
extern void aal_device_set_block_location(aal_device_block_t *block, blk_t blk);
extern void aal_device_free_block(aal_device_block_t *block);

#define B_DIRTY 0 

#define aal_block_get_size(block) aal_device_get_blocksize(block->device)

#define aal_block_mark_dirty(block) do { block->flags |= (1 << B_DIRTY); } while (0)
#define aal_block_dirty(block)	    (block->flags & (1 << B_DIRTY))
#define aal_block_makr_clean(block) do { block->flags &= ~(1 << B_DIRTY) } while (0) 

#endif

