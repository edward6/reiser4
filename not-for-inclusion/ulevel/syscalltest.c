#include <stdio.h>
#include <errno.h>
#include <asm/unistd.h>

static inline int reiser4(const char *command)
{
	long __res;
	__asm__ volatile ("int $0x80"
			  : "=a" (__res)
			  : "0" (268),"b" ((long)command)); \
	__syscall_return(int,__res); \
}

int main(int argc, char **argv)
{
	int result;

	result = reiser4(argv[1]);
	printf("%i\n", result);
}
