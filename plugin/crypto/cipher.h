#if !defined( __FS_REISER4_CRYPT_H__ )
#define __FS_REISER4_CRYPT_H__

#include <linux/crypto.h>

/* Crypto transforms involved in ciphering process and
   supported by reiser4 via appropriate transform plugins */
typedef enum {
	CIPHER_TFM,       /* cipher transform */
	DIGEST_TFM,       /* digest transform */
	LAST_TFM
} reiser4_tfm;

/* This represents a transform from the set above */
typedef struct reiser4_tfma {
	reiser4_plugin * plug;     /* transform plugin */
	struct crypto_tfm * tfm;   /* per-transform allocated info,
                                      belongs to the crypto-api. */
} reiser4_tfma_t;

/* This contains cipher related info copied from user space */
typedef struct crypto_data {
	int keysize;    /* key size */
	__u8 * key;     /* uninstantiated key */
	int keyid_size; /* size of passphrase */
	__u8 * keyid;   /* passphrase (uninstantiated keyid) */
} crypto_data_t;

/* Dynamically allocated per instantiated key info */
typedef struct crypto_stat {
	reiser4_tfma_t tfma[LAST_TFM];
//      cipher_key_plugin * kplug; *//* key manager responsible for
//                                      inheriting, validating, etc... */
	__u8 * keyid;                /* fingerprint (instantiated keyid) of
					the cipher key prepared by digest
					plugin, supposed to be stored in
					disk stat-data */
	int inst;                    /* this indicates if the ciper key
					is instantiated in the system */
	int keysize;                 /* uninstantiated key size (bytes),
					supposed to be stored in disk
					stat-data */
	int keyload_count;           /* number of the objects which has
					this crypto-stat attached */
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
