/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Scalable on-disk integers */

#include "debug.h"
#include "dscale.h"

static int gettag(const char *address)
{
	return (*address) >> 6;
}

static void cleartag(__u64 *value)
{
	*value <<= 2;
	*value >>= 2;
}

static int dscale_range(__u64 value)
{
	if (value > 0x3fffffff)
		return 3;
	if (value > 0x3fff)
		return 2;
	if (value > 0x3f)
		return 1;
	return 0;
}

int dscale_read(char *address, __u64 *value)
{
	int tag;
	int shift;

	tag = gettag(address);
	shift = 0;
	switch (tag) {
	case 0:
		*value = get_unaligned(address);
		break;
	case 1:
		*value = __be16_to_cpu(get_unaligned((__u16 *)address));
		break;
	case 2:
		*value = __be32_to_cpu(get_unaligned((__u32 *)address));
		break;
	case 3:
		*value = __be64_to_cpu(get_unaligned((__u64 *)(address + 1)));
		shift = 1;
		break;
	default:
		return RETERR(-EIO);
	}
	cleartag(value);
	return shift + (1 << tag);
}

int dscale_write(char *address, __u64 value)
{
	int tag;
	int shift;

	tag = dscale_range(value);
	shift = 0;
	value = __cpu_to_be64(value);
	switch(tag) {
	case 0:
		put_unaligned((__u8)value, address);
		break;
	case 1:
		put_unaligned((__u16)value, address);
		break;
	case 2:
		put_unaligned((__u32)value, address);
		break;
	case 3:
		put_unaligned((__u64)value, address + 1);
		shift = 1;
		break;
	}
	*address |= (tag << 6);
	return shift + (1 << tag);
}

int dscale_bytes(__u64 value)
{
	int bytes;

	bytes = 1 << dscale_range(value);
	if (bytes == 8)
		++ bytes;
	return bytes;
}

int dscale_fit(__u64 value, __u64 other)
{
	return dscale_range(value) == dscale_range(other);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
