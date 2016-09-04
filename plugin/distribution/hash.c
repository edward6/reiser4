/*
 * Adopted for using by Reiser4 distribution plugin
 *
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 */

#include <asm/types.h>

inline u32 rotl32 ( u32 x, s8 r )
{
  return (x << r) | (x >> (32 - r));
}

inline u64 rotl64 ( u64 x, s8 r )
{
  return (x << r) | (x >> (64 - r));
}

#define ROTL32(x,y)     rotl32(x,y)
#define ROTL64(x,y)     rotl64(x,y)

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

static inline u32 fmix ( u32 h )
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

//-----------------------------------------------------------------------------

u32 murmur3_x86_32(const void * key, int len, u32 seed)
{
	const u8 * data = (const u8*)key;
	const int nblocks = len / 4;

	u32 h1 = seed;

	u32 c1 = 0xcc9e2d51;
	u32 c2 = 0x1b873593;

	/* body */

	const u8 * tail;
	u32 k1;
	const u32 * blocks = (const u32 *)(data + nblocks*4);
	int i;

	for(i = -nblocks; i; i++) {
		u32 k = blocks[i];

		k *= c1;
		k = ROTL32(k,15);
		k *= c2;

		h1 ^= k;
		h1 = ROTL32(h1,13);
		h1 = h1*5+0xe6546b64;
	}

	/* tail */

	tail = (const u8*)(data + nblocks*4);

	k1 = 0;

	switch(len & 3)	{
	case 3:
		k1 ^= tail[2] << 16;
	case 2:
		k1 ^= tail[1] << 8;
	case 1:
		k1 ^= tail[0];
		k1 *= c1;
		k1 = ROTL32(k1,15);
		k1 *= c2;
		h1 ^= k1;
	};

	/* finalization */

	h1 ^= len;

	h1 = fmix(h1);

	return h1;
}
