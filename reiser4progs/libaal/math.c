/*
    math.c -- some math functions needed by libaal.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

/*
    Returns TRUE if passed value is power of two, FALSE otherwise. This function
    is used for checking block size for validness. 
*/
int aal_pow_of_two(unsigned long n) {
    return (n & -n) == n;
}

/* Retuns log2 of passed value */
int aal_log2(unsigned long n) {
    unsigned long x;

    for (x = 0; (unsigned long)(1 << x) <= n; x++);
	return x - 1;
}

/* Calculates factorial */
long long int aal_fact(long long int n) {
    return n ? n * aal_fact(n - 1) : 1;
}

/*
    Calculates the adler32 checksum for the data pointed by "buff" of the
    length "n". This function was originally taken from zlib, version 1.1.3,
    July 9th, 1998.

    Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Jean-loup Gailly        Mark Adler
    jloup@gzip.org          madler@alumni.caltech.edu

    The above comment is applyed to the only aal_alder32 function.
*/

#define ADLER_BASE (65521l)
#define ADLER_NMAX (5552)

unsigned int aal_adler32(char *buff, unsigned int n) {
    int k;
    unsigned char *t = buff;
    unsigned int s1 = 1, s2 = 0;

    while (n > 0) {
	k = n < ADLER_NMAX ? n : ADLER_NMAX;
    	n -= k;
	
	while (k--) {
	    s1 += *t++; 
	    s2 += s1;
	}
	
	s1 %= ADLER_BASE;
	s2 %= ADLER_BASE;
    }
    
    return (s2 << 16) | s1;
}

