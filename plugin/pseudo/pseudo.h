/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Handling of "pseudo" files representing unified access to meta data in
 * reiser4. See pseudo.c for more comments.
 */

#if !defined( __REISER4_PSEUDO_H__ )
#define __REISER4_PSEUDO_H__

#if YOU_CAN_COMPILE_PSEUDO_CODE

/**
 * low level operations on the pseudo files.
 *
 * Methods from this interface are directly callable by reiser4 system call.
 *
 * This operation structure looks suspiciously like yet another plugin
 * type. Doing so would simplify some things. For example, there are already
 * functions to look up plugin by name, dynamic loading is planned, etc.
 *
 */
typedef struct pseudo_ops {

	/**
	 * lookup method applicable to this pseudo file by method name.
	 *
	 * This is for something like "foo/..acl/dup", here "../acl" is the
	 * name of a pseudo file, and "dup" is name of an operation (method)
	 * applicable to "../acl". Once "..acl" is resolved to ACL object,
	 * ->method_lookup( "dup" ) can be called to get operation.
	 *
	 */
	int (*method_lookup) (const char *name, int len, reiser4_syscall_method * method);

	/**
	 * generic name of this pseudo object "..acl", "..key", etc.
	 */
	const char *name;

	/* 
	 * FIXME-NIKITA some other operations. Reiser4 syntax people should
	 * add something here.
	 */

} pseudo_ops;

#endif

/* __REISER4_PSEUDO_H__ */
#endif

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * scroll-step: 1
 * End:
 */
