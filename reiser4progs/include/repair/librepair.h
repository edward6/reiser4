/*
    repair/librepair.h -- the central recovery include file.
    Copyright (C) 1996 - 2002 Hans Reiser
    Author Vitaly Fertman
*/

#ifndef LIBREPAIR_H
#define LIBREPAIR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include <repair/repair.h>
#include <repair/filesystem.h>
#include <repair/format.h>

/*  -------------------------------------------------
    | Common scheem for communication with users.   |
    |-----------------------------------------------|
    |  stream  | default | with log | with 'no-log' |
    |----------|---------|----------|---------------|
    | warn     | stderr  | stderr   |  -            |
    | info     | stderr  | log      |  -            |
    | error    | stderr  | log      |  -            |
    | fatal    | stderr  | stderr   | stderr        |
    | bug      | stderr  | stderr   | stderr        |
    -------------------------------------------------
    info   - Information about what is going on. 
    warn   - warnings to users about what is going on, which should be viewed on-line.
    error  - Problems. 
    fatal  - Fatal problems which are supposed to be viewed on-line. 

    Modifiers: Auto (choose the default answer for all questions) and Verbose (provide some 
    extra information) and Quiet (quiet progress and provide only fatal and bug infotmation 
    to stderr; does not affect the log content though if log presents).
*/

#endif
