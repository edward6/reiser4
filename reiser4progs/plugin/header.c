/*
    header.c -- this just defines __plugin_start symbol at the 
    start of .plugins ELF-section. It is needed for monolithic building.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

unsigned long __plugin_start __attribute__((__section__(".plugins"))) = 0;

