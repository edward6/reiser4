/*
    math.c -- some math functions needed by libaal.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

int aal_pow_of_two(unsigned long n) {
    return (n & -n) == n;
}

int aal_log2(unsigned long n) {
    unsigned long x;

    for (x = 0; (unsigned long)(1 << x) <= n; x++);
	return x - 1;
}

