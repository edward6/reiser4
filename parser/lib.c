/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * functions for parser.y
 */


#include "lib.h"


/* FIXME:NIKITA->VOVA this file uses indentation completely different than the
 * rest of reiser4 and kernel. This complicates reading of the code by other
 * people. I think this should be changed.
 * OK. But after it's works*/

static struct
{
	char    *       wrd;
	int             class;
}
pars_key [] =
	{
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


struct lexcls lexcls[64]={
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

[Stb]={  STRING_CONSTANT_EMPTY, {1,
Str,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
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
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,ASG,App,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},

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
[Dot]={  0,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
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

[Str]={  STRING_CONSTANT,{1,
OK ,Str,Str,Str,Str,Str,OK ,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str,  Str,Str,Str,Str,Str,Str,Str,Str}},
[ASG]={  L_ASSIGN,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[App]={  L_ASSIGN,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,Ap2,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Lnk]={ L_SYMLINK,{0,
ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  ERR,ERR,ERR,ERR,ERR,ERR,ERR,ERR,  OK ,ERR,ERR,ERR,ERR,ERR,ERR,ERR}},

[Ap2]={  L_APPEND,{0,
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }}

};




#define LEX_XFORM  1001
#define LEXERR2    1002
#define LEX_Ste    1003

/* printing errors for parsing */
static void yyerror( struct reiser4_syscall_w_space *ws  /* work space ptr */,
		                             int msgnum  /* message number */, ...)
{
	char errstr[120]={"\nreiser4 parser:"};
	va_list args;
	va_start(args, msgnum);
	switch (msgnum)
		{
		case   101:
			strcat(errstr,"yacc stack overflow");
			break;
		case LEX_XFORM:
			strcat(errstr,"x format has odd number of symbols");
			break;
		case LEXERR2:
/*			int state = va_arg(args, int);*/
			strcat(errstr,"internal lex table error");
			break;
		case LEX_Ste:
			strcat(errstr,"wrong lexem");
			break;
		case 11111:
			{
				int state = va_arg(args, int);
/*				int s = va_arg(args, int);*/
				strcat(errstr," syntax error:");
				switch(state)
					{
					case 4:
						strcat(errstr,"wrong operation");
						break;
					case 6:
						strcat(errstr,"wrong assign operation");
						break;
					case 7:
					case 14:
						strcat(errstr,"wrong name");
						break;
					case 26:
						strcat(errstr,"wrong logical operation");
						break;
					case 9:
						strcat(errstr,"wrong THEN keyword");
						break;
					case 36:
					case 49:
						strcat(errstr,"wrong separatop");
						break;
					default:
						strcat(errstr,"syntax error");
						break;
					}
			}
			break;
		}
	va_end(args);
	printk(errstr);
	//	printk("\n%s",ws_pline);
	printk("\n%s",curr_symbol(ws));
}

static int yywrap()
{
    return 1;
}

/* free lists of work space*/
static void freeList(freeSpace_t * list /* head of list to be fee */)
{
	freeSpace_t * curr,* next;
	next = list;
	while (next)
		{
			curr = next;
			next = curr->freeSpace_next;
			kfree(curr);
		}
}

/* free work space*/
static int reiser4_pars_free(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	if (ws->freeSpHead)
		{
			freeList(ws->freeSpHead);
		}
	kfree(ws);
	return 0;
}

/* FIXME:NIKITA->VOVA code below looks like custom made memory allocator. Why
 * not to use slab? */
#define initNextFreeSpace(fs)	(fs)->freeSpace_next = NULL;                                      \
                                (fs)->freeSpaceMax   = (fs)->freeSpaceBase+FREESPACESIZE;         \
			        (fs)->freeSpace      = (fs)->freeSpaceBase


/* allocate work space */
static freeSpace_t * freeSpaceAlloc()
{
	freeSpace_t * fs;
	fs = ( freeSpace_t * ) kmalloc( sizeof( freeSpace_t ),GFP_KERNEL ) ;
	assert("VD kmalloc work space",fs!=NULL);
	memset( fs , 0, sizeof( freeSpace_t ));
	initNextFreeSpace(fs);



	return fs;
}

#define get_first_freeSpHead(ws) (ws)->freeSpHead
#define get_next_freeSpHead(curr) (curr)->freeSpace_next


/* allocate next work space */
static freeSpace_t * freeSpaceNextAlloc(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	freeSpace_t * curr,* next;
	curr=NULL;
	next = get_first_freeSpHead(ws);
	while (next)
		{
			curr = next;
			next = get_next_freeSpHead(curr);
		}
	next = freeSpaceAlloc();
	if(curr==NULL)
		{
			ws->freeSpHead=next;
		}
	else
		{
			curr->freeSpace_next=next;
		}
	next->freeSpace_next=NULL;
	return next;
}

/* allocate field lenth=len in work space */
static char* list_alloc(struct reiser4_syscall_w_space * ws/* work space ptr */,
			int len/* lenth of structures to be allocated in bytes */)
{
	char * rez;
	if( (ws->freeSpCur->freeSpace+len) > (ws->freeSpCur->freeSpaceMax) )
		{
			ws->freeSpCur = freeSpaceNextAlloc(ws);
		}
	rez = ws->freeSpCur->freeSpace;
	ws->freeSpCur->freeSpace += ROUND_UP(len);
	return rez;
}

/* allocate new level of parsing in work space */
static streg_t *alloc_new_level(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	return ( streg_t *)  list_alloc(ws,sizeof(streg_t));
}

/* allocate structure of new variable of input expression */
static pars_var_t * alloc_pars_var(struct reiser4_syscall_w_space * ws /* work space ptr */,
			     pars_var_t * last_pars_var /* last of allocated pars_var or NULL if list is empty */)
{
	pars_var_t * pars_var;
	PTRACE(ws, "begin ws->Head_pars_var =%p last_pars_var=%p",ws->Head_pars_var, last_pars_var);
	pars_var = (pars_var_t *)list_alloc(ws,sizeof(pars_var_t));
	if ( last_pars_var == NULL )
		{
			ws->Head_pars_var = pars_var;
		}
	else
		{
			last_pars_var->next = pars_var;
		}
	pars_var->next = NULL;
	PTRACE(ws, "return pars_var =%p ",pars_var);
	return pars_var;
}

/* free lnodes used in expression */
static int free_expr( /*struct reiser4_syscall_w_space * ws, */ expr_v4_t * expr)
{
	expr_list_t * tmp;
	int ret = 0;
	assert("VD-free_expr", expr!=NULL);
	switch (expr->h.type)
		{

		case EXPR_WRD:
			break;
		case EXPR_PARS_VAR:
			assert("VD-free_expr.EXPR_PARS_VAR", expr->pars_var.v!=Null);
			assert("VD-free_expr.EXPR_PARS_VAR.ln", expr->pars_var.v->ln!=Null);
			if (!--expr->pars_var.v->count)
				{
					lput(expr->pars_var.v->ln);
				}
			break;
		case EXPR_LIST:
			tmp=&expr->list;
			while (tmp)
				{
					assert("VD-free_expr.EXPR_LIST", tmp->h.type==EXPR_LIST);
					ret |= free_expr(tmp->source);
					tmp = tmp->next;
				}
			break;
		case EXPR_ASSIGN:
			assert("VD-free_expr.EXPR_ASSIGN", expr->assgn.target!=Null);
			assert("VD-free_expr.EXPR_ASSIGN.ln", expr->assgn.target->ln!=Null);
			assert("VD-free_expr.EXPR_ASSIGN.count", expr->assgn.target->count>0);
			if (!--expr->assgn.target->count)
				{
					lput(expr->assgn.target->ln);
				}
			ret |= free_expr(expr->assgn.source);
			break;
		case EXPR_LNODE:
			assert("VD-free_expr.lnode.lnode", expr->lnode.lnode!=Null);
			lput(expr->lnode.lnode);
			break;
		case EXPR_FLOW:
			break;
/*
		case EXPR_OP3:
			free_expr(expr->op3.op_r);
			free_expr(expr->op3.op_l);
			free_expr(expr->op3.op);
			break;
*/
		case EXPR_OP2:
			ret  = free_expr(expr->op2.op_r);
			ret |= free_expr(expr->op2.op_l);
			break;
		case EXPR_OP:
			ret = free_expr(expr->op.op);
			break;
		}
	return ret;
}


//ln->inode.inode->i_op->lookup(struct inode *,struct dentry *);
//current->fs->pwd->d_inode->i_op->lookup(struct inode *,struct dentry *);

/* alloca te space for lnode */
static lnode * alloc_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	lnode * ln;
	ln = ( lnode * ) kmalloc( sizeof( lnode ), GFP_KERNEL);
	assert("VD-alloc_pars_var", ln != NULL );
	memset( ln , 0, sizeof( lnode ));
	return ln;
}

/* make lnode_dentry from inode, except reiser4 inode */
static lnode * get_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */ ,
			 struct inode * inode /* inode for make lnode */)
{
	lnode * ln;
	reiser4_key key, * k_rez,* l_rez;
	PTRACE( ws, " inode=%p", inode );
	
#if 0                      /*def NOT_YET*/
	if ( is_reiser4_inode( inode ) )
		{

			k_rez             = build_sd_key( inode, &key);
			ln                = lget(  LNODE_REISER4_INODE, get_inode_oid( inode) );
			//			ln->lw.lw_sb = inode->isb;
			ln->reiser4_inode.inode = /*????*/  inode->isb;
			ln->reiser4_inode.inode = /*????*/  inode->isb;
			PTRACE( ws, "r4: lnode=%p", ln );
		}
	else
#endif
		{
			ln                = lget( LNODE_DENTRY, get_inode_oid( inode) );
			ln->dentry.dentry = d_alloc_anon(inode);
			PTRACE( ws, "no r4 lnode=%p,dentry=%p", ln, ln->dentry.dentry);
		}
	PTRACE( ws, " lnode=%p", ln );
	return ln;
}

/*  allocate work space, initialize work space, tables, take root inode and PWD inode */
static struct reiser4_syscall_w_space * reiser4_pars_init()
{
	struct reiser4_syscall_w_space * ws;
                                                            /* allocate work space for parser
							       working variables, attached to this call */
	ws = kmalloc( sizeof( struct reiser4_syscall_w_space ), GFP_KERNEL );
	assert("VD_allock work space", ws != NULL);
	memset( ws, 0, sizeof( struct reiser4_syscall_w_space ));
	ws->ws_yystacksize = MAXLEVELCO; /* must be 500 by default */
	ws->ws_yymaxdepth  = MAXLEVELCO; /* must be 500 by default */
	                                                    /* allocate first part of working tables
							       and initialise headers */
	ws->freeSpHead          = freeSpaceAlloc();
	ws->freeSpCur           = ws->freeSpHead;
	ws->wrdHead             = NULL;
	ws->root_e              = init_root(ws);
	ws->cur_level           = alloc_new_level(ws);
	ws->cur_level->cur_exp  = init_pwd(ws);
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
	ws->cur_level->prev     = NULL;
	ws->cur_level->next     = NULL;
	ws->cur_level->level    = 0;
	ws->cur_level->stype    = 0;
	return ws;
}


/* level up of parsing level */
static void level_up(struct reiser4_syscall_w_space *ws /* work space ptr */,
		     long type /* type of level we going to */)
{
	PTRACE(ws, "%s", "begin");
	if (ws->cur_level->next==NULL)
		{
			ws->cur_level->next       = alloc_new_level(ws);
			ws->cur_level->next->prev = ws->cur_level;
			ws->cur_level->next->next = NULL;
			ws->cur_level->level      = ws->cur_level->prev->level+1;
		}
	ws->cur_level           = ws->cur_level->next;
	ws->cur_level->stype    = type;
	ws->cur_level->cur_exp  = ws->cur_level->prev->wrk_exp;                  /* current pwd for new level */
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
}

/* level down of parsing level */
static  void  level_down(struct reiser4_syscall_w_space * ws /* work space ptr */,
			 long type1 /* type of level that was up( for checking) */,
			 long type2 /* type of level that is down(for checking)*/)
{
	assert("VD-level_down: type mithmatch", type1==type2);
	assert("VD-level_down: type mithmatch with level", type1==ws->cur_level->stype);
//	path_release(ws->cur_level->path_walk->nd); ??????
// this is wrong ????	ws->cur_level->prev->wrk_exp = ws->cur_level->wrk_exp ;           /* current wrk for new level */
	ws->cur_level                = ws->cur_level->prev;
}

/* move_selected_word - copy term from input bufer to free space.
 * if it need more, move freeSpace to the end.
 * otherwise next term will owerwrite it
 *  freeSpace is a kernel space no need make getnam().
 * exclude is for special for string: store without ''
 */
static void move_selected_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
			       int exclude  /* TRUE - for storing string without first and last symbols
					       FALS - for storing names */ )
{
	int i;
	/*	char * s= ws->ws_pline;*/
	if (exclude)
		{
			ws->yytext++;
		}
	for( ws->tmpWrdEnd = ws->freeSpCur->freeSpace; ws->yytext < curr_symbol(ws); )
		{
			i=0;
			//			while( *ws->yytext == '\'' )
			//				{
			//					ws->yytext++;
			//					i++;
			//				}
			//			while ( ws->yytext >  curr_symbol(ws) )
			//				{
			//					i--;
			//					ws->yytext--;
			//				}
			//			if ( i ) for ( i/=2; i; i-- )      *ws->tmpWrdEnd++='\'';    /*   in source text for each '' - result will '   */

			if ( *ws->yytext == '\\' )           /*         \????????   */
				{
					int tmpI;
					ws->yytext++;
					switch ( tolower( (int)*(ws->yytext) ) )
						{
						case 'x':                       /*  \x01..9a..e  */
							i = 0;
							tmpI = 1;
							while( tmpI)
								{
									if (isdigit( (int)*(ws->yytext) ) )
										{
											i = (i << 4) + ( *ws->yytext++ - '0' );
										}
									else if( tolower( (int) *(ws->yytext) ) >= 'a' && tolower( (int)*(ws->yytext) ) <= 'e' )
										{
											i = (i << 4) + ( *ws->yytext++ - 'a' + 10 );
										}
									else
										{
											if ( tmpI & 1 )
												{
													yyerror( ws, LEX_XFORM ); /* x format has odd number of symbols */
												}
											tmpI = 0;
										}
									if ( tmpI && !( tmpI++ & 1 ) )
										{
											*ws->tmpWrdEnd++ = (unsigned char) i;
											i = 0;
										}
								}
							break;
						}
				}
			else *ws->tmpWrdEnd++ = *ws->yytext++;
	                if( ws->tmpWrdEnd > (ws->freeSpCur->freeSpaceMax - sizeof(wrd_t)) )
		                {
					
					assert ("VD sys_reiser4. selectet_word:Internal space buffer overflow: input token exceed size of bufer",
						ws->freeSpCur->freeSpace > ws->freeSpCur->freeSpaceBase);
						/* we can reallocate new space and copy all
						   symbols of current token inside it */
					{
						freeSpace_t * tmp;
						tmp=ws->freeSpCur;
						ws->freeSpCur = freeSpaceNextAlloc(ws);
						assert ("VD sys_reiser4:Internal text buffer overflow: no enouse mem", ws->freeSpCur !=NULL);
						{
							int i;
							i = ws->tmpWrdEnd - tmp->freeSpace;
							memmove( ws->freeSpCur->freeSpace, tmp->freeSpace, i );
							ws->tmpWrdEnd = ws->freeSpCur->freeSpace + i;
						}
					}
		                }
                }
#if 0
	if (exclude)
		{
			ws->tmpWrdEnd--;
		}
#endif
	*ws->tmpWrdEnd++ = '\0';
}


/* compare parsed word with keywords*/
static int b_check_word(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	int i, j, l;
	j=sizeof(pars_key)/(sizeof(char*)+sizeof(int))-1;
	l=0;
	while( ( j - l ) >= 0 )
		{
			i  =  ( j + l /*+ 1*/ ) >> 1;
			switch( strcmp( pars_key[i].wrd, ws->freeSpCur->freeSpace ) )
				{
				case  0:
					PTRACE(ws,"founded: i=%d, %s, %d", i, pars_key[i].wrd, pars_key[i].class);
					return( pars_key[i].class );
					break;
				case  1: j = i - 1;               break;
				default: l = i + 1;               break;
				}
		}
	return(0);
}


/* comparing parsed word with already stored words, if not compared, storing it */
static __inline__ wrd_t * _wrd_inittab(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	wrd_t * cur_wrd;
	wrd_t * new_wrd;
	int len;
	new_wrd =  ws->wrdHead;
#if 0
	len = strlen( ws->freeSpCur->freeSpace) ;
#else
	len = ws->tmpWrdEnd - ws->freeSpCur->freeSpace - 1 ;
#endif
        PTRACE( ws, "wrd %s len=%d wrdHead=%p", ws->freeSpCur->freeSpace, len ,ws->wrdHead );
	cur_wrd = NULL;
	while ( !( new_wrd == NULL ) )
		{
			cur_wrd = new_wrd;
			if ( cur_wrd->u.len == len )
				{
					if( !memcmp( cur_wrd->u.name, ws->freeSpCur->freeSpace, cur_wrd->u.len ) )

						{
							PTRACE( ws, "wrd %s len=%d founded=%p", ws->freeSpCur->freeSpace, len ,cur_wrd );
							return cur_wrd;
						}
				}
			new_wrd = cur_wrd->next;
		}
	new_wrd         = ( wrd_t *)(ws->freeSpCur->freeSpace + ROUND_UP( len+1 ));
	new_wrd->u.name = ws->freeSpCur->freeSpace;
	new_wrd->u.len  = len;
	ws->freeSpCur->freeSpace= (char*)new_wrd + ROUND_UP(sizeof(wrd_t));
	new_wrd->next   = NULL;
	if (cur_wrd==NULL)
		{
			ws->wrdHead   = new_wrd;
		}
	else
		{
			cur_wrd->next = new_wrd;
		}
	PTRACE( ws, "wrd  len=%d new=%p, name=%p name=%s len=%d", len , new_wrd, new_wrd->u.name, new_wrd->u.name, new_wrd->u.len );
	return new_wrd;
}

/* lexical analisator for yacc automat */
static int reiser4_lex( struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	char term,n,i;
	int ret;
	char lcls;
//	char * s ;

//	s = curr_symbol(ws);              /* first symbol or Last readed symbol of the previous token parsing */
	if ( *curr_symbol(ws) == 0 ) return  0;        /* end of string is EOF */

	while(ncl[(int)*curr_symbol(ws)]==Blk)
		{
			next_symbol(ws);
			if ( *curr_symbol(ws) == 0 ) return  0;  /* end of string is EOF */
		}


	lcls    =       ncl[(int)*curr_symbol(ws)];
	ws->yytext  = curr_symbol(ws);
	term = 1;
	while( term )
		{
			n=lcls;
			while (  n > 0   )
				{
					next_symbol(ws);
					lcls=n;
					n = lexcls[ (int)lcls ].c[ (int)i=ncl[ (int)*curr_symbol(ws) ] ];
				}
			if ( n == OK )
				{
					term=0;
				}
			else
				{
					yyerror ( ws, LEXERR2, (lcls-1)* 20+i );
					return(0);
				}
		}
	switch (lcls)
		{
		case Blk:
		case Ste:
			yyerror(ws,LEX_Ste);
			break;
		case Wrd:
			move_selected_word( ws, lexcls[(int) lcls ].c[0] );
			if ( !(ret = b_check_word(ws)) )   /* if ret>0 this is keyword */
				{                          /*  this is not keyword. tray check in worgs. ret = Wrd */
					ret=lexcls[(int) lcls ].term;
					ws->ws_yylval.wrd = _wrd_inittab(ws);
				}
			break;
		case Int:
		case Ptr:
		case Pru:
		case Str: /*`......"*/
			move_selected_word( ws, lexcls[(int) lcls ].c[0] );
			ret=lexcls[(int) lcls ].term;
			ws->ws_yylval.wrd = _wrd_inittab(ws);
			break;
			/*
			move_selected_word( ws, lexcls[ lcls ].c[0] );
			ret=lexcls[ lcls ].term;
			ws->ws_yyval.w = _wrd_inittab(ws);
			break;
			*/
		case Stb:
		case Com:
		case Mns:
		case Les:
		case Slh:
		case Bsl: /*\ */
		case Sp1: /*;*/
		case Sp2: /*:*/
		case Dot: /*.*/
		case Sp4: /*=*/
		case Sp5: /*>*/
		case Sp6: /*?*/
		case ASG:/*<-*/
		case App:/*<<-*/
		case Lnk:/*->*/
			ret=lexcls[(int) lcls ].term;
			break;
		case Lpr:
		case Rpr:
			ws->ws_yylval.charType = *ws->yytext ;
			ret=lexcls[(int) lcls ].term;
			break;
		default :                                /*  others  */
			ret=*ws->yytext;
			break;
		}
    PTRACE(ws, "ret=%d", ret);
	return ret;
}



/*==========================================================*/

/* allocate new expression @type */
static expr_v4_t * alloc_new_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
				  int type /* type of new expression */)
{
	expr_v4_t * e;
	e         = ( expr_v4_t *)  list_alloc( ws, sizeof(expr_v4_t));
	e->h.type = type;
	return e;
}

/* store NULL name in word table */
wrd_t * nullname(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	PTRACE(ws, "%s", "begin");
	ws->tmpWrdEnd = ws->freeSpCur->freeSpace;
	*ws->tmpWrdEnd++ = 0;
	return _wrd_inittab(ws);
}

/* initialize node  for root lnode */
static expr_v4_t *  init_root(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	expr_v4_t * e;
	e                  = alloc_new_expr(ws,EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var(ws,NULL);
	e->pars_var.v->w      = nullname(ws) ; /* or '/' ????? */
	e->pars_var.v->ln     = get_lnode(ws,current->fs->root->d_inode) ;
	e->pars_var.v->parent = NULL;
	return e;
}

/* initialize node  for PWD lnode */
static expr_v4_t *  init_pwd(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	expr_v4_t * e;
	e                  = alloc_new_expr(ws,EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var(ws,ws->root_e->pars_var.v);

	e->pars_var.v->w      = nullname(ws) ;  /* better if it will point to full pathname for pwd */

	e->pars_var.v->ln     = get_lnode(ws,current->fs->pwd->d_inode) ;
	e->pars_var.v->parent = ws->root_e->pars_var.v;
	return e;
}


#if 0
static expr_v4_t *  pars_lookup(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	not ready;
	pars_var_t * rez_pars_var;
	pars_var_t * this_l;
	this_l = getFirstPars_Var(e1);
	while(this_l != NULL )
		{
		}
	assert("pars_lookup:lnode is null",rez_pars_var->ln!=NULL);
	memcpy( &curent_dentry.d_name   , w, sizeof(struct qstr));<---------------
	if( ( rez_pars_var->ln = pars_var->ln->d_inode->i_op->lookup( pars_var->ln->d_inode, &curent_dentry) ) == NULL )
		{
			/* lnode not exist: we will not need create it. this is error*/
		}
}
#endif

/*    Object_Name : begin_from name                 %prec ROOT       { $$ = pars_expr( ws, $1, $2 ) ; }
                  | Object_Name SLASH name                           { $$ = pars_expr( ws, $1, $3 ) ; }  */
static expr_v4_t *  pars_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
			      expr_v4_t * e1 /* first expression ( not yet used)*/,
			      expr_v4_t * e2 /* second expression*/)
{
	ws->cur_level->wrk_exp=e2;
	return e2;
}

static pars_var_t * getFirstPars_VarFromExpr(struct reiser4_syscall_w_space * ws )
{
	pars_var_t * ret;
	expr_v4_t * e = ws->cur_level->wrk_exp;
	switch (e->h.type)
		{
		case EXPR_PARS_VAR:
			ret = e->pars_var.v;
			break;
		default:

		}
	return ret;
}

/* search pars_var for @w */
static expr_v4_t *  lookup_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
				wrd_t * w /* word to search for */)
{
	expr_v4_t * e;
	pars_var_t * cur_pars_var;
	PTRACE(ws, "&w=%p,w->u.name=%p, %s",w,w->u.name,w->u.name);
#if 1           /* tmp.  this is fist version.  for II we need do "while" throus expression for all pars_var */
	cur_pars_var        = ws->cur_level->wrk_exp->pars_var.v;



#else
	cur_pars_var       = getFirstPars_VarFromExpr(ws);
	while(pars_var!=NULL)
		{
#endif



	e             = alloc_new_expr( ws, EXPR_PARS_VAR );

	e->pars_var.v    = lookup_pars_var_word( ws, cur_pars_var, w );


#if 0
			pars_var=getNextPars_VarFromExpr(ws);
		}
 all rezult mast be connected to expression.
#endif


	PTRACE(ws, "end e=%p",e);
	return e;
}

/* set work path in level to current in level */
static inline expr_v4_t * pars_lookup_curr(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for pwd of level */
	return ws->cur_level->wrk_exp;
}

/* set work path in level to root */
static inline expr_v4_t * pars_lookup_root(struct reiser4_syscall_w_space * ws)
{
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return ws->cur_level->wrk_exp;
}

/* seach @parent/w in internal table. if found return it, else @parent->lookup(@w) */
static pars_var_t *  lookup_pars_var_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
				    pars_var_t * parent /* parent for w       */,
				    wrd_t * w        /* to lookup for word */)
{
	int error;
	int result=0;
	struct dentry  * de, * de_rez;
	reiser4_key key,* k_rez;
	coord_t coord;
	lock_handle lh;
	item_plugin *iplug;
	pars_var_t * rez_pars_var;
	pars_var_t * last_pars_var;
	PTRACE(ws, "begin ws->Head_pars_var=%p, parent=%p w=%p",ws->Head_pars_var,parent,w);

	last_pars_var  = NULL;
	rez_pars_var   = ws->Head_pars_var;
	while (rez_pars_var!=NULL)
		{
			if( rez_pars_var->parent == parent && rez_pars_var->w == w)
				{
					rez_pars_var->count++;
					return rez_pars_var;
				}
			last_pars_var = rez_pars_var;
			rez_pars_var  = rez_pars_var->next;
		}
//	reiser4_fs        = 0;
	rez_pars_var         = alloc_pars_var(ws, last_pars_var);
	rez_pars_var->w      = w;
	rez_pars_var->parent = parent;

	switch (parent->ln->h.type)
		{
		case LNODE_DENTRY:
		case LNODE_INODE:
			if (parent->ln->h.type==LNODE_DENTRY)
				{
					de     =  parent->ln->dentry.dentry;
				}
			else
				{
					de = d_alloc_anon(parent->ln->inode.inode);
				}
			de_rez = lookup_one_len( w->u.name, de, w->u.len); /* namei.c */
			rez_pars_var->ln  = lget( LNODE_DENTRY, get_inode_oid( de_rez->d_inode) );
			PTRACE(ws, "rez de=%p",rez_pars_var->ln->dentry.dentry);
			break;
		case LNODE_PSEUDO:
			PTRACE(ws, "parent pseudo=%p",parent->ln->pseudo.host);
			break;
/*		case LNODE_LW: */
		case LNODE_REISER4_INODE:
			rez_pars_var->ln->h.type        = LNODE_REISER4_INODE /* LNODE_LW */;

#if 0                   /*   NOT_YET  ???? */


			result = coord_by_key(get_super_private(parent->ln->lw.lw_sb)->tree,
					      parent->ln->lw.key,
					      &coord,
					      &lh,
					      ZNODE_READ_LOCK,
					      FIND_EXACT,
					      LEAF_LEVEL,
					      LEAF_LEVEL,
					      CBK_UNIQUE,
					      0);
			//			if (REISER4_DEBUG && result == 0)
			//				check_sd_coord(coord, key);

			if (result != 0)
				{
					lw_key_warning(parent->ln->lw.key, result);
				}
			else
				{
					switch(item_type_by_coord(coord))
						{
						case STAT_DATA_ITEM_TYPE:
							printk("VD-item type is STAT_DATA\n");
						case DIR_ENTRY_ITEM_TYPE:
							printk("VD-item type is DIR_ENTRY\n");
							iplug = item_plugin_by_coord(coord);
							if (iplug->b.lookup != NULL)
								{
									iplug->b.lookup();   /*????*/
								}
							








						case INTERNAL_ITEM_TYPE:
							printk("VD-item type is INTERNAL\n");
						case ORDINARY_FILE_METADATA_TYPE:
							

						case OTHER_ITEM_TYPE:
							printk("VD-item type is OTHER\n");
						}

				}
			/*??  lookup_sd     find_item_obsolete */
#endif

		case LNODE_NR_TYPES:
			break;
		}
	PTRACE(ws, "de=%p       w->u.name= %p, u.name->%s, u.len=%d",de,w->u.name,w->u.name,w->u.len);

	return rez_pars_var;

}


/* execute code: walk tree, call plugins and return value */
static expr_v4_t * make_do_it(struct reiser4_syscall_w_space * ws /* work space ptr */,
			      expr_v4_t * e1 /* expression for execution (not yet used)*/ )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

/* if_then_else procedure */
static expr_v4_t * if_then_else(struct reiser4_syscall_w_space * ws /* work space ptr */,
				expr_v4_t * e1 /* expression of condition */,
				expr_v4_t * e2 /* expression of then */,
				expr_v4_t * e3 /* expression of else */ )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

/* not yet */
static expr_v4_t * if_then(struct reiser4_syscall_w_space * ws /* work space ptr */,
			   expr_v4_t * e1 /**/,
			   expr_v4_t * e2 /**/ )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

/* not yet */
static void goto_end(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
}


/* STRING_CONSTANT to expression */
static expr_v4_t * constToExpr(struct reiser4_syscall_w_space * ws /* work space ptr */,
			       wrd_t * e1 /* constant for convert to expression */)
{
	expr_v4_t * new_expr = alloc_new_expr(ws, EXPR_WRD );
	new_expr->wd.s = e1;
	return NULL;
}

/* allocate EXPR_OP2  */
static expr_v4_t * allocate_expr_op2(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr */,
				      expr_v4_t * e2 /* second expr */,
				      int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr( ws, EXPR_OP2 );
	assert("VD alloc op2", ret!=NULL);
	ret->h.exp_code = op;
	ret->op2.op_l = e1;
	ret->op2.op_r = e2;
	return ret;
}

/* allocate EXPR_OP  */
static expr_v4_t * allocate_expr_op(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr */,
 				      int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr(ws, EXPR_OP2 );
	assert("VD alloc op2", ret!=NULL);
	ret->h.exp_code = op;
	ret->op.op = e1;
	return ret;
}


/* concatenate expressions */
static expr_v4_t * connect_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of connecting */,
				      expr_v4_t * e2 /* second expr of connecting */)
{
	return allocate_expr_op2( ws, e1, e2, CONNECT );
}


/* compare expressions */
static expr_v4_t * compare_EQ_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_EQ );
}


static expr_v4_t * compare_NE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_NE );
}


static expr_v4_t * compare_LE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LE );
}


static expr_v4_t * compare_GE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GE );
}


static expr_v4_t * compare_LT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LT );
}


static expr_v4_t * compare_GT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GT );
}


static expr_v4_t * compare_OR_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_OR );
}


static expr_v4_t * compare_AND_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */,
				      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_AND );
}


static expr_v4_t * not_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */)
{
	return allocate_expr_op( ws, e1, COMPARE_NOT );
}


/**/
static expr_v4_t * check_exist(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of comparing */)
{
	return e1;
}

/* union lists */
static expr_v4_t * union_lists(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of connecting */,
				      expr_v4_t * e2 /* second expr of connecting */)
{
	expr_list_t *next, *last;
	assert("VD-connect_list", e1->h.type == EXPR_LIST);

	last = (expr_list_t *)e1;
	next = e1->list.next;
	while ( next )                   /* find last in list */
		{
			last = next;
			next = next->next;
		}
	if ( e2->h.type == EXPR_LIST )
		{                       /* connect 2 lists */
			last->next = (expr_list_t *) e2;
		}
	else
		{                      /* add 2 EXPR to 1 list */
			next = (expr_list_t *) alloc_new_expr(ws, EXPR_LIST );
			assert("VD alloct list", next!=NULL);
			next->next = NULL;
			next->source = e2;
			last->next = next;
		}
	return e1;
}


/*  make list from expressions */
static expr_v4_t * list_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
				      expr_v4_t * e1 /* first expr of list */,
				      expr_v4_t * e2 /* second expr of list */)
{
	expr_v4_t * ret;

	if ( e1->h.type == EXPR_LIST )  
		{
			ret = union_lists( ws, e1, e2);
		}
	else
		{
			
			if ( e2->h.type == EXPR_LIST )  
				{
					ret = union_lists( ws, e2, e1);
				}
			else
				{				
					ret = alloc_new_expr(ws, EXPR_LIST );
					assert("VD alloct list 1", ret!=NULL);
					ret->list.source = e1;
					ret->list.next = (expr_list_t *)alloc_new_expr(ws, EXPR_LIST );
					assert("VD alloct list 2",ret->list.next!=NULL);
					ret->list.next->next = NULL;
					ret->list.next->source = e2;
				}
		}
	return ret;
}



static inline expr_v4_t * list_async_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return list_expression( ws, e1 , e2  );
}



static expr_v4_t * assign(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	/* while for each pars_var in e1*/
	pump(e1->pars_var.v,e2);
	return e2;   /* tmp.  */
}



static expr_v4_t * assign_invert(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e2;
}


static expr_v4_t * symlink(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e2;
}



/*
 A flow is a source from which data can be obtained. A Flow can be one of these types:

   1. memory area in user space. (char *area, size_t length)
   2. memory area in kernel space. (caddr_t *area, size_t length)
   3. file-system object (lnode *obj, loff_t offset, size_t length)
*/
#if 0
typedef struct connect connect_t;

struct connect
{
	expr_v4_t * (*u)(pars_var_t *dst, expr_v4_t *src);
};

static expr_v4_t * reiser4_assign( pars_var_t *dst, expr_v4_t *src )
{
    int           ret_code;
    file_plugin  *src_fplug;
    file_plugin  *dst_fplug;
    connect_t     connection;

    /*
     * select how to transfer data from @src to @dst.
     *
     * Default implementation of this is common_transfer() (see below).
     *
     * Smart file plugin can choose connection based on type of @dst.
     *
     */
#if 0
    connection = dst->v->fplug -> select_connection( src, dst );
#else
    /*    connection.u=common_transfer;*/
#endif

    /* do transfer */
    return common_transfer( &dst, &src );
}

#endif


static  int source_not_empty(expr_v4_t *source)
{
	return 0;
}

static mm_segment_t __ski_old_fs;	


#define START_KERNEL_IO_GLOB	                \
		__ski_old_fs = get_fs();	\
		set_fs( KERNEL_DS )

#define END_KERNEL_IO_GLOB			\
		set_fs( __ski_old_fs );		

#define PUMP_BUF_SIZE (PAGE_CACHE_SIZE)


static tube_t * get_tube_general(pars_var_t *sink, expr_v4_t *source)
{
	tube_t * tube=NULL;
	tube = kmalloc(sizeof(struct tube), GFP_KERNEL);
	memset( tube , 0, sizeof( struct tube ));

	PTRACE1( "%s", "begin");
	assert("get_tube_general: no tube",!IS_ERR(tube));
	assert("get_tube_general: src expression wrong",source->h.type == EXPR_PARS_VAR);
	assert("get_tube_general: src no dentry",source->pars_var.v->ln->h.type== LNODE_DENTRY);
	assert("get_tube_general: dst no dentry",sink->ln->h.type== LNODE_DENTRY);

	tube->buf = kmalloc(PUMP_BUF_SIZE, GFP_KERNEL);
	memset( tube->buf , 0, PUMP_BUF_SIZE);

	tube->readoff     = 0;
	tube->writeoff    = 0;

	tube->type_offset = 0;
	tube->offset      = 0;
	tube->len         = 0;
	tube->used        = 0;
	tube->src         = dentry_open(source->pars_var.v->ln->dentry.dentry, NULL, O_RDONLY);;
	tube->dst         = dentry_open(sink->ln->dentry.dentry, NULL, O_WRONLY);;
//	tube->source      = source;
//	tube->sink        = sink;
	START_KERNEL_IO_GLOB;
	return tube;
}

static size_t reserv_space_in_sink(tube_t * tube, size_t len )
{
	PTRACE1( "%s", "begin tube->buf=%p, tube->len=%d, &tube->readoff=%d",tube->buf, tube->len, &tube->readoff);
	return 	vfs_read(tube->src, tube->buf, len, &tube->readoff);
}

static size_t get_available_len(struct file * fl)
{
	PTRACE1( "%s", "begin");
	return PUMP_BUF_SIZE;
}

static int prep_tube_general(tube_t * tube)
{
	PTRACE1( "%s", "begin");
	tube->len = reserv_space_in_sink( tube, get_available_len(tube->src) );
	return tube->len;
}

static int source_to_tube_general(tube_t * tube)
{
//	tube->source->fplug->read(tube->offset,tube->len);
	PTRACE1( "%s", "begin");
	return tube->len;
}

static int tube_to_sink_general(tube_t * tube)
{
//	tube->sink->fplug->write(tube->offset,tube->len);
//	tube->offset+=tube->len;
	PTRACE1( "%s", "begin tube->buf=%p, tube->len=%d, &tube->writeoff=%d",tube->buf, tube->len, &tube->writeoff);
	return 	vfs_write(tube->dst, tube->buf, tube->len, &tube->writeoff);
}

static void put_tube(tube_t * tube)
{
	PTRACE1( "%s", "begin");
	END_KERNEL_IO_GLOB;
	kfree(tube->buf);
	kfree(tube);
}


/*
  Often connection() will be a method that employs memcpy(). Sometimes
  copying data from one file plugin to another will mean transforming
  the data. What reiser4_assign does depends on the type of the flow
  and sink. If @flow is based on the kernel-space area, memmove() is
  used to copy data. If @flow is based on the user-space area,
  copy_from_user() is used. If @flow is based on a file-system object,
  flow_place() uses the page cache as a universal translator, loads
  the object's data into the page cache, and then copies them into
  @area. Someday methods will be written to copy objects more
  efficiently than using the page cache (e.g. consider copying holes
  [add link to definition of a hole]), but this will not be
  implemented in V4.0.
*/
static int  pump( pars_var_t *sink, expr_v4_t *source )
{
      tube_t * tube;
      int ret_code;
      int (*prep_tube)(tube_t *);
//      int (*prep_tube)(expr_v4_t *);
      int (*source_to_tube)(tube_t *);
      int (*tube_to_sink)(tube_t *);

      //      pos_t source_pos;
      //      pos_t sink_pos;

	PTRACE1( "%s", "begin");

      /* remember to write code for freeing tube, error handling, etc. */
#if 0
      tube = sink->fplug -> get_tube( sink, source);
      prep_tube = sink->fplug->prep_tube (tube);
      source_to_tube = source->fplug->source_to_tube;
      tube_to_sink = sink->fplug->tube_to_sink;
#else
      tube = get_tube_general( sink, source);
      prep_tube = prep_tube_general;
      source_to_tube = source_to_tube_general;
      tube_to_sink = tube_to_sink_general;
#endif

      while( prep_tube( tube ) ) {
        ret_code = source_to_tube( tube );
        ret_code = tube_to_sink( tube );
      }
      put_tube(tube);
      return ret_code;
	PTRACE1( "%s", "end");
}




/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
