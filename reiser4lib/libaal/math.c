/*
	math.c -- some math functions needed by libaal.
	Copyright (C) 1996-2002 Hans Reiser.
*/

int aal_pow_of_two(unsigned long n) {
	return (n & -n) == n;
}

int aal_log2(int n) {
	int x;
	
	for (x = 0; 1 << x <= n; x++);
		return x - 1;
}

