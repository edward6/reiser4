/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants for lex component of parser.y
 */

#define ERR  -128

#define OK   0

#define Blk  1			/* blank */
#define Wrd  2			/* any symbol exept spec symbl */
#define Int  3			/* numeric */
#define Ptr  4			/* pointer */
#define Pru  5			/* _pruner */

#define Stb  6			/* ` string begin */
#define Ste  7			/* ' string end */
#define Lpr  8			/* ( */
#define Rpr  9			/* ) */
#define Com 10			/* , */
#define Mns 11			/* - */

#define Pls 11			/* +  ??? */

#define Dot 12			/* . */
#define Slh 13			/* / */
#define Lsq 14			/* [ */
#define Rsq 15			/* ] */
#define Bsl 16			/* \ */
#define Lfl 18			/* { */
#define Rfl 19			/* } */
#define Pip 20			/* | */
#define Sp1 22			/* : */
#define Sp2 23			/* ; */

#define Les 24			/* < */

#define Sp4 25			/* = */
#define Sp5 26			/* > */
#define Sp6 27			/* ? */

#define LastTerm Sp6

#define Res 28			/*  */

#define Str 32
#define ASG 33
#define App 34
#define Lnk 35

static char ncl[256] = {
	ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
	ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
	ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
	ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
	/* 32 */
	/*        !     "    #     $     %     &     ' */
	Blk, Res, Res, Res, Res, Res, Res, Ste,
	/* (      )     *    +     ,     -     .     / */
	Lpr, Rpr, Res, Pls, Com, Mns, Dot, Slh,
	/* 0      1     2    3     4     5     6     7 */
	Int, Int, Int, Int, Int, Int, Int, Int,
	/* 8      9     :    ;     <     =     >     ? */
	Int, Int, Sp2, Sp1, Les, Sp4, Sp5, Sp6,

	/* 64 */
	/* @      A     B    C     D     E     F     G */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* H      I     J    K     L     M     N     O */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* P      Q     R    S     T     U     V     W */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* X      Y     Z    [     \     ]     ^     _ */
	Wrd, Wrd, Wrd, Lsq, Bsl, Rsq, Res, Pru,
	/* 96 */
	/* `      a     b    c     d     e     f     g */
	Stb, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* h      i     j    k     l     m     n     o */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* p      q     r    s     t     u     v     w */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/* x      y     z    {     |     }     ~       */
	Wrd, Wrd, Wrd, Lfl, Pip, Rfl, Wrd, ERR,

	/*128 */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/*160 */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/*192 */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	/*224 */
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd,
	Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, Wrd, ERR
};

struct {
	int term;
	char c[32]
} lexcls[64] = {
/*               a    1         _    `    '      (    )    ,    -    <    /    [    ]      \    {    }    |    ;    :    .    =      >    ?     +       */
/*          Blk  Wrd  Int  Ptr  Pru  Stb  Ste    Lpr  Rpr  Com  Mns  Les  Slh  Lsq  Rsq    Bsl  Lfl  Rfl  Pip  Sp1  Sp2  Dot  Sp4    Sp5  Sp6  Pls ...  */
	0,
/*Blk*/ {0, Blk, Wrd, Int, Ptr, Pru, Str, ERR, Lpr, Rpr, Com, Mns, Les,
		 Slh, Lsq, Rsq, Bsl, Lfl, Rfl, Pip, Sp1, Sp2, Dot, Sp4, Sp5,
	 Sp6, ERR, ERR, ERR, ERR, ERR, ERR},
	WORD,
/*Wrd*/ {0, OK, Wrd, Wrd, Wrd, Wrd, OK, OK, OK, OK, OK, Wrd, Wrd, Wrd,
		 OK, OK, Wrd, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK, OK, OK},

	N_WORD, *? ? ? ? ? */
/*Int*/ {0, OK, Wrd, Int, Wrd, Wrd, OK, OK, OK, OK, OK, Wrd, Wrd,
		     OK, OK, OK, Wrd, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK, OK, OK, OK, OK},

	P_WORD, *? ? ? ? ? ? */
/*Ptr*/ {0, OK, Wrd, Wrd, Wrd, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		     OK, OK, Wrd, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK, OK, OK, OK},
	P_RUNNER,
/*Pru*/ {0, OK, Pru, Pru, Pru, Pru, OK, OK, OK, OK, OK, Pru, Pru, Pru,
		 OK, OK, Pru, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK, OK, OK},

	STRING_CONSTANT_EMPTY,
/*Stb*/ {1, Str, Str, Str, Str, Str, Str, OK, Str, Str, Str, Str, Str,
		 Str, Str, Str, Str, Str, Str, Str, Str, Str, Str, Str, Str,
	 Str, Str, Str, Str, Str, Str, Str},
	0,
/*Ste*/ {0, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
		 ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
	 ERR, ERR, ERR, ERR, ERR, ERR, ERR},
	L_PARENT,
/*Lpr*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	R_PARENT,
/*Rpr*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	COMMA,
/*Com*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Mns*/ {0, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
		 ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, Lnk,
	 ERR, ERR, ERR, ERR, ERR, ERR, ERR},
	LT,
/*Les*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, ASG, App, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	SLASH,
/*Slh*/ {0, OK, Wrd, Wrd, Wrd, Wrd, OK, OK, SPL, OK, OK, Wrd, OK, Slh,
		 OK, OK, Wrd, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK, OK, OK},

	L_SKW_PARENT,
/*Lsq*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	R_SKW_PARENT,
/*Rsq*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	1,
/*Bsl*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	L_FLX_PARENT,
/*Lfl*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	R_FLX_PARENT,
/*Rfl*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Pip*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Sp1*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	SEMICOLON,
/*Sp2*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Dot*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Sp4*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Sp5*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	0,
/*Sp6*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	PLUS,
/*Pls*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	0,
/*Res*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	STRING_CONSTANT,
/*Str*/ {1, OK, Str, Str, Str, Str, Str, OK, Str, Str, Str, Str, Str,
		 Str, Str, Str, Str, Str, Str, Str, Str, Str, Str, Str, Str,
	 Str, Str, Str, Str, Str, Str, Str},
	L_ASSIGN,
	
	    /*ASG*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		     OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		     OK, OK},
	0,
/*App*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, Ap2, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},
	L_SYMLINK,
/*Lnk*/ {0, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
		 ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, OK, ERR,
	 ERR, ERR, ERR, ERR, ERR, ERR},

	L_APPEND,
/*Ap2*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		 OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
	 OK},

	SLASH_L_PARENT,
	
	    /*SPL*/ {0, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		     OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK, OK,
		     OK, OK},

};

BLANK
    EOF
    EOL
    PLUS
    SPACE ORDERED INV_L INV_R P_BYTES_WRITTEN P_BYTES_READ IS NAME NONAME UNNAME
