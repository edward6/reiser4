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

long long int aal_fact(long long int n) {
    return n ? n * aal_fact(n - 1) : 1;
}

