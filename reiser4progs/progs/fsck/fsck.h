/*
    fsck.h -- fsck structure declarations.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.    
*/

#ifdef HAVE_CONFIG_H
#  include <config.h> 
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <version.h>
#include <misc.h>
#include <profile.h>
#include <repair/repair.h>
#include <repair/filesystem.h>
#include <reiser4/reiser4.h>


