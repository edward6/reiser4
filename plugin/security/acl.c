/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "acl.h"

#include "../../debug.h"
#include "../../dformat.h"
#include "../../inode.h"

#include <linux/posix_acl.h>
#include <linux/xattr_acl.h>

static int
check_write(struct inode *inode, umode_t mode, int mask)
{
	int result;

	result = 0;
	if (mask & MAY_WRITE) {
		/*
		 * Nobody gets write access to a read-only fs.
		 */
		if (IS_RDONLY(inode) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			result = -EROFS;
		/*
		 * Nobody gets write access to an immutable file.
		 */
		else if (IS_IMMUTABLE(inode))
			result = -EACCES;
	}
	return result;
}

static int
check_capabilities(umode_t mode, int mask)
{
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	/*
	 * Adjusted to match POSIX 1003.1e draft 17 more thoroughly. See
	 * http://marc.theaimsgroup.com/?l=linux-kernel&m=107253552623787&w=2
	 */
	if (!(mask & MAY_EXEC) || (mode & S_IXUGO) || S_ISDIR(mode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;
	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (mask == MAY_READ || (S_ISDIR(mode) && !(mask & MAY_WRITE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}

static int
check_mode(umode_t mode, int mask)
{
	/*
	 * If the DACs are ok we don't need any capability check.
	 */
	if (((mode & mask & (MAY_READ|MAY_WRITE|MAY_EXEC)) == mask))
		return 0;
	else
		return check_capabilities(mode, mask);
}

static int
check_group(struct inode *inode, umode_t mode, int mask)
{
	if (in_group_p(inode->i_gid))
		mode >>= 3;
	return check_mode(mode, mask);
}

static struct posix_acl *
inode_get_acl(struct inode *inode)
{
	return reiser4_inode_data(inode)->perm_plugin_data.acl_perm_info.access;
}

static struct posix_acl *
inode_get_default_acl(struct inode *inode)
{
	return reiser4_inode_data(inode)->perm_plugin_data.acl_perm_info.access;
}

static void
inode_set_acl(struct inode *inode, struct posix_acl *acl)
{
	struct posix_acl *old;

	old = inode_get_acl(inode);
	if (old != NULL)
		posix_acl_release(old);
	reiser4_inode_data(inode)->perm_plugin_data.acl_perm_info.access = acl;
}

static void
inode_set_default_acl(struct inode *inode, struct posix_acl *acl)
{
	struct posix_acl *old;

	old = inode_get_default_acl(inode);
	if (old != NULL)
		posix_acl_release(old);
	reiser4_inode_data(inode)->perm_plugin_data.acl_perm_info.dfault = acl;
}

static struct posix_acl *
get_acl(struct inode *inode, int type)
{
	switch(type) {
	case ACL_TYPE_ACCESS:
		return inode_get_acl(inode);
	case ACL_TYPE_DEFAULT:
		return inode_get_default_acl(inode);
	default:
		wrong_return_value("nikita-3444", "type");
	}
	return NULL;
}

reiser4_internal int
mask_ok_acl(struct inode *inode, int mask)
{
	umode_t mode;
	struct posix_acl *acl;
	int result;

	mode = inode->i_mode;

	result = check_write(inode, mode, mask);
	if (result == 0) {
		if (current->fsuid == inode->i_uid)
			result = check_mode(mode >> 6, mask);
		else {
			result = check_group(inode, mode, mask);
			acl = inode_get_acl(inode);
			if (acl != NULL) {
				result = posix_acl_permission(inode, acl, mask);
				if (result == -EACCES)
					result = check_capabilities(mode, mask);
			}
		}
	}
	return result;
}

typedef struct {
	d16 tag;
	d16 perm;
	d32 id;
} reiser4_acl_entry;

typedef struct {
	d16 count;
} reiser4_acl_header;


static void
move_on(int *length, char **area, int size_of)
{
	*length -= size_of;
	*area += size_of;
	assert("nikita-617", *length >= 0);
}

static int
read_ace(struct posix_acl *acl, int no, char **area, int *len)
{
	reiser4_acl_entry *ace;

	ace = (reiser4_acl_entry *)*area;
	if (*len < sizeof *ace)
		return -EIO;

	acl->a_entries[no].e_tag  = d16tocpu(&ace->tag);
	acl->a_entries[no].e_perm = d16tocpu(&ace->perm);
	switch(acl->a_entries[no].e_tag) {
	case ACL_USER_OBJ:
	case ACL_GROUP_OBJ:
	case ACL_MASK:
	case ACL_OTHER:
		acl->a_entries[no].e_id = ACL_UNDEFINED_ID;
		break;
	case ACL_USER:
	case ACL_GROUP:
		acl->a_entries[no].e_id = d32tocpu(&ace->id);
		break;
	default:
		return RETERR(-EIO);
	}
	move_on(len, area, sizeof *ace);
	return 0;
}

static reiser4_xattr_plugin xattr_acl_handlers[];
static reiser4_xattr_plugin xattr_acl_trigger_handlers[];

static xattr_namespace acl_trigger_namespace = {
	.linkage = TYPE_SAFE_LIST_HEAD_INIT(acl_trigger_namespace.linkage),
	.plug    = xattr_acl_trigger_handlers
};

static int init_acl(reiser4_plugin *plugin)
{
	xattr_add_common_namespace(&acl_trigger_namespace);
	return 0;
}

/* this is called by ->present method of static_stat_data plugin when plugin
 * extension is present that contains ACL plugin. */
static int
load_acl(struct inode * inode, reiser4_plugin * plugin, char **area, int *len)
{
	reiser4_acl_header *head;
	int count;
	struct posix_acl *acl;
	int result;

	head = (reiser4_acl_header *)*area;
	if (*len < sizeof *head)
		return RETERR(-EIO);
	count = d16tocpu(&head->count);
	move_on(len, area, sizeof *head);

	acl = posix_acl_alloc(count, GFP_KERNEL);
	if (acl != NULL) {
		int i;

		for (i = 0, result = 0; i < count && result == 0; ++ i)
			result = read_ace(acl, i, area, len);
		if (result == 0)
			result = reiser4_set_acl(inode, ACL_TYPE_ACCESS, acl);
		if (result != 0)
			inode_set_acl(inode, NULL);
	} else
		result = RETERR(-ENOMEM);
	return result;
}

static int
save_len_acl(struct inode * inode, reiser4_plugin * plugin)
{
	struct posix_acl *acl;

	acl = inode_get_acl(inode);
	if (acl != 0)
		return
			sizeof(reiser4_acl_header) +
			acl->a_count * sizeof(reiser4_acl_entry);
	else
		return 0;
}

static int
save_acl(struct inode * inode, reiser4_plugin * plugin, char **area)
{
	struct posix_acl *acl;

	acl = inode_get_acl(inode);
	if (acl != NULL) {
		reiser4_acl_header *head;
		int i;

		head = (reiser4_acl_header *)*area;
		cputod16(acl->a_count, &head->count);
		*area += sizeof *head;
		for (i = 0; i < acl->a_count; ++i) {
			reiser4_acl_entry *ace;

			ace = (reiser4_acl_entry *)*area;
			cputod32(acl->a_entries[i].e_id, &ace->id);
			cputod16(acl->a_entries[i].e_tag, &ace->tag);
			cputod16(acl->a_entries[i].e_perm, &ace->perm);
			*area += sizeof *ace;
		}
	}
	return 0;
}

static int
change_acl(struct inode * inode, reiser4_plugin * plugin)
{
	int result;

	if (inode_perm_plugin(inode) == NULL ||
	    inode_perm_plugin(inode)->h.id != ACL_PERM_ID ||
	    inode_get_acl(inode) == NULL) {
		result = reiser4_set_acl(inode, ACL_TYPE_ACCESS, NULL);
		if (result == 0)
			plugin_set_perm(&reiser4_inode_data(inode)->pset,
					&plugin->perm);
		else if (result == -EOPNOTSUPP)
			result = 0;
	} else
		result = 0;
	return result;
}

void
clear_acl(struct inode *inode)
{
	inode_set_acl(inode, NULL);
}

reiser4_plugin_ops acl_plugin_ops = {
	.init     = init_acl,
	.load     = load_acl,
	.save_len = save_len_acl,
	.save     = save_acl,
	.change   = change_acl
};

static int
set_acl_plugin(struct inode *inode)
{
	int result;

	if (inode_perm_plugin(inode) == NULL ||
	    inode_perm_plugin(inode)->h.id != ACL_PERM_ID) {
		reiser4_inode *info;
		perm_plugin *acl_plug;

		info = reiser4_inode_data(inode);
		acl_plug = perm_plugin_by_id(ACL_PERM_ID);
		result = plugin_set_perm(&info->pset, acl_plug);
		if (result == 0) {
			result = xattr_add_namespace(inode, xattr_acl_handlers);
			if (result == 0)
				inode_set_plugin(inode,
						 perm_plugin_to_plugin(acl_plug));
		}
	} else
		result = 0;
	return result;
}

int
reiser4_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	int result;

	result = 0;
	if (!S_ISLNK(inode->i_mode)) {
		switch(type) {
		case ACL_TYPE_ACCESS:
			if (acl != NULL) {
				mode_t mode;

				mode = inode->i_mode;
				result = posix_acl_equiv_mode(acl, &mode);
				if (result >= 0) {
					inode->i_mode = mode;
					if (result == 0)
						acl = NULL;
				} else
					return result;
			}
			inode_set_acl(inode, acl);
			break;
		case ACL_TYPE_DEFAULT:
			if (!S_ISDIR(inode->i_mode))
				return acl ? RETERR(-EACCES) : 0;
			inode_set_default_acl(inode, acl);
			break;
		default:
			return RETERR(-EINVAL);
		}
		result = set_acl_plugin(inode);
	} else
		result = RETERR(-EOPNOTSUPP);
	return result;
}

/* below is an interface to the xattr API */

static int
xattr_get_acl(struct inode *inode, int type, void *buffer, size_t size)
{
	struct posix_acl *acl;
	int result;

	acl = get_acl(inode, type);
	if (!IS_ERR(acl)) {
		if (acl != NULL)
			result = posix_acl_to_xattr(acl, buffer, size);
		else
			result = RETERR(-ENODATA);
	} else
		result = RETERR(PTR_ERR(acl));
	return result;
}

static int
xattr_set_acl(struct inode *inode, int type, const void *value, size_t size)
{
	struct posix_acl *acl;
	int result;

	result = 0;
	if (current->fsuid == inode->i_uid || capable(CAP_FOWNER)) {
		if (value != NULL) {
			acl = posix_acl_from_xattr(value, size);
			if (IS_ERR(acl))
				return RETERR(PTR_ERR(acl));
			else if (acl != NULL) {
				result = posix_acl_valid(acl);
			}
		} else
			acl = NULL;
		if (result == 0) {
			result = reiser4_set_acl(inode, type, acl);
			if (result == 0) {
				file_plugin *fplug;
				__u64 tograb;

				fplug = inode_file_plugin(inode);
				tograb = fplug->estimate.update(inode);
				result = reiser4_grab_space(tograb,
							    BA_CAN_COMMIT);
				if (result == 0)
					result = reiser4_update_sd(inode);
			}
		}
	} else
		result = RETERR(-EPERM);
	return result;
}


static
size_t reiser4_xattr_list_acl_access(char *list, struct inode *inode,
				     const char *name, int name_len)
{
	const size_t size = sizeof(XATTR_NAME_ACL_ACCESS);

	if (list != NULL)
		memcpy(list, XATTR_NAME_ACL_ACCESS, size);
	return size;
}

static
int reiser4_xattr_get_acl_access(struct inode *inode, const char *name,
				 void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return xattr_get_acl(inode, ACL_TYPE_ACCESS, buffer, size);
	else
		return RETERR(-EINVAL);
}

static
int reiser4_xattr_set_acl_access(struct inode *inode, const char *name,
				 const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return xattr_set_acl(inode, ACL_TYPE_ACCESS, value, size);
	else
		return RETERR(-EINVAL);
}

static
size_t reiser4_xattr_list_acl_default(char *list, struct inode *inode,
				      const char *name, int name_len)
{
	const size_t size = sizeof(XATTR_NAME_ACL_DEFAULT);

	if (list != NULL)
		memcpy(list, XATTR_NAME_ACL_DEFAULT, size);
	return size;
}

static
int reiser4_xattr_get_acl_default(struct inode *inode, const char *name,
				 void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return xattr_get_acl(inode, ACL_TYPE_DEFAULT, buffer, size);
	else
		return RETERR(-EINVAL);
}

static
int reiser4_xattr_set_acl_default(struct inode *inode, const char *name,
				 const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return xattr_set_acl(inode, ACL_TYPE_DEFAULT, value, size);
	else
		return RETERR(-EINVAL);
}

static reiser4_xattr_plugin xattr_acl_handlers[] = {
	[0] = {
		.prefix	= XATTR_NAME_ACL_ACCESS,
		.list	= reiser4_xattr_list_acl_access,
		.get	= reiser4_xattr_get_acl_access,
		.set	= reiser4_xattr_set_acl_access
	},
	[1] = {
		.prefix	= XATTR_NAME_ACL_DEFAULT,
		.list	= reiser4_xattr_list_acl_default,
		.get	= reiser4_xattr_get_acl_default,
		.set	= reiser4_xattr_set_acl_default
	},
	[2] = {
		.prefix	= NULL,
		.list	= NULL,
		.get	= NULL,
		.set	= NULL
	}
};

static int eopnotsupp(void)
{
	return RETERR(-EOPNOTSUPP);
}

static reiser4_xattr_plugin xattr_acl_trigger_handlers[] = {
	[0] = {
		.prefix	= XATTR_NAME_ACL_ACCESS,
		.list	= (void *)eopnotsupp,
		.get	= (void *)eopnotsupp,
		.set	= reiser4_xattr_set_acl_access
	},
	[1] = {
		.prefix	= XATTR_NAME_ACL_DEFAULT,
		.list	= (void *)eopnotsupp,
		.get	= (void *)eopnotsupp,
		.set	= reiser4_xattr_set_acl_default
	},
	[2] = {
		.prefix	= NULL,
		.list	= NULL,
		.get	= NULL,
		.set	= NULL
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

