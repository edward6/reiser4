/*
    factory.h -- plugin factory header file.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FACTORY_H
#define FACTORY_H

#ifdef CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>

extern errno_t libreiser4_factory_init(void);
extern void libreiser4_factory_done(void);

extern errno_t libreiser4_factory_foreach(reiser4_plugin_func_t func, 
    void *data);

extern reiser4_plugin_t *libreiser4_factory_ifind(rid_t type, 
    rid_t id);

extern reiser4_plugin_t *libreiser4_factory_nfind(rid_t type, 
    const char *name);

extern reiser4_plugin_t *libreiser4_factory_cfind(reiser4_plugin_func_t func,
    void *data);

extern reiser4_plugin_t *libreiser4_plugin_fload(const char *name);
extern reiser4_plugin_t *libreiser4_plugin_eload(reiser4_plugin_entry_t entry);
extern void libreiser4_plugin_uload(reiser4_plugin_t *plugin);

#endif

