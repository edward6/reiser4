// -*- C++ -*-
// File: pars.cls.h
//
// Created: Thu Oct 25 15:20:32 2001
//
// $Id$
//



#define yymaxdepth __file_##maxdepth
#define yyparse __file_parse
#define yylex   __file_lex
#define yyerror __file_error
#define yylval  __file_lval
#define yychar  __file_char
#define yydebug __file_debug
#define yypact  __file_pact  
#define yyr1    __file_r1                    
#define yyr2    __file_r2                    
#define yydef   __file_def           
#define yychk   __file_chk           
#define yypgo   __file_pgo           
#define yyact   __file_act           
#define yyexca  __file_exca
#define yyerrflag __file_errflag
#define yynerrs __file_nerrs
#define yyps    __file_ps
#define yypv    __file_pv
#define yys     __file_s
#define yy_yys  __file_yys
#define yystate __file_state
#define yytmp   __file_tmp
#define yyv     __file_v
#define yy_yyv  __file_yyv
#define yyval   __file_val
#define yylloc  __file_lloc
#define yyreds  __file_reds
#define yytoks  __file_toks
#define yylhs   __file_yylhs
#define yylen   __file_yylen
#define yydefred __file_yydefred
#define yydgoto __file_yydgoto
#define yysindex __file_yysindex
#define yyrindex __file_yyrindex
#define yygindex __file_yygindex
#define yytable  __file_yytable
#define yycheck  __file_yycheck





#define EL -2

#define OK   -1
#define ERR   0
#define Idn  1
#define Int  2
#define fld  3
#define E_D  4
#define Edg  2
#define STR  2
#define str  2
#define DOT  2
#define DD   2
#define blk  2
#define SLH  2
#define BSL  22 

#define blk 10
#define DD   9

static
char   ncl     [256] =
{

	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	/* 32*/
  /*        !     "    #     $     %     &     ' */
	blk,    9,  STR,   15,    8,   15,   15,  str,
  /* (      )     *    +     ,     -     .     / */
     15,   15,   10,  PLS,   15,  PLS,  DOT,    8,
  /* 0      1     2    3     4     5     6     7 */
	Int,  Int,  Int,  Int,  Int,  Int,  Int,  Int,
  /* 8      9     :    ;     <     =     >     ? */
	Int,  Int,   15,   15,   11,   12,   13,   15,
        
	/* 64*/
  /* @      A     B    C     D     E     F     G */
	Ind,  Ind,  Ind,  Ind,  Ind,  Exp,  Ind,  Ind,
  /* H      I     J    K     L     M     N     O */
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
  /* P      Q     R    S     T     U     V     W */
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
  /* X      Y     Z    [     \     ]     ^     _ */
	Ind,  Ind,  Ind,    3,  BSL,   15,   14,  Ind,
	/* 96*/
  /* `      a     b    c     d     e     f     g */
    STR,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
  /* h      i     j    k     l     m     n     o */
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
  /* p      q     r    s     t     u     v     w */
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
  /* x      y     z    {     |     }     ~     */
	Ind,  Ind,  Ind,   15,   15,   15,   15,    0,
        
	/*128*/
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	/*160*/
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	/*192*/
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	/*224*/
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,
	Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind,  Ind
},


        lexcls[32][16]=
{


/*            a     1     E       +-     .   blank   "      \     &     *     <        =      >     ^  ...*/
/*           Ind   Int   Exp     PLS   DOT   blk   STR     BSL    &     *     <        =      >     ^  ...*/

/* ERR   */  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,
/* Idn   */  Idn,  Idn,  Ind,    OK ,  Ind,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/* Int   */  OK ,  Int,  E_d,    OK ,  fld,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/* Exp   */  Ind,  Ind,  Ind,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,

/* PLS   */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/* DOT   */  OK ,  fld,  OK ,    OK ,  DD ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/* blk   */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/* STR   */  STR,  STR,  STR,    STR,  STR,  STR,  STR,    Sbs,  STR,  STR,  STR,     STR,  STR,  STR,  STR,

/* BSL   */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,






/* str   */  OK ,  OK ,  OK ,    OK ,  OK ,  OK , STR,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,

    
/* fld   */  OK ,  fld,  E_d,    OK ,  ERR,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,
/*  DD   */  Idn,  Idn,  ERR,    ERR,  ERR,  ERR,  ERR,    17,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,

/* SLH   */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,

/* !     */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,      blk,  OK ,  OK ,  OK ,
/* *     */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  blk,  OK ,      OK ,  OK ,  OK ,  OK ,
/* <     */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  ERR,   18,      blk,   24,  ERR,  OK ,
/* =     */  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  ERR,  ERR,      blk,  ERR,  ERR,  OK ,


/* Sbs   */  OK ,  Sbo,  ERR,    ERR,  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,
/* So1   */  ERR,  Sbo,  ERR,    ERR,  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,
/* So2   */  ERR,  STR,  ERR,    ERR,  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,

/* E_d   */  ERR,  Edg,  ERR,    Edg,  ERR,  ERR,  ERR,    ERR,  ERR,  ERR,  ERR,     ERR,  ERR,  ERR,  ERR,
/* Edg   */  OK ,  Edg,  OK ,    OK ,  OK ,  OK ,  OK ,    OK ,  OK ,  OK ,  OK ,     OK ,  OK ,  OK ,  OK ,


};
