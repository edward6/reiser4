/*
    misc.c -- some common tools for all reiser4 utilities.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>

#include <reiser4progs/misc.h>

#define KB 1024
#define MB (KB * KB)
#define GB (KB * MB)

long int reiser4progs_misc_strtol(const char *str, int *error) {
    char *err;
    long result = 0;

    if (error)
	*error = 0;
	
    if (!str) {
	if (error) *error = 1; 
	return 0;
    }	
	
    result = strtol(str, &err, 10);
	
    if (errno == ERANGE || *err) {
	if (error) *error = 1;
	return 0;
    }	
	
    return result;
}

int reiser4progs_misc_choose_check(const char *chooses, int choose) {
    unsigned i;
	
    if (!chooses) return 0;
	
    for (i = 0; i < strlen(chooses); i++)
	if (chooses[i] == choose) return 1;
	
    return 0;
}

int reiser4progs_misc_choose_propose(const char *chooses, 
    const char *error, const char *format, ...) 
{
    va_list args;
    int choose, prompts = 0;
    char mess[4096], buf[255];
	
    if (!chooses || !format || !error)
	return 0;
	
    memset(mess, 0, 4096);
	
    va_start(args, format);
    vsprintf(mess, format, args);
    va_end(args);
	
    fprintf(stderr, mess);
    fflush(stderr);
	
    do {
	memset(buf, 0, 255);
		
	fgets(buf, 255, stdin);
	choose = buf[0];
			
	if (reiser4progs_misc_choose_check(chooses, choose)) 
	    break;
		
	if (prompts < 2) {
	    fprintf(stderr, error);
	    fflush(stderr);
	}	
    } while (prompts++ < 2);
	
    if (!reiser4progs_misc_choose_check(chooses, choose))
	choose = 0;
	
    return choose;
}

int reiser4progs_misc_dev_check(const char *dev) {
    struct stat st;
	
    if (!dev)
	return 0;
	
    if (stat(dev, &st) == -1)
	return 0;
	
    if (!S_ISBLK(st.st_mode)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, 
	    "Device %s isn't a block device.", dev);
    }
	
    return 1;
}

unsigned long long reiser4progs_misc_size_parse(const char *str, int *error) {
    unsigned long long size;
    char number[255], label = 0;
	 
    if (error)
	*error = 0;
	
    if (!str || strlen(str) == 0) {
	if (error) *error = 1;
	return 0;
    }	
	
    memset(number, 0, 255);
    strncpy(number, str, strlen(str));
    label = number[strlen(number) - 1];
	
    if (label == 'K' || label == 'M' || label == 'G')
	number[strlen(number) - 1] = '\0';
    else
	label = 0;	
	
    if ((size = reiser4progs_misc_strtol(number, error)) == 0 && *error)
	return 0;
	
    if (label == 0 || label == 'M')
	size = size * MB;
    else if (label == 'K')
	size = size * KB;
    else if (label == 'G')
	size = size * GB;

    return size;
}

int reiser4progs_misc_size_check(const char *str) {
    int error = 0;

    reiser4progs_misc_size_parse(str, &error);
    return !error;
}

