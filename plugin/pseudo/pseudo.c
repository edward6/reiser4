/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Handling of "pseudo" files representing unified access to meta data in
 * reiser4.
 */

/*
 * See http://namesys.com/v4/v4.html, and especially
 * http://namesys.com/v4/v4.html#syscall for basic information about reiser4
 * pseudo files, access to meta-data, reiser4() system call, etc.
 *
 * Pseudo files (PF) should be accessible from both reiser4() system call and
 * normal POSIX calls. Important point here is that reiser4() is supposed to
 * be fast. In particular it was argued that creation of inodes/dentries/files
 * during interpretation of reiser4() system call command language (RSCCL?) is
 * too expensive. Thus, RSCCL operates on its own light-weight handles for
 * objects involved. On the other hand, we need to provide traditional
 * interface to VFS based on inode/dentry/file.
 *

When assigning inode numbers to pseudo files, we use the last 2^62 oids of the 64 oid space.

     We implement each type of PF ("..key", "..foo") as
 *   new object plugin in addition to the standard object plugins (regular
 *   file, directory, pipe, and so on). 

 *   PF inodes require a pointer to the
 *   "host" object inode. 

     Adding yet another field to the generic
 *   reiser4_inode_info_data that will only be used for the PF seems to be an
 *   excess. Moreover, each PF type can require its own additional state,
 *   which is hard to predict in advance, and ability to freely add new PF
 *   types with rich and widely different semantics seems to be important for
 *   the reiser4.
 *
 *   This difficulty can be solved by observing that some fields in reiser4
 *   private part of inode are meaningless for PFs. Examples are: tail, hash,
 *   and stat-data plugin pointers, etc.
 *
 * PROPOSED SOLUTION
 *
 *   So, for now, we can reuse locality_id in reiser4 private part of inode to
 *   store number of host inode of PF, and create special object plugin for
 *   each PF type. 

 *   That plugin will use some other field in reiser4 private
 *   inode part that is meaning less for pseudo files (->extmask?) to
 *   determine type of PF and use appopriate low level PF operations
 *   (pseudo_ops) to implement VFS operations.

Use the pluginid field?

 *
 *   All this (hack-like) machinery has of course nothing to do with reiser4()
 *   system call that will use pseudo_ops directly.

 We do however need to define the methods that the reiser4 system call will use.

 *
 * NOTES
 *
 *  Special flag in reiser4_plugin_ref->flags to detect pseudo file.
(The plugin id?)
 *  Mark PF inode as loaded: ->flags | REISER4_LOADED
 *
 */

#if YOU_CAN_COMPILE_PSEUDO_CODE
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
