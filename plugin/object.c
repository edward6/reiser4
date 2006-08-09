/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/*
 * Examples of object plugins: file, directory, symlink, special file.
 *
 * Plugins associated with inode:
 *
 * Plugin of inode is plugin referenced by plugin-id field of on-disk
 * stat-data. How we store this plugin in in-core inode is not
 * important. Currently pointers are used, another variant is to store offsets
 * and do array lookup on each access.
 *
 * Now, each inode has one selected plugin: object plugin that
 * determines what type of file this object is: directory, regular etc.
 *
 * This main plugin can use other plugins that are thus subordinated to
 * it. Directory instance of object plugin uses hash; regular file
 * instance uses tail policy plugin.
 *
 * Object plugin is either taken from id in stat-data or guessed from
 * i_mode bits. Once it is established we ask it to install its
 * subordinate plugins, by looking again in stat-data or inheriting them
 * from parent.
 *
 * How new inode is initialized during ->read_inode():
 * 1 read stat-data and initialize inode fields: i_size, i_mode,
 *   i_generation, capabilities etc.
 * 2 read plugin id from stat data or try to guess plugin id
 *   from inode->i_mode bits if plugin id is missing.
 * 3 Call ->init_inode() method of stat-data plugin to initialise inode fields.
 *
 * NIKITA-FIXME-HANS: can you say a little about 1 being done before 3?  What
 * if stat data does contain i_size, etc., due to it being an unusual plugin?
 *
 * 4 Call ->activate() method of object's plugin. Plugin is either read from
 *    from stat-data or guessed from mode bits
 * 5 Call ->inherit() method of object plugin to inherit as yet un initialized
 *    plugins from parent.
 *
 * Easy induction proves that on last step all plugins of inode would be
 * initialized.
 *
 * When creating new object:
 * 1 obtain object plugin id (see next period)
 * NIKITA-FIXME-HANS: period?
 * 2 ->install() this plugin
 * 3 ->inherit() the rest from the parent
 *
 * We need some examples of creating an object with default and non-default
 * plugin ids.  Nikita, please create them.
 */

#include "../inode.h"

static int _bugop(void)
{
	BUG_ON(1);
	return 0;
}

#define bugop ((void *)_bugop)

static int _dummyop(void)
{
	return 0;
}

#define dummyop ((void *)_dummyop)

static int change_file(struct inode *inode, reiser4_plugin * plugin)
{
	/* cannot change object plugin of already existing object */
	return RETERR(-EINVAL);
}

static reiser4_plugin_ops file_plugin_ops = {
	.change = change_file
};

/*
 * Definitions of object plugins.
 */

file_plugin file_plugins[LAST_FILE_PLUGIN_ID] = {
	[UNIX_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = UNIX_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "reg",
			.desc = "regular file",
			.linkage = {NULL, NULL},
		},
		.inode_ops = {
			.permission = permission_common,
			.setattr = setattr_unix_file,
			.getattr = getattr_common
		},
		.file_ops = {
			.llseek = generic_file_llseek,
			.read = read_unix_file,
			.write = do_sync_write,
			.aio_write = generic_file_aio_write,
			.ioctl = ioctl_unix_file,
			.mmap = mmap_unix_file,
			.open = open_unix_file,
			.release = release_unix_file,
			.fsync = sync_unix_file,
			.sendfile = sendfile_unix_file
		},
		.as_ops = {
			.writepage = reiser4_writepage,
			.readpage = readpage_unix_file,
			.sync_page = block_sync_page,
			.writepages = writepages_unix_file,
			.set_page_dirty = reiser4_set_page_dirty,
			.readpages = readpages_unix_file,
			.prepare_write = prepare_write_unix_file,
			.commit_write =	commit_write_unix_file,
			.batch_write = batch_write_unix_file,
			.bmap = bmap_unix_file,
			.invalidatepage = reiser4_invalidatepage,
			.releasepage = reiser4_releasepage
		},
		.write_sd_by_inode = write_sd_by_inode_common,
		.flow_by_inode = flow_by_inode_unix_file,
		.key_by_inode = key_by_inode_and_offset_common,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create_object = create_object_common,	/* this is not inode_operations's create */
		.delete_object = delete_object_unix_file,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_unix_file,
		.can_add_link = can_add_link_common,
		.detach = dummyop,
		.bind = dummyop,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.init_inode_data = init_inode_data_unix_file,
		.cut_tree_worker = cut_tree_worker_common,
		.wire = {
			.write = wire_write_common,
			.read = wire_read_common,
			.get = wire_get_common,
			.size = wire_size_common,
			.done = wire_done_common
		}
	},
	[DIRECTORY_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = DIRECTORY_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "dir",
			.desc = "directory",
			.linkage = {NULL, NULL}
		},
		.inode_ops = {.create = NULL},
		.file_ops = {.owner = NULL},
		.as_ops = {.writepage = NULL},

		.write_sd_by_inode = write_sd_by_inode_common,
		.flow_by_inode = bugop,
		.key_by_inode = bugop,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common_dir,
		.create_object = create_object_common,
		.delete_object = delete_directory_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common_dir,
		.owns_item = owns_item_common_dir,
		.can_add_link = can_add_link_common,
		.can_rem_link = can_rem_link_common_dir,
		.detach = detach_common_dir,
		.bind = bind_common_dir,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_common_dir,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common_dir
		},
		.wire = {
			.write = wire_write_common,
			.read = wire_read_common,
			.get = wire_get_common,
			.size = wire_size_common,
			.done = wire_done_common
		},
		.init_inode_data = init_inode_ordering,
		.cut_tree_worker = cut_tree_worker_common,
	},
	[SYMLINK_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = SYMLINK_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "symlink",
			.desc = "symbolic link",
			.linkage = {NULL,NULL}
		},
		.inode_ops = {
			.readlink = generic_readlink,
			.follow_link = follow_link_common,
			.permission = permission_common,
			.setattr = setattr_common,
			.getattr = getattr_common
		},
		/* inode->i_fop of symlink is initialized by NULL in setup_inode_ops */
		.file_ops = {.owner = NULL},
		.as_ops = {.writepage = NULL},

		.write_sd_by_inode = write_sd_by_inode_common,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create_object = create_symlink,
		.delete_object = delete_object_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.can_add_link = can_add_link_common,
		.detach = dummyop,
		.bind = dummyop,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.init_inode_data = init_inode_ordering,
		.cut_tree_worker = cut_tree_worker_common,
		.destroy_inode = destroy_inode_symlink,
		.wire = {
			.write = wire_write_common,
			.read = wire_read_common,
			.get = wire_get_common,
			.size = wire_size_common,
			.done = wire_done_common
		}
	},
	[SPECIAL_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = SPECIAL_FILE_PLUGIN_ID,
			.pops = &file_plugin_ops,
			.label = "special",
			.desc =
			"special: fifo, device or socket",
			.linkage = {NULL, NULL}
		},
		.inode_ops = {
			.permission = permission_common,
			.setattr = setattr_common,
			.getattr = getattr_common
		},
		/* file_ops of special files (sockets, block, char, fifo) are
		   initialized by init_special_inode. */
		.file_ops = {.owner = NULL},
		.as_ops = {.writepage = NULL},

		.write_sd_by_inode = write_sd_by_inode_common,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create_object = create_object_common,
		.delete_object = delete_object_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.detach = dummyop,
		.bind = dummyop,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.init_inode_data = init_inode_ordering,
		.cut_tree_worker = cut_tree_worker_common,
		.wire = {
			.write = wire_write_common,
			.read = wire_read_common,
			.get = wire_get_common,
			.size = wire_size_common,
			.done = wire_done_common
		}
	},
	[CRC_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = CRC_FILE_PLUGIN_ID,
			.pops = &cryptcompress_plugin_ops,
			.label = "cryptcompress",
			.desc = "cryptcompress file",
			.linkage = {NULL, NULL}
		},
		.inode_ops = {
			.permission = permission_common,
			.setattr = setattr_cryptcompress,
			.getattr = getattr_common
		},
		.file_ops = {
			.llseek = generic_file_llseek,
			.read = read_cryptcompress,
			.write = write_cryptcompress,
			.aio_read = generic_file_aio_read,
			.mmap = mmap_cryptcompress,
			.release = release_cryptcompress,
			.fsync = sync_common,
			.sendfile = sendfile_cryptcompress
		},
		.as_ops = {
			.writepage = reiser4_writepage,
			.readpage = readpage_cryptcompress,
			.sync_page = block_sync_page,
			.writepages = writepages_cryptcompress,
			.set_page_dirty = reiser4_set_page_dirty,
			.readpages = readpages_crc,
			.prepare_write = prepare_write_common,
			.invalidatepage = reiser4_invalidatepage,
			.releasepage = reiser4_releasepage
		},
		.write_sd_by_inode = write_sd_by_inode_common,
		.flow_by_inode = flow_by_inode_cryptcompress,
		.key_by_inode = key_by_inode_cryptcompress,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_cryptcompress,
		.create_object = create_cryptcompress,
		.open_object = open_cryptcompress,
		.delete_object = delete_cryptcompress,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.detach = dummyop,
		.bind = dummyop,
		.safelink = safelink_common,
		.estimate = {
			.create = estimate_create_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.init_inode_data = init_inode_data_cryptcompress,
		.cut_tree_worker = cut_tree_worker_cryptcompress,
		.destroy_inode = destroy_inode_cryptcompress,
		.wire = {
			.write = wire_write_common,
			.read = wire_read_common,
			.get = wire_get_common,
			.size = wire_size_common,
			.done = wire_done_common
		}
	}
};

static int change_dir(struct inode *inode, reiser4_plugin * plugin)
{
	/* cannot change dir plugin of already existing object */
	return RETERR(-EINVAL);
}

static reiser4_plugin_ops dir_plugin_ops = {
	.change = change_dir
};

/*
 * definition of directory plugins
 */

dir_plugin dir_plugins[LAST_DIR_ID] = {
	/* standard hashed directory plugin */
	[HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = HASHED_DIR_PLUGIN_ID,
			.pops = &dir_plugin_ops,
			.label = "dir",
			.desc = "hashed directory",
			.linkage = {NULL, NULL}
		},
		.inode_ops = {
			.create = create_common,
			.lookup = lookup_common,
			.link = link_common,
			.unlink = unlink_common,
			.symlink = symlink_common,
			.mkdir = mkdir_common,
			.rmdir = unlink_common,
			.mknod = mknod_common,
			.rename = rename_common,
			.permission = permission_common,
			.setattr = setattr_common,
			.getattr = getattr_common
		},
		.file_ops = {
			.llseek = llseek_common_dir,
			.read = generic_read_dir,
			.readdir = readdir_common,
			.release = release_dir_common,
			.fsync = sync_common
		},
		.as_ops = {
			.writepage = bugop,
			.sync_page = bugop,
			.writepages = dummyop,
			.set_page_dirty = bugop,
			.readpages = bugop,
			.prepare_write = bugop,
			.commit_write = bugop,
			.bmap = bugop,
			.invalidatepage = bugop,
			.releasepage = bugop
		},
		.get_parent = get_parent_common,
		.is_name_acceptable = is_name_acceptable_common,
		.build_entry_key = build_entry_key_hashed,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_common,
		.rem_entry = rem_entry_common,
		.init = init_common,
		.done = done_common,
		.attach = attach_common,
		.detach = detach_common,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink = dir_estimate_unlink_common
		}
	},
	/* hashed directory for which seekdir/telldir are guaranteed to
	 * work. Brain-damage. */
	[SEEKABLE_HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = SEEKABLE_HASHED_DIR_PLUGIN_ID,
			.pops = &dir_plugin_ops,
			.label = "dir32",
			.desc = "directory hashed with 31 bit hash",
			.linkage = {NULL, NULL}
		},
		.inode_ops = {
			.create = create_common,
			.lookup = lookup_common,
			.link = link_common,
			.unlink = unlink_common,
			.symlink = symlink_common,
			.mkdir = mkdir_common,
			.rmdir = unlink_common,
			.mknod = mknod_common,
			.rename = rename_common,
			.permission = permission_common,
			.setattr = setattr_common,
			.getattr = getattr_common
		},
		.file_ops = {
			.llseek = llseek_common_dir,
			.read =	generic_read_dir,
			.readdir = readdir_common,
			.release = release_dir_common,
			.fsync = sync_common
		},
		.as_ops = {
			.writepage = bugop,
			.sync_page = bugop,
			.writepages = dummyop,
			.set_page_dirty = bugop,
			.readpages = bugop,
			.prepare_write = bugop,
			.commit_write = bugop,
			.bmap = bugop,
			.invalidatepage = bugop,
			.releasepage = bugop
		},
		.get_parent = get_parent_common,
		.is_name_acceptable = is_name_acceptable_common,
		.build_entry_key = build_entry_key_seekable,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_common,
		.rem_entry = rem_entry_common,
		.init = init_common,
		.done = done_common,
		.attach = attach_common,
		.detach = detach_common,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink = dir_estimate_unlink_common
		}
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
