/*
    device.h -- device independent interface and block-working functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DEVICE_H
#define DEVICE_H

#include <aal/aal.h>

/* 
    This types is used for keeping the block number and block count 
    value. They are needed to be increase source code maintainability.

    For instance, there is some function:

    blk_t some_func(void);
    
    It is clear to any reader, that this function is working with block
    number, it returns block number.

    Yet another variant of this function:

    uint64_t some_func(void);
    
    This function may return anything. This is may be bytes, blocks, etc.
*/
typedef uint64_t blk_t;
typedef uint64_t count_t;

struct aal_device_ops;

/*
    Abstract device structure. It consists of flags device opened with, user 
    specified data, some opaque entity (for standard file it is file descriptor), 
    name of device (for instance, /dev/hda2), block size of device and device
    operations.
*/
struct aal_device {
    int flags;
    void *data;
    void *entity;
    uint16_t blocksize;
    char name[256], error[256];
    struct aal_device_ops *ops;
};

typedef struct aal_device aal_device_t;

/* 
    Operations which may be performed on the device. Some of them may not
    be implemented.
*/
struct aal_device_ops {
    errno_t (*read)(aal_device_t *, void *, blk_t, count_t);
    errno_t (*write)(aal_device_t *, void *, blk_t, count_t);
    errno_t (*sync)(aal_device_t *);
    int (*flags)(aal_device_t *);
    errno_t (*equals)(aal_device_t *, aal_device_t *);
    uint32_t (*stat)(aal_device_t *);
    count_t (*len)(aal_device_t *);
};

/*
    Disk block structure. It is a replica of struct buffer_head from the linux 
    kernel. It consists of flags (dirty, clean, etc), data (pointer to data of
    block), block size, offset (offset in bytes where block is placed on device),
    and pointer to device, block opened on.
*/
struct aal_block {
    int flags;
    void *data;
    uint16_t size;
    uint64_t offset;
    aal_device_t *device;
};

typedef struct aal_block aal_block_t;

extern aal_device_t *aal_device_open(struct aal_device_ops *ops, 
    uint16_t blocksize, int flags, void *data);

extern void aal_device_close(aal_device_t *device);

extern errno_t aal_device_set_bs(aal_device_t *device, 
    uint16_t blocksize);

extern uint16_t aal_device_get_bs(aal_device_t *device);

extern errno_t aal_device_read(aal_device_t *device, 
    void *buff, blk_t block, count_t count);

extern errno_t aal_device_write(aal_device_t *device, 
    void *buff, blk_t block, count_t count);

extern errno_t aal_device_sync(aal_device_t *device);
extern int aal_device_flags(aal_device_t *device);

extern int aal_device_equals(aal_device_t *device1, 
    aal_device_t *device2);

extern uint32_t aal_device_stat(aal_device_t *device);
extern count_t aal_device_len(aal_device_t *device);
extern char *aal_device_name(aal_device_t *device);
extern char *aal_device_error(aal_device_t *device);

/* Block-working functions */
extern aal_block_t *aal_block_alloc(aal_device_t *device, 
    blk_t blk, char c);

extern aal_block_t *aal_block_read(aal_device_t *device, 
    blk_t blk);

extern errno_t aal_block_reread(aal_block_t *block, 
    aal_device_t *device, blk_t blk);

extern errno_t aal_block_write(aal_block_t *block);

extern void aal_block_free(aal_block_t *block);
extern blk_t aal_block_get_nr(aal_block_t *block);
extern void aal_block_set_nr(aal_block_t *block, blk_t blk);
extern uint32_t aal_block_size(aal_block_t *block);

#define B_DIRTY 0 

#define aal_block_is_dirty(block) block->flags & (1 << B_DIRTY)
#define aal_block_is_clean(block) (!aal_block_is_dirty(block))

#define aal_block_dirty(block) block->flags |=  (1 << B_DIRTY)
#define aal_block_clean(block) block->flags &= ~(1 << B_DIRTY)

#endif

