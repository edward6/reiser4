/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants for lex component of parser.y
 */



#define ERR  -128


#if 1
typedef enum {
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
    Ap2  ,
    Nam  ,

    LastState
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
#define Nam 37
#define LastState 38
#endif

#define STRING_CONSTANT_EMPTY STRING_CONSTANT   /* tmp */


static char   ncl     [256] = {
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


struct lexcls {
  int term;
  char c[32];
} ;

static struct {
	char    *       wrd;
	int             class;
}
pars_key [] = {
  { "and"         ,    AND            },
  { "else"        ,    ELSE           },
  { "eq"          ,    EQ             },
  { "ge"          ,    GE             },
  { "gt"          ,    GT             },
  { "if"          ,    IF             },
  { "le"          ,    LE             },
  { "lt"          ,    LT             },
  { "ne"          ,    NE             },
  { "not"         ,    NOT            },
  { "or"          ,    OR             },
  { "then"        ,    THEN           },
  { "tw/"         ,    TRANSCRASH     }
};


struct lexcls lexcls[64] = {
/*
..   a   1       _   `   '     (   )   ,   -   <   /   [   ]     \   {   }   |   ;   :   .   =     >   ?   +
Blk Wrd Int Ptr Pru Stb Ste   Lpr Rpr Com Mns Les Slh Lsq Rsq   Bsl Lfl Rfl Pip Sp1 Sp2 Dot Sp4   Sp5 Sp6 Pls ...  */
[Blk]={ 0, {0,
Blk,Wrd,Int,Ptr,Pru,Str,ERR,  Lpr,Rpr,Com,Mns,Les,Slh,Lsq,Rsq,  Bsl,Lfl,Rfl,Pip,Sp1,Sp2,Dot,Sp4,  Sp5,Sp6,ERR,ERR,ERR,ERR,ERR,ERR}},
[Wrd]={  WORD, {0,
OK ,Wrd,Wrd,Wrd,Wrd,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  Bsl,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Int]={  WORD, {0,
OK ,Wrd,Int,Wrd,Wrd,OK ,OK ,  OK ,OK ,OK ,Wrd,OK ,OK ,OK ,OK ,  Wrd,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Ptr]={  WORD,{0,
OK ,Wrd,Wrd,Wrd,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  Wrd,OK ,OK ,OK ,OK ,OK ,Wrd,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pru]={  P_RUNNER,{0,
OK ,Pru,Pru,Pru,Pru,OK ,OK ,  OK ,OK ,OK ,Pru,OK ,OK ,OK ,OK ,  Pru,OK ,OK ,OK ,OK ,OK ,Pru,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
/*
[Stb]={  STRING_CONSTANT_EMPTY, {1,
Str,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
*/
[Stb]={  0, {1,
Stb,Stb,Stb,Stb,Stb,Stb,Str,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb,  Stb,Stb,Stb,Stb,Stb,Stb,Stb,Stb}},

[Ste]={  0, {0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},
[Lpr]={  L_BRACKET /*L_PARENT*/,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rpr]={  R_BRACKET,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Com]={  COMMA,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Mns]={  0,{0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  Lnk,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},
[Les]{  LT,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,ASG,App,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,Nam,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Slh]={  SLASH,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,Slh,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Lsq]={  0/*L_SKW_PARENT*/,{0,           /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rsq]={  0/*R_SKW_PARENT*/,{0,            /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Bsl]={  0,{0,
Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd}},
[Lfl]={  0 /*L_FLX_PARENT*/,{0,            /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Rfl]={  0 /*R_FLX_PARENT*/,{0,            /*mast removed*/
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pip]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp1]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp2]={  SEMICOLON,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Dot]={ WORD,{0,
OK ,Wrd,Wrd,Wrd,Wrd,Wrd,Wrd,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,Dot,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp4]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Sp5]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Sp6]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Pls]={  PLUS,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Res]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
/*
[Str]={  STRING_CONSTANT,{1,
Str,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
*/
[Str]={  STRING_CONSTANT,{1,
OK ,OK ,OK ,OK ,OK ,OK ,Stb,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},


[ASG]={  L_ASSIGN,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[App]={  L_ASSIGN,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,Ap2,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Lnk]={ L_SYMLINK,{0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  OK ,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},

[Ap2]={  L_APPEND,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

[Nam]={  NAMED,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }}

};


