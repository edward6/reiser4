/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Scalable on-disk integers */

/*
 * Various on-disk structures contain integer-like structures. Stat-data
 * contain [yes, "data" is plural, check the dictionary] file size, link
 * count; extent unit contains extent width etc. To accommodate for general
 * case enough space is reserved to keep largest possible value. 64 bits in
 * all cases above. But in overwhelming majority of cases numbers actually
 * stored in these fields will be comparatively small and reserving 8 bytes is
 * a waste of precious disk bandwidth.
 *
 * Scalable integers are one way to solve this problem. dscale_write()
 * function stores __u64 value in the given area consuming from 1 to 9 bytes,
 * depending on the magnitude of the value supplied. dscale_read() reads value
 * previously stored by dscale_write().
 *
 * dscale_write() produces format not completely unlike of UTF: two highest
 * bits of the first byte are used to store "tag". One of 4 possible tag
 * values is chosen depending on the number being encoded:
 *
 *           0 ... 0x3f               => 0
 *        0x40 ... 0x3fff             => 1
 *      0x4000 ... 0x3fffffff         => 2
 *  0x40000000 ... 0xffffffffffffffff => 3
 *
 * (see dscale_range() function)
 *
 * As _highest_ bits are used for the test (which is natural) scaled integers
 * are stored in BIG-ENDIAN format in contrast with the rest of reiser4 which
 * uses LITTLE-ENDIAN.
 *
 */

#include "debug.h"
#include "dscale.h"

/* return tag of scaled integer stored at @address */
static int gettag(const unsigned char *address)
{
	return (*address) >> 6;
}

/* clear tag from value */
static void cleartag(__u64 *value, int tag)
{
	/* W-w-what ?! */
	*value &= ~(3 << (((1 << tag) << 3) - 2));
}

/* return tag for @value */
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

/* restore value stored at @adderss by dscale_write() and return number of
 * bytes consumed */
int dscale_read(unsigned char *address, __u64 *value)
{
	int tag;

	tag = gettag(address);
	switch (tag) {
	case 3:
		*value = __be64_to_cpu(get_unaligned((__u64 *)(address + 1)));
		/* worst case: 8 bytes for value itself plus one byte for
		 * tag */
		return 9;
	case 0:
		*value = get_unaligned(address);
		break;
	case 1:
		*value = __be16_to_cpu(get_unaligned((__u16 *)address));
		break;
	case 2:
		*value = __be32_to_cpu(get_unaligned((__u32 *)address));
		break;
	default:
		return RETERR(-EIO);
	}
	cleartag(value, tag);
	return 1 << tag;
}

/* store @value at @address and return number of bytes consumed */
int dscale_write(unsigned char *address, __u64 value)
{
	int tag;
	int shift;
	unsigned char *valarr;

	tag = dscale_range(value);
	value = __cpu_to_be64(value);
	valarr = (unsigned char *)&value;
	shift = (tag == 3) ? 1 : 0;
	memcpy(address + shift, valarr + sizeof value - (1 << tag), 1 << tag);
	*address |= (tag << 6);
	return shift + (1 << tag);
}

/* number of bytes required to store @value */
int dscale_bytes(__u64 value)
{
	int bytes;

	bytes = 1 << dscale_range(value);
	if (bytes == 8)
		++ bytes;
	return bytes;
}

/* returns true if @value and @other require the same number of bytes to be
 * stored. Used by detect when data structure (like stat-data) has to be
 * expanded or contracted. */
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
