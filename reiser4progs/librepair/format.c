/*
    libprogs/format.c - methods are needed for handle the fs format.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <repair/librepair.h>

static reiser4_plugin_t *__choose_format(reiser4_fs_t *fs, aal_device_t *host_device) {
    reiser4_plugin_t *plugin;
   
    aal_assert("vpf-167", fs != NULL, return NULL);
    aal_assert("vpf-169", host_device != NULL, return NULL);
    aal_assert("vpf-168", repair_data(fs) != NULL, return NULL);
    aal_assert("vpf-168", repair_data(fs)->profile != NULL, return NULL);
    
    if (!(plugin = reiser4_master_guess(host_device))) {
	/* Format was not detected on the partition. */
	aal_exception_fatal("Cannot detect an on-disk format on (%s).", 
	    aal_device_name(host_device));
	
	if (!(plugin = libreiser4_factory_find_by_id(FORMAT_PLUGIN_TYPE, 
	    repair_data(fs)->profile->format))) 
	{
	    aal_exception_fatal("Cannot find the format plugin (%d) specified in the "
		"profile.", repair_data(fs)->profile->format);
	    return NULL;	    
	}

	if (aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_YES|EXCEPTION_NO, 
	    "Do you want to build the on-disk format (%s) specified in the profile?",
	    plugin->h.label) == EXCEPTION_NO)
	    return NULL;
    } else {
	/* Format was detected on the partition. */
	if (repair_data(fs)->profile->format != plugin->h.id)
	    aal_exception_fatal("The detected on-disk format (%s) differs from the "
		"profile's one.\nDo not forget to specify the correct on-disk format "
		"in the profile next time.", plugin->h.label);
	else if (repair_verbose(repair_data(fs))) {
	    aal_exception_info("The on-disk format (%s) was detected on (%s).", plugin->h.label, 
		aal_device_name(host_device));
	}
    }
    
    return plugin;
}

errno_t repair_format_check(reiser4_fs_t *fs) {
    reiser4_plugin_t *plugin = NULL;

    aal_assert("vpf-165", fs != NULL, return -1);
    aal_assert("vpf-166", repair_data(fs) != NULL, return -1);
    aal_assert("vpf-171", repair_data(fs)->host_device != NULL, return -1);
    
    if (!fs->format) {
	/* Format was not opened. */
	aal_exception_fatal("Cannot open the on-disk format on (%s)", 
	    aal_device_name(repair_data(fs)->host_device));
	
	if (!(plugin = __choose_format(fs, repair_data(fs)->host_device)))
	    return -1;

	/* Create the format with invalid parameters and fix them at the check. */
	if (!(fs->format = reiser4_format_create(repair_data(fs)->host_device, 0, 
	    INVALID_PLUGIN_ID, plugin->h.id))) 
	{
	    aal_exception_fatal("Cannot create a filesystem of the format (%s).", 
		plugin->h.label);
	    return -1;
	}
    } 
    
    /* Format was either opened or created. Check it and fix it. */
    if (libreiser4_plugin_call(return -1, fs->format->entity->plugin->format_ops, check, 
	fs->format->entity, repair_data(fs)->options)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Failed to recover the on-disk format (%s) on (%s).", plugin->h.label, 
	    aal_device_name(repair_data(fs)->host_device));
	return -1;
    }
    
    return 0;
}

void repair_format_print(reiser4_fs_t *fs, FILE *stream, uint16_t options) {
    char buf[4096];

    aal_assert("vpf-165", fs != NULL, return);
    aal_assert("vpf-175", fs->format != NULL, return);

    if (!stream)
	return;

    aal_memset(buf, 0, 4096);

    libreiser4_plugin_call(return, fs->format->entity->plugin->format_ops, print, 
	fs->format->entity, buf, 4096, options);
    
    fprintf(stream, "%s", buf);
}
