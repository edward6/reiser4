/*
    profile.c -- methods for working with profiles in reiser4 programs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <stdio.h>
#include <reiser4/reiser4.h>

static reiserfs_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
        .desc = "Profile for reiser4 with smart drop policy",
	.node		= NODE_REISER40_ID,
	.file = {
	    .reg	= FILE_REG40_ID, 
	    .dir	= FILE_DIR40_ID,
	    .symlink	= FILE_SYMLINK40_ID,
	    .special	= FILE_SPECIAL40_ID,
	},
	.item = {
	    .internal	= ITEM_INTERNAL40_ID,
	    .statdata	= ITEM_STATDATA40_ID,
	    .direntry	= ITEM_CDE40_ID,
	    .file_body = {
		.drop	= ITEM_DROP40_ID,
		.extent	= ITEM_EXTENT40_ID,
	    },
	    .acl	= ITEM_ACL40_ID,
	},       
	.hash		= HASH_R5_ID,
	.drop_policy	= DROP_SMART_ID,
	.perm		= PERM_RWX_ID,
	.format		= FORMAT_REISER40_ID,
	.oid		= OID_REISER40_ID,
	.alloc		= ALLOC_REISER40_ID,
	.journal	= JOURNAL_REISER40_ID,
	.key		= KEY_REISER40_ID,
	.sdext		= 1 << SDEXT_UNIX_ID
    },
    [1] = {
	.label = "extent40",
	.desc = "Profile for reiser4 with extents turned on",
	.node		= NODE_REISER40_ID,
	.file = {
	    .reg	= FILE_REG40_ID, 
	    .dir	= FILE_DIR40_ID,
	    .symlink	= FILE_SYMLINK40_ID,
	    .special	= FILE_SPECIAL40_ID,
	},
	.item = {
	    .internal	= ITEM_INTERNAL40_ID,
	    .statdata	= ITEM_STATDATA40_ID,
	    .direntry	= ITEM_CDE40_ID,
	    .file_body = {
		.drop	= ITEM_DROP40_ID,
		.extent	= ITEM_EXTENT40_ID,
	    },
	    .acl	= ITEM_ACL40_ID,
	},       
	.hash		= HASH_R5_ID,
	.drop_policy	= DROP_NEVER_ID,
	.perm		= PERM_RWX_ID,
	.format		= FORMAT_REISER40_ID,
	.oid		= OID_REISER40_ID,
	.alloc		= ALLOC_REISER40_ID,
	.journal	= JOURNAL_REISER40_ID,
	.key		= KEY_REISER40_ID,
	.sdext		= 1 << SDEXT_UNIX_ID
    },
    [2] = {
	.label = "drop40",
	.desc = "Profile for reiser4 with drops turned on",     
	.node		= NODE_REISER40_ID,
	.file = {
	    .reg	= FILE_REG40_ID, 
	    .dir	= FILE_DIR40_ID,
	    .symlink	= FILE_SYMLINK40_ID,
	    .special	= FILE_SPECIAL40_ID,
	},
	.item = {
	    .internal	= ITEM_INTERNAL40_ID,
	    .statdata	= ITEM_STATDATA40_ID,
	    .direntry	= ITEM_CDE40_ID,
	    .file_body = {
		.drop	= ITEM_DROP40_ID,
		.extent	= ITEM_EXTENT40_ID,
	    },
	    .acl	= ITEM_ACL40_ID,
	},       
	.hash		= HASH_R5_ID,
	.drop_policy	= DROP_ALWAYS_ID,
	.perm		= PERM_RWX_ID,
	.format		= FORMAT_REISER40_ID,
	.oid		= OID_REISER40_ID,
	.alloc		= ALLOC_REISER40_ID,
	.journal	= JOURNAL_REISER40_ID,
	.key		= KEY_REISER40_ID,
	.sdext		= 1 << SDEXT_UNIX_ID
    }
};

/* Finds profile by its name */
reiserfs_profile_t *progs_profile_find(
    const char *profile		    /* needed profile name */
) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++) {
	if (!aal_strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
	    return &reiser4profiles[i];
    }

    return NULL;
}

/* Shows all knows profiles */
void progs_profile_print_list(void) {
    unsigned i;
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiserfs_profile_t)); i++)
	printf("(%d) %s (%s).\n", i + 1, reiser4profiles[i].label, reiser4profiles[i].desc);
}

/* 0 profile is the default one. */
reiserfs_profile_t *progs_profile_default() {
    return &reiser4profiles[0];
}

enum plugin_internal_type {
    PROGS_NODE_PLUGIN,
    PROGS_FILE_PLUGIN,
    PROGS_DIR_PLUGIN,
    PROGS_SYMLINK_PLUGIN,
    PROGS_SPECIAL_PLUGIN,
    PROGS_INTERNAL_PLUGIN, 
    PROGS_STATDATA_PLUGIN, 
    PROGS_DIRENTRY_PLUGIN,
    PROGS_TAIL_PLUGIN,
    PROGS_EXTENT_PLUGIN,
    PROGS_ACL_PLUGIN,
    PROGS_HASH_PLUGIN,
    PROGS_TAIL_POLICY_PLUGIN,
    PROGS_PERM_PLUGIN,
    PROGS_FORMAT_PLUGIN,
    PROGS_OID_PLUGIN,
    PROGS_ALLOC_PLUGIN,
    PROGS_JOURNAL_PLUGIN,
    PROGS_KEY_PLUGIN,
    PROGS_LAST_PLUGIN
};

static char *progs_internal_plugin_name[] = {
    "NODE",
    "REGULAR_FILE",
    "DIR_FILE",
    "SYMLINK_FILE",
    "SPECIAL_FILE",
    "INTERNAL_ITEM",
    "STATDATA_ITEM",
    "DIRENTRY_ITEM",
    "TAIL_ITEM",
    "EXTENT_ITEM",
    "ACL_ITEM",
    "HASH",
    "TAIL_POLICY",
    "PERM",
    "FORMAT",
    "OID",
    "ALLOC",
    "JOURNAL",
    "KEY"
};

typedef enum plugin_internal_type plugin_internal_type_t;

static reiserfs_id_t *progs_profile_get_field(reiserfs_profile_t *profile, 
    plugin_internal_type_t internal_type) 
{
    if (!profile)
	return NULL;
	
    if (internal_type >= PROGS_LAST_PLUGIN) 
	return NULL;
    
    switch (internal_type) {
	case PROGS_NODE_PLUGIN:
	    return &profile->node;
	case PROGS_FILE_PLUGIN:
	    return &profile->file.reg;
	case PROGS_DIR_PLUGIN:
	    return &profile->file.dir;
	case PROGS_SYMLINK_PLUGIN:
	    return &profile->file.symlink;
	case PROGS_SPECIAL_PLUGIN:
	    return &profile->file.special;
	case PROGS_INTERNAL_PLUGIN:
	    return &profile->item.internal;
	case PROGS_STATDATA_PLUGIN:
	    return &profile->item.statdata;
	case PROGS_DIRENTRY_PLUGIN:
	    return &profile->item.direntry;
	case PROGS_TAIL_PLUGIN:
	    return &profile->item.file_body.drop;
	case PROGS_EXTENT_PLUGIN:
	    return &profile->item.file_body.extent;
	case PROGS_ACL_PLUGIN:
	    return &profile->item.acl;
	case PROGS_HASH_PLUGIN:
	    return &profile->hash;
	case PROGS_TAIL_POLICY_PLUGIN:
	    return &profile->drop_policy;
	case PROGS_PERM_PLUGIN:
	    return &profile->perm;
	case PROGS_FORMAT_PLUGIN:
	    return &profile->format;
	case PROGS_OID_PLUGIN:
	    return &profile->oid;
	case PROGS_ALLOC_PLUGIN:
	    return &profile->alloc;
	case PROGS_JOURNAL_PLUGIN:
	    return &profile->journal;
	case PROGS_KEY_PLUGIN:
	    return &profile->key;

	default: 
	    return NULL;	    
    }
    return NULL;
}

static reiserfs_plugin_type_t progs_profile_get_plugin_type(plugin_internal_type_t internal_type) {
    if (internal_type >= PROGS_LAST_PLUGIN) 
	return -1;
    
    switch (internal_type) {
	case PROGS_NODE_PLUGIN:
	    return NODE_PLUGIN_TYPE;
	case PROGS_FILE_PLUGIN:
	    return FILE_PLUGIN_TYPE;
	case PROGS_DIR_PLUGIN:
	    return FILE_PLUGIN_TYPE;
	case PROGS_SYMLINK_PLUGIN:
	    return FILE_PLUGIN_TYPE;
	case PROGS_SPECIAL_PLUGIN:
	    return FILE_PLUGIN_TYPE;
	case PROGS_INTERNAL_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_STATDATA_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_DIRENTRY_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_TAIL_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_EXTENT_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_ACL_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_HASH_PLUGIN:
	    return HASH_PLUGIN_TYPE;
	case PROGS_TAIL_POLICY_PLUGIN:
	    return DROP_POLICY_PLUGIN_TYPE;
	case PROGS_PERM_PLUGIN:
	    return PERM_PLUGIN_TYPE;
	case PROGS_FORMAT_PLUGIN:
	    return FORMAT_PLUGIN_TYPE;
	case PROGS_OID_PLUGIN:
	    return OID_PLUGIN_TYPE;
	case PROGS_ALLOC_PLUGIN:
	    return ALLOC_PLUGIN_TYPE;
	case PROGS_JOURNAL_PLUGIN:
	    return JOURNAL_PLUGIN_TYPE;
	case PROGS_KEY_PLUGIN:
	    return KEY_PLUGIN_TYPE;

	default:
	    return -1;
    }
    return -1;
}

static plugin_internal_type_t progs_profile_get_plugin_internal_type(
    const char *plugin_type_name) 
{
    if (!plugin_type_name)
	return -1;

    if (!aal_strncmp(plugin_type_name, "NODE", 4)) {
	return PROGS_NODE_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "FILE", 4)) {
	return PROGS_FILE_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "DIR", 3)) {
	return PROGS_DIR_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "SYMLINK", 7)) {
	return PROGS_SYMLINK_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "SPECIAL", 7)) {
	return PROGS_SPECIAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "INTERNAL", 8)) {	
	return PROGS_INTERNAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "STATDATA", 8)) {
	return PROGS_STATDATA_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "DIRENTRY", 8)) {
	return PROGS_DIRENTRY_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "TAIL", 4)) {
	return PROGS_TAIL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "EXTENT", 6)) {
	return PROGS_EXTENT_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "ACL", 3)) {
	return PROGS_ACL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "HASH", 4)) {
	return PROGS_HASH_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "TAIL_POLICY", 11)) {
	return PROGS_TAIL_POLICY_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "PERM", 4)) {
	return PROGS_PERM_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "FORMAT", 6)) {
	return PROGS_FORMAT_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "OID", 3)) {
	return PROGS_OID_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "ALLOC", 5)) {
	return PROGS_ALLOC_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "JOURNAL", 7)) {
	return PROGS_JOURNAL_PLUGIN;
    } else if (!aal_strncmp(plugin_type_name, "KEY", 3)) {
	return PROGS_KEY_PLUGIN;
    } else {
	return -1;
    }    
}

static char *progs_profile_get_plugin_internal_type_name(
    plugin_internal_type_t internal_type) 
{
    return (internal_type >= PROGS_LAST_PLUGIN) ? NULL : 
	progs_internal_plugin_name[internal_type];
}


int progs_profile_override_plugin_id_by_name(reiserfs_profile_t *profile, 
    const char *plugin_type_name, const char *plugin_label) 
{
    plugin_internal_type_t int_type;
    reiserfs_plugin_type_t type;
    reiserfs_id_t *id;
    reiserfs_plugin_t *plugin;

    if (!profile || !plugin_type_name || !plugin_label)
	return -1;
       	
    if ((int_type = progs_profile_get_plugin_internal_type(plugin_type_name)) == 
	(plugin_internal_type_t)-1)
	return -1;
    
    if ((type = progs_profile_get_plugin_type(int_type)) == (reiserfs_plugin_type_t)-1)
	return -1;

    if (!(plugin = libreiser4_factory_find_by_name(type, plugin_label))) 
	return -1;
    
    if ((id = progs_profile_get_field(profile, int_type)) == NULL)
	return -1;
    
    *id = plugin->h.id;
 
    return 0;
}

void progs_profile_print(reiserfs_profile_t *profile) {
    int i;
    reiserfs_plugin_t *plugin;

    if (!profile)
	return;
	
    printf("\nProfile %s: %s.\n", profile->label, profile->desc);
    for (i = 0; i < PROGS_LAST_PLUGIN; i++) {
	if ((plugin = libreiser4_factory_find_by_id(
	    progs_profile_get_plugin_type(i), *progs_profile_get_field(profile, i))) != NULL) 
	{
	    printf("%s plugin is %s: %s.\n", progs_profile_get_plugin_internal_type_name(i), 
		plugin->h.label, plugin->h.desc);
	} else {
	    printf("%s plugin is not found.\n", 
		progs_profile_get_plugin_internal_type_name(i));
	}
    }
    printf("\n");
}

