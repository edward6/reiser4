/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Handling of "pseudo" files representing unified access to meta data in
   reiser4. See pseudo.c for more comments. */

#if !defined( __REISER4_PSEUDO_H__ )
#define __REISER4_PSEUDO_H__

#include "../plugin_header.h"
#include "../../key.h"

#include <linux/fs.h>

/* low level operations on the pseudo files.
  
   Methods from this interface are directly callable by reiser4 system call.
  
   This operation structure looks suspiciously like yet another plugin
   type. Doing so would simplify some things. For example, there are already
   functions to look up plugin by name, dynamic loading is planned, etc.
  
*/
typedef struct pseudo_plugin {
	plugin_header h;

	int (*try) (const struct inode *parent, const char *name);
	/* lookup method applicable to this pseudo file by method name.
	  
	   This is for something like "foo/..acl/dup", here "../acl" is the
	   name of a pseudo file, and "dup" is name of an operation (method)
	   applicable to "../acl". Once "..acl" is resolved to ACL object,
	   ->lookup( "dup" ) can be called to get operation.
	  
	*/
	int (*lookup) (const char *name);

	oid_t (*makeid)(void);

	/* NOTE-NIKITA some other operations. Reiser4 syntax people should
	   add something here. */

} pseudo_plugin;

typedef struct pseudo_info {
	pseudo_plugin *plugin;
	struct inode  *host;
} pseudo_info_t;

extern struct inode *lookup_pseudo(struct inode *parent, const char *name);

typedef enum { 
	PSEUDO_TEST_ID,
	LAST_PSEUDO_ID
} reiser4_pseudo_id;

/* __REISER4_PSEUDO_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
