/*
    footer.c -- this just defines __plugin_end symbol at the 
    end of .plugins ELF-section. It is needed for monolithic building.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

unsigned long __plugin_end __attribute__((__section__(".plugins"))) = 0;

