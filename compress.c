/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* reiser4 compression transform plugins */

#include "debug.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"

#include <linux/config.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>
#include <linux/types.h>

LOCAL void __lzrw1_compress(UBYTE *, ULONG, UBYTE *, ULONG *);
LOCAL void __lzrw1_decompress(UBYTE *, ULONG, UBYTE *, ULONG *);

/******************************************************************************/
/*                         Start of LZRW1.C                                   */
/******************************************************************************/
/*
  THE LZRW1 ALGORITHM
  ===================
  Author : Ross N. Williams.
  Date   : 31-Mar-1991.

  1. I typed the following code in from my paper "An Extremely Fast Data
  Compression Algorithm", Data Compression Conference, Utah, 7-11 April,
  1991. The  fact that this  code works indicates  that the code  in the
  paper is OK.

  2. This file has been copied into a test harness and works.

  3. Some users running old C compilers may wish to insert blanks around
  the "="  symbols of  assignments so  as to  avoid expressions  such as
  "a=*b;" being interpreted as "a=a*b;"

  4. This code is public domain.

  5. Warning:  This code  is non-deterministic insofar  as it  may yield
  different  compressed representations  of the  same file  on different
  runs. (However, it will always decompress correctly to the original).

  6. If you use this code in anger (e.g. in a product) drop me a note at
  ross@spam.ua.oz.au and I will put you  on a mailing list which will be
  invoked if anyone finds a bug in this code.

  7.   The  internet   newsgroup  comp.compression   might  also   carry
  information on this algorithm from time to time.
*/
/******************************************************************************/
#define FLAG_BYTES    4     /* Number of bytes used by copy flag. */
#define FLAG_COMPRESS 0     /* Signals that compression occurred. */
#define FLAG_COPY     1     /* Signals that a copyover occurred.  */
/******************************************************************************/

LOCAL void __lzrw1_compress(p_src_first,src_len,p_dst_first,p_dst_len)
     /* Input  : Specify input block using p_src_first and src_len.          */
     /* Input  : Point p_dst_first to the start of the output zone (OZ).     */
     /* Input  : Point p_dst_len to a ULONG to receive the output length.    */
     /* Input  : Input block and output zone must not overlap.               */
     /* Output : Length of output block written to *p_dst_len.               */
     /* Output : Output block in Mem[p_dst_first..p_dst_first+*p_dst_len-1]. */
     /* Output : May write in OZ=Mem[p_dst_first..p_dst_first+src_len+256-1].*/
     /* Output : Upon completion guaranteed *p_dst_len<=src_len+FLAG_BYTES.  */
     UBYTE *p_src_first,*p_dst_first; ULONG src_len,*p_dst_len;
#define PS *p++!=*s++  /* Body of inner unrolled matching loop.         */
#define ITEMMAX 16     /* Maximum number of bytes in an expanded item.  */
{UBYTE *p_src=p_src_first,*p_dst=p_dst_first;
 UBYTE *p_src_post=p_src_first+src_len,*p_dst_post=p_dst_first+src_len;
 UBYTE *p_src_max1=p_src_post-ITEMMAX,*p_src_max16=p_src_post-16*ITEMMAX;
 UBYTE *hash[4096],*p_control; UWORD control=0,control_bits=0;
 *p_dst=FLAG_COMPRESS; p_dst+=FLAG_BYTES; p_control=p_dst; p_dst+=2;
 while (TRUE)
   {UBYTE *p,*s; UWORD unroll=16,len,index; ULONG offset;
   if (p_dst>p_dst_post) goto overrun;
   if (p_src>p_src_max16)
     {unroll=1;
     if (p_src>p_src_max1)
       {if (p_src==p_src_post) break; goto literal;}}
   begin_unrolled_loop:
   index=((40543*((((p_src[0]<<4)^p_src[1])<<4)^p_src[2]))>>4) & 0xFFF;
   p=hash[index]; hash[index]=s=p_src; offset=s-p;
   if (offset>4095 || p<p_src_first || offset==0 || PS || PS || PS)
     {literal: *p_dst++=*p_src++; control>>=1; control_bits++;}
   else
     {PS || PS || PS || PS || PS || PS || PS ||
	PS || PS || PS || PS || PS || PS || s++; len=s-p_src-1;
     *p_dst++=((offset&0xF00)>>4)+(len-1); *p_dst++=offset&0xFF;
     p_src+=len; control=(control>>1)|0x8000; control_bits++;}
#ifndef linux
   end_unrolled_loop: if (--unroll) goto begin_unrolled_loop;
#else 
   /* end_unrolled_loop: */ if (--unroll) goto begin_unrolled_loop;
#endif
   if (control_bits==16)
     {*p_control=control&0xFF; *(p_control+1)=control>>8;
     p_control=p_dst; p_dst+=2; control=control_bits=0;}
   }
 control>>=16-control_bits;
 *p_control++=control&0xFF; *p_control++=control>>8;
 if (p_control==p_dst) p_dst-=2;
 *p_dst_len=p_dst-p_dst_first;
 return;
 overrun: fast_copy(p_src_first,p_dst_first+FLAG_BYTES,src_len);
 *p_dst_first=FLAG_COPY; *p_dst_len=src_len+FLAG_BYTES;
}

/******************************************************************************/

LOCAL void __lzrw1_decompress(p_src_first,src_len,p_dst_first,p_dst_len)
     /* Input  : Specify input block using p_src_first and src_len.          */
     /* Input  : Point p_dst_first to the start of the output zone.          */
     /* Input  : Point p_dst_len to a ULONG to receive the output length.    */
     /* Input  : Input block and output zone must not overlap. User knows    */
     /* Input  : upperbound on output block length from earlier compression. */
     /* Input  : In any case, maximum expansion possible is eight times.     */
     /* Output : Length of output block written to *p_dst_len.               */
     /* Output : Output block in Mem[p_dst_first..p_dst_first+*p_dst_len-1]. */
     /* Output : Writes only  in Mem[p_dst_first..p_dst_first+*p_dst_len-1]. */
     UBYTE *p_src_first, *p_dst_first; ULONG src_len, *p_dst_len;
{UWORD controlbits=0, control;
 UBYTE *p_src=p_src_first+FLAG_BYTES, *p_dst=p_dst_first,
   *p_src_post=p_src_first+src_len;
 if (*p_src_first==FLAG_COPY)
   {fast_copy(p_src_first+FLAG_BYTES,p_dst_first,src_len-FLAG_BYTES);
   *p_dst_len=src_len-FLAG_BYTES; return;}
 while (p_src!=p_src_post)
   {if (controlbits==0)
     {control=*p_src++; control|=(*p_src++)<<8; controlbits=16;}
   if (control&1)
     {UWORD offset,len; UBYTE *p;
     offset=(*p_src&0xF0)<<4; len=1+(*p_src++&0xF);
     offset+=*p_src++&0xFF; p=p_dst-offset;
     while (len--) *p_dst++=*p++;}
   else
     *p_dst++=*p_src++;
   control>>=1; controlbits--;
   }
 *p_dst_len=p_dst-p_dst_first;
}

/******************************************************************************/
/*                          End of LZRW1.C                                    */
/******************************************************************************/

static void 
lzrw1_compress(void *ctx, __u8 *src_first, unsigned src_len, __u8 *dst_first, unsigned *dst_len)
{
	assert("edward-764", ctx == NULL);
	__lzrw1_compress(src_first, src_len, dst_first, dst_len);
	return;
}

static void 
lzrw1_decompress(void *ctx, __u8 *src_first, unsigned src_len, __u8 *dst_first, unsigned *dst_len)
{
	assert("edward-765", ctx == NULL);
	__lzrw1_decompress(src_first, src_len, dst_first, dst_len);
	return;
}

/******************************************************************************/
/*                                GZIP6.C                                     */
/******************************************************************************/
/*                                                                            */
/* See linux/zlib.h for details                                               */
/*                                                                            */
/******************************************************************************/

#define GZIP6_DEF_LEVEL		        Z_DEFAULT_COMPRESSION
#define GZIP6_DEF_WINBITS		11
#define GZIP6_DEF_MEMLEVEL		MAX_MEM_LEVEL

static int 
gzip6_alloc (void ** ctx, tfm_action act)
{
	int ret = -ENXIO;
	assert("edward-766", *ctx == NULL);
#if REISER4_GZIP_TFM 
	ret = 0;
	switch (act) {
	case TFM_WRITE: /* compress */
		*ctx = __vmalloc(zlib_deflate_workspacesize(),
				 (in_softirq() ? GFP_ATOMIC : GFP_KERNEL)|__GFP_HIGHMEM,
				 PAGE_KERNEL);
		if (*ctx == NULL) {
			ret = -ENOMEM;
			break;
		}
		xmemset(*ctx, 0, zlib_deflate_workspacesize());
		break;
	case TFM_READ: /* decompress */
		*ctx = reiser4_kmalloc(zlib_inflate_workspacesize(),
				       (in_softirq() ? GFP_ATOMIC : GFP_KERNEL));
		if (*ctx == NULL) {
			ret = -ENOMEM;
			break;
		}
		xmemset(*ctx, 0, zlib_inflate_workspacesize());
		break;
	default:
		impossible("edward-767", "alloc workspace for unknown tfm action");
	}
#endif
	if (ret)
		warning("edward-768", "alloc workspace for gzip6 (tfm action = %d) failed\n", act);
	return ret;
}

static void 
gzip6_free (void ** ctx, tfm_action act)
{
#if REISER4_GZIP_TFM
	assert("edward-769", *ctx != NULL);
	
	switch (act) {
	case TFM_WRITE: /* compress */
		vfree(*ctx);
		break;
	case TFM_READ:	
		reiser4_kfree(*ctx);
		break;
	default:
		impossible("edward-770", "free workspace for unknown tfm action");
	}
#endif
	return;
}

static void 
gzip6_compress( void *ctx, __u8 *src_first, unsigned src_len, __u8 *dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;
	compression_plugin * cplug = compression_plugin_by_id(GZIP6_COMPRESSION_ID);
	
	xmemset(&stream, 0, sizeof(stream));
	
	if (!ctx) {
		ret = cplug->alloc(&stream.workspace, TFM_WRITE);
		if (ret)
			goto rollback;
	}
	else 
		stream.workspace = ctx;	
	
	ret = zlib_deflateInit2(&stream, GZIP6_DEF_LEVEL, Z_DEFLATED,
	                        -GZIP6_DEF_WINBITS, GZIP6_DEF_MEMLEVEL,
	                        Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		warning("edward-771", "zlib_deflateInit2 returned %d\n", ret);
		goto rollback;
	}
	ret = zlib_deflateReset(&stream);
	if (ret != Z_OK) {
		warning("edward-772", "zlib_deflateReset returned %d\n", ret);
		goto rollback;
	}
	stream.next_in = src_first;
	stream.avail_in = src_len;
	stream.next_out = dst_first;
	stream.avail_out = *dst_len;
	
	ret = zlib_deflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		warning("edward-773", "zlib_deflate returned %d\n", ret);
		goto rollback;
	}
	*dst_len = stream.total_out;
	if (!ctx)
		cplug->free(&stream.workspace, TFM_WRITE);
	return;
 rollback:
	if (!ctx && stream.workspace)
		cplug->free(&stream.workspace, TFM_WRITE);	
	*dst_len = src_len;
#endif
	return;
}

static void 
gzip6_decompress(void * ctx, __u8 *src_first, unsigned src_len, __u8 *dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;
	compression_plugin * cplug = compression_plugin_by_id(GZIP6_COMPRESSION_ID);
	
	xmemset(&stream, 0, sizeof(stream));
	
	if (!ctx) {
		ret = cplug->alloc(&stream.workspace, TFM_READ);
		if (ret)
			goto out;
	}
	else 
		stream.workspace = ctx;	
	
	ret = zlib_inflateInit2(&stream, -GZIP6_DEF_WINBITS);
	if (ret != Z_OK) {
		warning("edward-774", "zlib_inflateInit2 returned %d\n", ret);
		goto out;
	}
	ret = zlib_inflateReset(&stream);
	if (ret != Z_OK) {
		warning("edward-775", "zlib_inflateReset returned %d\n", ret);
		goto out;
	}
	
	stream.next_in = src_first;
	stream.avail_in = src_len;
	stream.next_out = dst_first;
	stream.avail_out = *dst_len;
	
	ret = zlib_inflate(&stream, Z_SYNC_FLUSH);
	/*
	 * Work around a bug in zlib, which sometimes wants to taste an extra
	 * byte when being used in the (undocumented) raw deflate mode.
	 * (From USAGI).
	 */
	if (ret == Z_OK && !stream.avail_in && stream.avail_out) {
		u8 zerostuff = 0;
		stream.next_in = &zerostuff;
		stream.avail_in = 1; 
		ret = zlib_inflate(&stream, Z_FINISH);
	}
	if (ret != Z_STREAM_END) {
		warning("edward-776", "zlib_inflate returned %d\n", ret);
		goto out;
	}
	*dst_len = stream.total_out;
 out:
	if (!ctx && stream.workspace)
		cplug->free(&stream.workspace, TFM_READ);
#endif
	return;
}

compression_plugin compression_plugins[LAST_COMPRESSION_ID] = {
	[NONE_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = NONE_COMPRESSION_ID,
			.pops = NULL,
			.label = "none",
			.desc = "absence of any compression transform",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.overrun = 0,
		.alloc = NULL,
		.free = NULL,
	        .compress = NULL,
	        .decompress = NULL
	},
	[LZRW1_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = LZRW1_COMPRESSION_ID,
			.pops = NULL,
			.label = "lzrw1",
			.desc = "lzrw1 compression transform",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.overrun = 256,
		.alloc = NULL,
		.free = NULL,
	        .compress = lzrw1_compress,
	        .decompress = lzrw1_decompress
	},
	[GZIP6_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = GZIP6_COMPRESSION_ID,
			.pops = NULL,
			.label = "gzip6",
			.desc = "gzip6 compression transform",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.overrun = 0,
		.alloc = gzip6_alloc,
		.free = gzip6_free,
	        .compress = gzip6_compress,
	        .decompress = gzip6_decompress
	}
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 120
  scroll-step: 1
  End:
*/

