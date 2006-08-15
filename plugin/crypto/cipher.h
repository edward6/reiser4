/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* This file contains definitions for the objects operated
   by reiser4 key manager, which is something like keyring
   wrapped by appropriate reiser4 plugin */

#if !defined( __FS_REISER4_CRYPT_H__ )
#define __FS_REISER4_CRYPT_H__

#include <linux/crypto.h>

/* Transform actions involved in ciphering process and
   supported by reiser4 via appropriate transform plugins */
typedef enum {
	CIPHER_TFM,       /* cipher transform */
	DIGEST_TFM,       /* digest transform */
	LAST_TFM
} reiser4_tfm;

/* This represents a transform action in reiser4 */
typedef struct reiser4_tfma {
	reiser4_plugin * plug;     /* transform plugin */
	struct crypto_tfm * tfm;   /* low-level info, operated by
				      linux crypto-api (see linux/crypto) */
} reiser4_tfma_t;

/* key info imported from user space */
typedef struct crypto_data {
	int keysize;    /* uninstantiated key size */
	__u8 * key;     /* uninstantiated key */
	int keyid_size; /* size of passphrase */
	__u8 * keyid;   /* passphrase */
} crypto_data_t;

/* This object contains all needed infrastructure to implement
   cipher transform. This is operated (allocating, inheriting,
   validating, binding to host inode, etc..) by reiser4 key manager.

   This info can be allocated in two cases:
   1. importing a key from user space.
   2. reading inode from disk */
typedef struct crypto_stat {
	reiser4_tfma_t tfma[LAST_TFM];
//      cipher_key_plugin * kplug; /* key manager */
	__u8 * keyid;              /* key fingerprint, created by digest plugin,
				      using uninstantiated key and passphrase.
				      supposed to be stored in disk stat-data */
	int inst;                  /* this indicates if the cipher key is
				      instantiated (case 1 above) */
	int keysize;               /* uninstantiated key size (bytes), supposed
				      to be stored in disk stat-data */
	int keyload_count;         /* number of the objects which has this
				      crypto-stat attached */
} crypto_stat_t;

#endif /* __FS_REISER4_CRYPT_H__ */

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
