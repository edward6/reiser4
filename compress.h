#if !defined( __FS_REISER4_COMPRESS_H__ )
#define REISER4_COMPRESS_H

#include <linux/types.h>
#include <linux/string.h>

typedef enum {
	TFM_READ,
	TFM_WRITE
} tfm_action;

/******************************************************************************/
/*                                                                            */
/*                                    PORT.H                                  */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/* This module contains macro definitions and types that are likely to        */
/* change between computers.                                                  */
/*                                                                            */
/******************************************************************************/

#ifndef DONE_PORT       /* Only do this if not previously done.               */

   #ifdef THINK_C
      #define UBYTE unsigned char      /* Unsigned byte                       */
      #define UWORD unsigned int       /* Unsigned word (2 bytes)             */
      #define ULONG unsigned long      /* Unsigned word (4 bytes)             */
      #define BOOL  unsigned char      /* Boolean                             */
      #define FOPEN_BINARY_READ  "rb"  /* Mode string for binary reading.     */
      #define FOPEN_BINARY_WRITE "wb"  /* Mode string for binary writing.     */
      #define FOPEN_TEXT_APPEND  "a"   /* Mode string for text appending.     */
      #define REAL double              /* USed for floating point stuff.      */
   #endif
   #if defined(LINUX) || defined(linux)
      #define UBYTE __u8               /* Unsigned byte                       */
      #define UWORD __u16              /* Unsigned word (2 bytes)             */
      #define ULONG __u32              /* Unsigned word (4 bytes)             */
      #define LONG  __s32              /* Signed   word (4 bytes)             */
      #define BOOL  is not used here   /* Boolean                             */
      #define FOPEN_BINARY_READ  not used  /* Mode string for binary reading. */
      #define FOPEN_BINARY_WRITE not used  /* Mode string for binary writing. */
      #define FOPEN_TEXT_APPEND  not used  /* Mode string for text appending. */
      #define REAL not used                /* USed for floating point stuff.  */
      #ifndef TRUE
      #define TRUE 1
      #endif
   #endif

   #define DONE_PORT                   /* Don't do all this again.            */
   #define MALLOC_FAIL NULL            /* Failure status from malloc()        */
   #define LOCAL static                /* For non-exported routines.          */
   #define EXPORT                      /* Signals exported function.          */
   #define then                        /* Useful for aligning ifs.            */

#endif

/******************************************************************************/
/*                              End of PORT.H                                 */
/******************************************************************************/

#define fast_copy(src,dst,len) xmemcpy(dst,src,len)

#endif /* __FS_REISER4_COMPRESS_H__ */

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
