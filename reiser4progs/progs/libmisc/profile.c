/*
    profile.c -- methods for working with profiles in reiser4 programs.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <profile.h>

static reiser4_profile_t reiser4profiles[] = {
    [0] = {
	.label = "default40",
        .desc = "Profile for reiser4 with smart drop policy",
	.node		= NODE_REISER40_ID,
	.dir = {
	    .dir	= DIR_DIR40_ID,
	},
	.file = {
	    .reg	= FILE_REG40_ID, 
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
	.dir = {
	    .dir	= DIR_DIR40_ID,
	},
	.file = {
	    .reg	= FILE_REG40_ID, 
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
	.dir = {
	    .dir	= DIR_DIR40_ID,
	},
	.file = {
	    .reg	= FILE_REG40_ID, 
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
reiser4_profile_t *progs_profile_find(
    const char *profile		    /* needed profile name */
) {
    unsigned i;
    
    aal_assert("vpf-104", profile != NULL, return NULL);
    
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiser4_profile_t)); i++) {
	if (!aal_strncmp(reiser4profiles[i].label, profile, strlen(reiser4profiles[i].label)))
	    return &reiser4profiles[i];
    }

    return NULL;
}

/* Shows all knows profiles */
void progs_profile_list(void) {
    unsigned i;
    
    printf("\nKnown profiles are:\n");
    for (i = 0; i < (sizeof(reiser4profiles) / sizeof(reiser4_profile_t)); i++)
	printf("%s: %s.\n", reiser4profiles[i].label, reiser4profiles[i].desc);
    printf("\n");
}

/* 0 profile is the default one. */
reiser4_profile_t *progs_profile_default() {
    return &reiser4profiles[0];
}

enum progs_plugin_type {
    PROGS_NODE_PLUGIN,
    PROGS_FILE_PLUGIN,
    PROGS_DIR_PLUGIN,
    PROGS_SYMLINK_PLUGIN,
    PROGS_SPECIAL_PLUGIN,
    PROGS_INTERNAL_PLUGIN, 
    PROGS_STATDATA_PLUGIN, 
    PROGS_DIRENTRY_PLUGIN,
    PROGS_DROP_PLUGIN,
    PROGS_EXTENT_PLUGIN,
    PROGS_ACL_PLUGIN,
    PROGS_HASH_PLUGIN,
    PROGS_DROP_POLICY_PLUGIN,
    PROGS_PERM_PLUGIN,
    PROGS_FORMAT_PLUGIN,
    PROGS_OID_PLUGIN,
    PROGS_ALLOC_PLUGIN,
    PROGS_JOURNAL_PLUGIN,
    PROGS_KEY_PLUGIN,
    PROGS_LAST_PLUGIN
};

typedef enum progs_plugin_type progs_plugin_type_t;

static char *progs_plugin_name[] = {
    "NODE",
    "FILE",
    "DIR",
    "SYMLINK",
    "SPECIAL",
    "INTERNAL",
    "STATDATA",
    "DIRENTRY",
    "DROP",
    "EXTENT",
    "ACL",
    "HASH",
    "DROP_POLICY",
    "PERM",
    "FORMAT",
    "OID",
    "ALLOC",
    "JOURNAL",
    "KEY"
};

static reiser4_id_t *progs_profile_field(reiser4_profile_t *profile, 
    progs_plugin_type_t type) 
{
    aal_assert("umka-920", profile != NULL, return NULL);
    
    if (type >= PROGS_LAST_PLUGIN) 
	return NULL;
    
    switch (type) {
	case PROGS_NODE_PLUGIN:
	    return &profile->node;
	case PROGS_FILE_PLUGIN:
	    return &profile->file.reg;
	case PROGS_DIR_PLUGIN:
	    return &profile->dir.dir;
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
	case PROGS_DROP_PLUGIN:
	    return &profile->item.file_body.drop;
	case PROGS_EXTENT_PLUGIN:
	    return &profile->item.file_body.extent;
	case PROGS_ACL_PLUGIN:
	    return &profile->item.acl;
	case PROGS_HASH_PLUGIN:
	    return &profile->hash;
	case PROGS_DROP_POLICY_PLUGIN:
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

static reiser4_plugin_type_t progs_profile_it2pt(progs_plugin_type_t type) {

    if (type >= PROGS_LAST_PLUGIN) 
	return 0xffff;
    
    switch (type) {
	case PROGS_NODE_PLUGIN:
	    return NODE_PLUGIN_TYPE;
	case PROGS_DIR_PLUGIN:
	    return DIR_PLUGIN_TYPE;
	case PROGS_FILE_PLUGIN:
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
	case PROGS_DROP_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_EXTENT_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_ACL_PLUGIN:
	    return ITEM_PLUGIN_TYPE;
	case PROGS_HASH_PLUGIN:
	    return HASH_PLUGIN_TYPE;
	case PROGS_DROP_POLICY_PLUGIN:
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
	    return 0xffff;
    }
}

static progs_plugin_type_t progs_profile_name2it(const char *name) {
    aal_assert("umka-921", name != NULL, return 0xffff);

    if (!aal_strncmp(name, "NODE", 4))
	return PROGS_NODE_PLUGIN;
    else if (!aal_strncmp(name, "FILE", 4))
	return PROGS_FILE_PLUGIN;
    else if (!aal_strncmp(name, "DIR", 3))
	return PROGS_DIR_PLUGIN;
    else if (!aal_strncmp(name, "SYMLINK", 7))
	return PROGS_SYMLINK_PLUGIN;
    else if (!aal_strncmp(name, "SPECIAL", 7))
	return PROGS_SPECIAL_PLUGIN;
    else if (!aal_strncmp(name, "INTERNAL", 8))
	return PROGS_INTERNAL_PLUGIN;
    else if (!aal_strncmp(name, "STATDATA", 8))
	return PROGS_STATDATA_PLUGIN;
    else if (!aal_strncmp(name, "DIRENTRY", 8))
	return PROGS_DIRENTRY_PLUGIN;
    else if (!aal_strncmp(name, "TAIL", 4))
	return PROGS_DROP_PLUGIN;
    else if (!aal_strncmp(name, "EXTENT", 6))
	return PROGS_EXTENT_PLUGIN;
    else if (!aal_strncmp(name, "ACL", 3))
	return PROGS_ACL_PLUGIN;
    else if (!aal_strncmp(name, "HASH", 4))
	return PROGS_HASH_PLUGIN;
    else if (!aal_strncmp(name, "DROP_POLICY", 11))
	return PROGS_DROP_POLICY_PLUGIN;
    else if (!aal_strncmp(name, "PERM", 4))
	return PROGS_PERM_PLUGIN;
    else if (!aal_strncmp(name, "FORMAT", 6))
	return PROGS_FORMAT_PLUGIN;
    else if (!aal_strncmp(name, "OID", 3))
	return PROGS_OID_PLUGIN;
    else if (!aal_strncmp(name, "ALLOC", 5))
	return PROGS_ALLOC_PLUGIN;
    else if (!aal_strncmp(name, "JOURNAL", 7))
	return PROGS_JOURNAL_PLUGIN;
    else if (!aal_strncmp(name, "KEY", 3))
	return PROGS_KEY_PLUGIN;
    
    return 0xffff;
}

static char *progs_profile_it2name(progs_plugin_type_t type) {
    return (type >= PROGS_LAST_PLUGIN) ? NULL : progs_plugin_name[type];
}

errno_t progs_profile_override(reiser4_profile_t *profile, 
    const char *type, const char *name) 
{
    reiser4_id_t *field;
    progs_plugin_type_t it;
    reiser4_plugin_type_t pt;
    reiser4_plugin_t *plugin;

    aal_assert("umka-922", profile != NULL, return -1);
    aal_assert("umka-923", type != NULL, return -1);
    aal_assert("umka-924", name != NULL, return -1);
       	
    if ((it = progs_profile_name2it(type)) == 0xffff) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find plugin type \"%s\".", type);
	return -1;
    }
    
    if ((pt = progs_profile_it2pt(it)) == 0xffff)
	return -1;
    
    if (!(plugin = libreiser4_factory_find_by_name(pt, name))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find plugin by type \"%s\" and name \"%s\".", type, name);
	return -1;
    }
    
    if (!(field = progs_profile_field(profile, it))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get profile field.");
	return -1;
    }
    
    *field = plugin->h.id;
 
    return 0;
}

void progs_profile_print(reiser4_profile_t *profile) {
    int i;
    reiser4_plugin_t *plugin;

    aal_assert("umka-925", profile != NULL, return);
	
    printf("\nProfile %s:\n", profile->label);
    for (i = 0; i < PROGS_LAST_PLUGIN; i++) {
	if ((plugin = libreiser4_factory_find_by_id(progs_profile_it2pt(i), 
	    *progs_profile_field(profile, i))) != NULL) 
	{
	    printf("%s: %s (%s).\n", progs_profile_it2name(i),
		plugin->h.label, plugin->h.desc);
	}
    }
    printf("\n");
}

