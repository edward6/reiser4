// -*- C++ -*-
// File: pars.cls.h
//
// Created: Thu Oct 25 15:20:32 2001
//
// $Id$
//





#define ERR  -128

#define OK   0

#define Blk  1   /* blank */
#define Wrd  2   /* any symbol exept spec symbl */
#define Int  3   /* numeric */
#define Ptr  4   /* pointer */
#define Pru  5   /* _pruner */

#define Stb  6   /* ` string begin */
#define Ste  7   /* ' string end */
#define Lpr  8   /* ( */ 
#define Rpr  9   /* ) */
#define Com 10   /* , */
#define Mns 11   /* - */
#define Dot 12   /* . */
#define Slh 13   /* / */
#define Lsq 14   /* [ */
#define Rsq 15   /* ] */
#define Bsl 16   /* \ */
#define Lfl 18   /* { */
#define Rfl 19   /* } */
#define Pip 20   /* | */
#define Sp1 22   /* : */
#define Sp2 23   /* ; */
#define Sp3 24   /* < */

#define Sp4 25   /* = */
#define Sp5 26   /* > */
#define Sp6 27   /* ? */

#define LastTerm Sp6

#define Res 28   /*  */


#define STr 32
#define ASG 33
#define App 34
#define Lnk 35





static char   ncl     [256] =
{
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,  ERR,
	/* 32*/
      /*        !     "    #     $     %     &     ' */
	Blk,  Res,  Res,  Res,  Res,  Res,  Res,  Ste,
      /* (      )     *    +     ,     -     .     / */
        Lpr,  Rpr,  Res,  Pls,  Com,  Mns,  Sp3,  Slh,
      /* 0      1     2    3     4     5     6     7 */
	Int,  Int,  Int,  Int,  Int,  Int,  Int,  Int,
      /* 8      9     :    ;     <     =     >     ? */
	Int,  Int,  Sp2,  Sp1,  Les,  Sp4,  Sp5,  Sp6,
        
	/* 64*/
      /* @      A     B    C     D     E     F     G */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Exp,  Wrd,  Wrd,
      /* H      I     J    K     L     M     N     O */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* P      Q     R    S     T     U     V     W */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* X      Y     Z    [     \     ]     ^     _ */
	Wrd,  Wrd,  Wrd,  Lsq,  Bsl Rsq,  Res,  Pru,
	/* 96*/
      /* `      a     b    c     d     e     f     g */
        Stb,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* h      i     j    k     l     m     n     o */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* p      q     r    s     t     u     v     w */
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,
      /* x      y     z    {     |     }     ~     */
	Wrd,  Wrd,  Wrd,  Lfl,  Pip,  Rfl,  St0,    0,
        
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
	Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd,  Wrd
};


static
char        lexcls[32][32]=
{
/*               a    1         _    `    '      (    )    ,    -    <    /    [    ]      \    {    }    |    ;    :    .    =      >    ?            */
/*          Blk  Wrd  Int  Ptr  Pru  Stb  Ste    Lpr  Rpr  Com  Mns  Les  Slh  Lsq  Rsq    Bsl  Lfl  Rfl  Pip  Sp1  Sp2  Sp3  Sp4    Sp5  Sp6  Res ...  */

/*Blk*/{0,  Blk, Wrd, Int, Ptr, Pru, STr, ERR,   Lpr, Rpr, Com, Mns, OK , Slh, Lsq, Rsq,   Bsl, Lfl, Rfl, Pip, Sp1, Sp2, Sp3, Sp4,   Sp5, Sp6, OK , OK , OK , OK , OK , OK },
/*Wrd*/{0,  OK , Wrd, Wrd, Wrd, Wrd, OK , OK ,   OK , OK , OK , Wrd, Wrd, Wrd, OK , OK ,   Wrd, OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Int*/{0,  OK , Wrd, Int, Wrd, Wrd, OK , OK ,   OK , OK , OK , Wrd, Wrd, OK , OK , OK ,   Wrd, OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Ptr*/{0,  OK , Wrd, Wrd, Wrd, OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   Wrd, OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Pru*/{0,  OK , Pru, Pru, Pru, Pru, OK , OK ,   OK , OK , OK , Pru, Pru, Pru, OK , OK ,   Pru, OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Stb*/{0,  OK , STr, STr, STr, STr, STr, OK ,   STr, STr, STr, STr, STr, STr, STr, STr,   STr, STr, STr, STr, STr, STr, STr, STr,   STr, STr, STr, STr, STr, STr, STr, STr},
/*Ste*/{0,  ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR},
/*Lpr*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Rpr*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Com*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Mns*/{0,  ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,   ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR},
/*Les*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , ASG, App, OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Slh*/{0,  OK , Wrd, Wrd, Wrd, Wrd, OK , OK ,   OK , OK , OK , Wrd, OK , Wrd, OK , OK ,   Wrd, OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Lsq*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Rsq*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Bsl*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Lfl*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Rfl*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Pip*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Sp1*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Sp2*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Sp3*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Sp4*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Sp5*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*Sp6*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Res*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },

/*STr*/{0,  OK , STr, STr, STr, STr, STr, OK ,   STr, STr, STr, STr, STr, STr, STr, STr,   STr, STr, STr, STr, STr, STr, STr, STr,   STr, STr, STr, STr, STr, STr, STr, STr},
/*ASG*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   Lnk, OK , OK , OK , OK , OK , OK , OK },
/*App*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK },
/*Lnk*/{0,  OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK ,   OK , OK , OK , OK , OK , OK , OK , OK }
};
