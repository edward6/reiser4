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

#define ADLER_BASE	    (65521l)
#define ADLER_NMAX	    (5552)

#define ADLER_DO1(buff, s1, s2) \
    { s1 += *buff++; s2 += s1; }
    
#define ADLER_DO2(buff, s1, s2) \
    ADLER_DO1(buff, s1, s2); ADLER_DO1(buff, s1, s2);

#define ADLER_DO4(buff, s1, s2) \
    ADLER_DO2(buff, s1, s2); ADLER_DO2(buff, s1, s2);

#define ADLER_DO8(buff, s1, s2) \
    ADLER_DO4(buff, s1, s2); ADLER_DO4(buff, s1, s2);

#define ADLER_DO16(buff, s1, s2) \
    ADLER_DO8(buff, s1, s2); ADLER_DO8(buff, s1, s2);

unsigned int aal_adler32(char *buff, unsigned int n) {
    int k;
    unsigned char *t = buff;
    unsigned int s1 = 1, s2 = 0;

    while (n > 0) {
	k = n < ADLER_NMAX ? n : ADLER_NMAX;
    	n -= k;
	
	while (k >= 16) {
	    ADLER_DO16(t, s1, s2);
	    k -= 16;
	}
	
	while (k--)
	    ADLER_DO1(t, s1, s2);
	
	s1 %= ADLER_BASE;
	s2 %= ADLER_BASE;
    }
    
    return (s2 << 16) | s1;
}

