/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants for lex component of parser.y
 */



#define ERR  -128


#if 1
typedef enum
  {
    OK   ,
    Blk  ,   /* blank */
    Wrd  ,   /* any symbol exept spec symbl */
    Int  ,   /* numeric */
    
    Ptr  ,   /* pointer */
    
    Pru  ,   /* _pruner */
    
    Stb  ,   /* ` string begin */
    Ste  ,   /* ' string end */
    Lpr  ,   /* ( [ { */ 
    Rpr  ,   /* ) ] } */
    Com  ,   /* , */
    Mns  ,   /* - */
    
       
    Les  ,   /* < */
    Slh  ,   /* / */
    
    Lsq  ,   /* [ ----------*/
    Rsq  ,   /* ] ----------*/
    
    Bsl  ,   /* \ */
    
    Lfl  ,   /* { ----------*/
    Rfl  ,   /* } ----------*/
    
    Pip  ,   /* | */
    Sp1  ,   /* : */
    Sp2  ,   /* ; */
    
    Dot  ,   /* . */
    
    Sp4  ,   /* = */
    Sp5  ,   /* > */
    Sp6  ,   /* ? */
    Pls  ,   /* +  ???*/
    
    /*LastTerm Sp6*/
    
    Res  ,   /*  */
    
    Str  ,
    ASG  ,
    App  ,
    Lnk  ,

    Ap2  

  } state;
#else
#define OK   0
#define Blk  1   /* blank */
#define Wrd  2   /* any symbol exept spec symbl */
#define Int  3   /* numeric */

#define Ptr  4   /* pointer */

#define Pru  5   /* _pruner */

#define Stb  6   /* ` string begin */
#define Ste  7   /* ' string end */
#define Lpr  8   /* ( [ { */ 
#define Rpr  9   /* ) ] } */
#define Com 10   /* , */
#define Mns 11   /* - */

#define Pls 11   /* +  ???*/

#define Les 12   /* < */
#define Slh 13   /* / */

#define Lsq 14   /* [ ----------*/
#define Rsq 15   /* ] ----------*/

#define Bsl 16   /* \ */

#define Lfl 18   /* { ----------*/
#define Rfl 19   /* } ----------*/

#define Pip 20   /* | */
#define Sp1 22   /* : */
#define Sp2 23   /* ; */

#define Dot 24   /* . */

#define Sp4 25   /* = */
#define Sp5 26   /* > */
#define Sp6 27   /* ? */

#define LastTerm Sp6

#define Res 28   /*  */


#define Str 32
#define ASG 33
#define App 34
#define Lnk 35

#define Ap2 36
#endif

#define STRING_CONSTANT_EMPTY STRING_CONSTANT   /* tmp */


static char   ncl     [256] =
{
	Blk,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	/* 32*/
      /*        !     "    #     $     %     &     ' */
	Blk,  Res,  Res,  Res,  Res,  Res,  Res,  Ste,
      /* (      )     *    +     ,     -     .     / */
        Lpr,  Rpr,  Res,  Pls,  Com,  Mns,  Dot,  Slh,
      /* 0      1     2    3     4     5     6     7 */
	Int,  Int,  Int,  Int,  Int,  Int,  Int,  Int,
      /* 8      9     :    ;     <     =     >     ? */
	Int,  Int,  Sp2,  Sp1,  Les,  Sp4,  Sp5,  Sp6,
        
	/* 64*/
      /* @      A     B    C     D     E     F     G */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* H      I     J    K     L     M     N     O */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* P      Q     R    S     T     U     V     W */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* X      Y     Z    [     \     ]     ^     _ */
	Wrd,  Wrd,  Wrd,  Lpr,  Bsl,  Rpr,  Res,  Pru,
	/* 96*/
      /* `      a     b    c     d     e     f     g */
        Stb,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* h      i     j    k     l     m     n     o */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* p      q     r    s     t     u     v     w */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* x      y     z    {     |     }     ~       */
	Wrd,  Wrd,  Wrd,  Lpr,  Pip,  Rpr,  Wrd,  ERR,
        
	/*128*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*160*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*192*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	/*224*/
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  ERR
};


struct lexcls
{
  int term;
  char c[32];
} ;




/*
EOF
EOL
ORDERED
INV_L ??
INV_R ??
IS
NAME ??
UNNAME ??
*/
