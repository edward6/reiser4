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
 * Pseudo files should be accessible from both reiser4() system call and
 * normal POSIX calls. Important point here is that reiser4() is supposed to
 * be fast. In particular it was argued that creation of inodes/dentries/files
 * during interpretation of reiser4() system call command language (RSCCL?) is
 * too expensive. Thus, RSCCL operates on its own light-weight handles for
 * objects involved. On the other hand, we need to provide traditional
 * interface to VFS based on inode/dentry/file.
 *
 * Source code organization: currently everything is in pseudo.[ch], mainly
 * because there not much code written. Probably pseudo/ should be created and
 * each pseudo file type will have special .[ch] pair there. Or may be
 * plugin/pseudo/ is better.
 *
 *
 * OPEN ISSUES
 *
 *
 * (A) 
 *
 *   To avoid code duplication, it seems rasonable to design such a
 *   unified interface to pseudo files directory suitable for use by RSCCL and
 *   implement VFS operations on the top of it.
 *
 *   Problem is that RSCCL is not well defined as of now.
 *
 *
 * (B) 
 *
 *   Another complication is with inode numbers: it is not clear what
 *   inode numbers should be assigned to inodes created for pseudo files (in
 *   the case of VFS access paths), and to what super block(s) they have to
 *   be attached.
 *
 *   Different schemata can be devised for this. For now it is better to stick
 *   to something really simple. For eaxmple, we can use existing reiser4
 *   super block, and assign the same otherwise invalid inode number to all
 *   pseudo files. Then, find_actor passed to the iget can be used to
 *   distinguish them.
 *
 *
 * (C) 
 *
 *   How to implement VFS operations? One solution is to implement new
 *   inode flavour completely separate from other reiser4 inodes, with
 *   different private part, different set of VFS ops, etc. This is easier to
 *   do in 2.5., because no changes in any global data structures are longer
 *   necessary.
 *
 *   On the other hand, we can implement each type of pseudo file ("..key",
 *   "..foo") as new object plugin in addition to the standard object plugins
 *   (regular file, directory, pipe, and so on). This has an advantage on
 *   reusing existing object plugin methods.
 *
 *   Problems with latter approach is that pseudo file inode has to store some
 *   additional information somewhere. At the very least, pointer to the
 *   "host" object inode is required. Adding yet another field to the generic
 *   reiser4_inode_info_data that will only be used for the pseudo files seems
 *   to be an excess. Moreover, each pseudo file type can require its own
 *   additional state, which is hard to predict in advance, and ability to
 *   freely add new pseudo files with rich and widely different semantics
 *   seems to be important for the reiser4.
 *
 *
 *
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
