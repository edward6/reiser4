/*
    tools.c -- tool functions which are needed by libreiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

int reiserfs_tools_log2(int n) {
    int x;
    for (x = 0; 1 << x <= n; x++);
    	return x - 1;
}

int reiserfs_tools_power_of_two(unsigned long n) {
    return (n & -n) == n;
}

