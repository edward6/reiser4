/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * functions for parser.y
 */


#include "lib.h"

#ifdef CONFIG_REISER4_FS_SYSCALL_DEBUG
static void yy_exit()
{
}
#endif

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
[Res]={  0,{0, 
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Res]={  0,{0, 
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Res]={  0,{0, 
OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK ,  OK ,OK ,OK ,OK ,OK ,OK ,OK ,OK }},
[Res]={  0,{0, 
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







static void yyerror( struct reiser4_syscall_w_space *ws, int msgnum , ...)
{
	printk("\nreiser4 parser: error # %d\n", msgnum);
}

static int yywrap()
{
    return 1;
}


static void freeList(freeSpace_t * list)
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


static int reiser4_pars_free(struct reiser4_syscall_w_space * ws)
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

static freeSpace_t * freeSpaceAlloc()
{
	freeSpace_t * fs;
	if ( ( fs = ( freeSpace_t * ) kmalloc( sizeof( freeSpace_t ),GFP_KERNEL ) ) != NULL )
		{
			initNextFreeSpace(fs);
		}
	return fs;
}

#define get_first_freeSpHead(ws) (ws)->freeSpHead
#define get_next_freeSpHead(curr) (curr)->freeSpace_next

static freeSpace_t * freeSpaceNextAlloc(struct reiser4_syscall_w_space * ws)
{
	freeSpace_t * curr,* next;
	PTRACE(ws, "%s", "begin");
	curr=NULL;
	next = get_first_freeSpHead(ws);
	while (next)
		{
	PTRACE(ws, "%s", "next");
			curr = next;
			next = get_next_freeSpHead(curr);
		}
	if ((next = freeSpaceAlloc())!=NULL)
		{
			if(curr==NULL)
				{
					ws->freeSpHead=next;
				}
			else
				{
					curr->freeSpace_next=next;
				}
			next->freeSpace_next=NULL;
		}
else
{

	PTRACE(ws, "%s", "else");
}
	return next;
}

static char* list_alloc(struct reiser4_syscall_w_space * ws, int size)
{
	char * rez;
	PTRACE(ws, "%s, space=%p, free=%p", "begin",ws->freeSpCur,ws->freeSpCur->freeSpace);
	assert("list_alloc:bad ws",ws!=NULL);
	if( (unsigned long)(ws->freeSpCur->freeSpace) > ((unsigned long)(ws->freeSpCur->freeSpaceMax) - size) )
		{
			ws->freeSpCur = freeSpaceNextAlloc(ws);
			assert("VD-LIST_ALLOC",ws->freeSpCur!=NULL);
		}
	rez = ws->freeSpCur->freeSpace;
	assert("VD-LIST_ALLOC:rez==NULL",rez!=NULL);
	ws->freeSpCur->freeSpace += ROUND_UP(size);
	return rez;
}


static streg_t *alloc_new_level(struct reiser4_syscall_w_space * ws)
{
	PTRACE(ws, "%s", "begin");
	return ( streg_t *)  list_alloc(ws,sizeof(streg_t));
}

static vnode_t * alloc_vnode(struct reiser4_syscall_w_space * ws, vnode_t * last_vnode)
{
	vnode_t * vnode;
	PTRACE(ws, "%s", "begin");

	vnode = (vnode_t *)list_alloc(ws,sizeof(vnode_t));

	if ( last_vnode == NULL )
		{
			ws->Head_vnode=vnode;
		}
	else
		{
			last_vnode->next=vnode;
		}
	vnode->next=NULL;
	return vnode;
}






//ln->inode.inode->i_op->lookup(struct inode *,struct dentry *);
//current->fs->pwd->d_inode->i_op->lookup(struct inode *,struct dentry *);



static lnode * get_lnode(struct reiser4_syscall_w_space * ws, struct inode * inode)
{
	lnode * ln,* l_rez;
	reiser4_key * k_rez;
	PTRACE( ws, " inode=%p", inode );

	if ( ( ln = ( lnode * ) kmalloc( sizeof( lnode ), GFP_KERNEL) ) != NULL )
		{
			if (is_reiser4_inode(inode))
				{
					ln->h.type = LNODE_LW;
					k_rez = build_sd_key( inode, &ln->lw.key);
					l_rez = lget( ln, LNODE_LW, get_key_objectid(&ln->lw.key ) );
					PTRACE( ws, "r4: lnode=%p", ln );
				}
			else
				{
					ln->h.type = LNODE_INODE;
					ln->inode.inode = inode;
					PTRACE( ws, "no r4 lnode=%p", ln );
				}
		}
	PTRACE( ws, " lnode=%p", ln );

	return ln;
}

static struct reiser4_syscall_w_space * reiser4_pars_init()
{
	struct reiser4_syscall_w_space * ws;
                                                            /* allocate work space for parser 
							       working variables, attached to this call */
	if ( ( ws = kmalloc( sizeof( struct reiser4_syscall_w_space ), GFP_KERNEL ) )==NULL )
		{
		  return NULL; /*-ENOMEM;*/
		}
	PTRACE(ws, "%s", "allocated");
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



static void level_up(struct reiser4_syscall_w_space *ws, int type)
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


static  void  level_down(struct reiser4_syscall_w_space * ws, int type1, int type2)
{
	PTRACE(ws, "%s", "begin");
	assert("VD-level_down, type mithmatch",type1==type2);
	assert("VD-level_down, type level mithmatch",type2==ws->cur_level->stype);
//	path_release(ws->cur_level->path_walk->nd); ??????
// this is wrong ????	ws->cur_level->prev->wrk_exp = ws->cur_level->wrk_exp ;           /* current wrk for new level */
	ws->cur_level                = ws->cur_level->prev;
}

#define curr_symbol(ws) ((ws)->ws_pline)
#define next_symbol(ws)  (++curr_symbol(ws))
#define tolower(a) a
#define isdigit(a) ((a)>=0 && (a)<=9)

/* move_selected_word - copy term from input bufer to free space. 
 * if it need more, move freeSpace to the end. 
 * otherwise next term will owerwrite it
 *  freeSpace is a kernel space no need make getnam().
 * exclude is for special for string: store without ''
 */

static void move_selected_word(struct reiser4_syscall_w_space * ws, int exclude )
{
	int i;
	/*	char * s= ws->ws_pline;*/
	if (exclude)
		{
			ws->yytext++;
		}
	for( ws->tmpWrdEnd = ws->freeSpCur->freeSpace; ws->yytext <= curr_symbol(ws); )
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
													yyerror( ws, 00 ); /* x format has odd number of symbols */
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
					if ( ws->freeSpCur->freeSpace > ws->freeSpCur->freeSpaceBase ) /* we can reallocate new space and copy all
											       symbols of current token inside it */
						{
							freeSpace_t * tmp;
							tmp=ws->freeSpCur;
							if ( (ws->freeSpCur = freeSpaceNextAlloc(ws))!=NULL)
								{
									int i;
									i = ws->tmpWrdEnd - tmp->freeSpace;
									memmove( ws->freeSpCur->freeSpace, tmp->freeSpace, i );
									ws->tmpWrdEnd = ws->freeSpCur->freeSpace + i;
								}
							else
								{
									yyerror( ws,0 ); /* Internal text buffer overflow: no enouse mem */
									yy_exit();
								}
						}
					else
						{
							yyerror( ws, 111 ); /* Internal space buffer overflow: input token exceed size of bufer */
							yy_exit();
						}
		                }
                }
	if (exclude)
		{
			ws->tmpWrdEnd--;
		}
	*ws->tmpWrdEnd++ = '\0';
	PTRACE(ws, "term is---->%s<----", ws->freeSpCur->freeSpace);
}



static int b_check_word(struct reiser4_syscall_w_space * ws )
{
	int i, j, l;
	j=sizeof(pars_key)/(sizeof(char*)+sizeof(int))-1;
	l=0;
	PTRACE(ws," word for check is------->%s<-------j=%d", ws->freeSpCur->freeSpace, j);
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
	PTRACE(ws,"%s", "false");
	return(0);
}



static __inline__ wrd_t * _wrd_inittab(struct reiser4_syscall_w_space * ws )
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
	PTRACE( ws, "wrd  len=%d new=%p", len , new_wrd );
	return new_wrd;
}

static int reiser4_lex( struct reiser4_syscall_w_space * ws )
{
	char term,n,i;
	int ret;
	char lcls;
	char * s ;
	PTRACE(ws, "%s", "lex1");

	if ( ( s = curr_symbol(ws) ) == 0 ) return EOF;  /* first symbol or Last readed symbol of the previous token parsing */

	lcls    =       ncl[*s];
	ws->yytext  = s;
	term = 1;
	while( term )
		{
//			PTRACE(ws, "while1: lcls=%d,n=%d,i=%d,%c",lcls,n,i,*s);
			while ( ( n = lexcls[ lcls ].c[ i=ncl[ * ( s ) ] ] ) > 0   )
				{
					PTRACE(ws, "while2: lcls=%d,n=%d,i=%d,%c",lcls,n,i,*s);
					lcls=n;
					s = next_symbol(ws);
				}
			if ( n == OK )
				{
					term=0;
				}
			else 
				{
					PTRACE(ws, "error: lcls=%d,n=%d,i=%d,%c",lcls,n,i,*s);
					yyerror ( ws, 2222, (lcls-1)* 20+i, s );
					return(0);
				}
		}
	PTRACE(ws, "lex2: lcls=%d,n=%d,i=%d,%c",lcls,n,i,*s);
	switch (lcls)
		{
		case Blk:
		case Ste: 
			yyerror(ws,0);
			break;
		case Wrd:
			move_selected_word( ws, lexcls[ lcls ].c[0] );
			if ( !(ret = b_check_word(ws)) )   /* if ret>0 this is keyword */
				{                          /*  this is not keyword. tray check in worgs. ret = Wrd */
					PTRACE(ws, "%s ", "no keyword");
					ret=lexcls[ lcls ].term;
					ws->ws_yyval.wrd = _wrd_inittab(ws);
				}
			break;
		case Int:
		case Ptr:
		case Pru:
		case Str: /*`......"*/
			move_selected_word( ws, lexcls[ lcls ].c[0] );
			ret=lexcls[ lcls ].term;
			ws->ws_yyval.wrd = _wrd_inittab(ws);
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
			ret=lexcls[ lcls ].term;
			break;
		case Lpr: 
		case Rpr: 
			ws->ws_yyval.charType = *ws->yytext ;
			ret=lexcls[ lcls ].term;
			break;
		default :                                /*  others  */
			ret=*ws->yytext;
			break;
		}
    PTRACE(ws, "ret=%d", ret);
	return ret;
}



/*==========================================================*/


static expr_v4_t * alloc_new_expr(struct reiser4_syscall_w_space * ws, int type)
{
	expr_v4_t * e;
	PTRACE(ws, "%s", "begin");
	assert("alloc_new_expr: bad ws",ws!=NULL);
	e         = ( expr_v4_t *)  list_alloc( ws, sizeof(expr_v4_t));
	e->h.type = type;
	return e;
}

wrd_t * nullname(struct reiser4_syscall_w_space * ws)
{
	PTRACE(ws, "%s", "begin");
	ws->tmpWrdEnd = ws->freeSpCur->freeSpace;
	*ws->tmpWrdEnd++ = 0;
	return _wrd_inittab(ws);
}

static expr_v4_t *  init_root(struct reiser4_syscall_w_space * ws)
{
	expr_v4_t * e;
	PTRACE(ws, "%s", "begin");
	e                  = alloc_new_expr(ws,EXPR_VNODE);
	e->vnode.v         = alloc_vnode(ws,NULL);
	e->vnode.v->w      = nullname(ws) ; /* or '/' ????? */
	e->vnode.v->ln     = get_lnode(ws,current->fs->root->d_inode) ;
	e->vnode.v->parent = NULL;
	return e;
}

static expr_v4_t *  init_pwd(struct reiser4_syscall_w_space * ws)
{
	expr_v4_t * e;
	PTRACE(ws, "%s", "begin");
	e                  = alloc_new_expr(ws,EXPR_VNODE);
	e->vnode.v         = alloc_vnode(ws,ws->root_e->vnode.v);
	e->vnode.v->w      = nullname(ws) ;  /* better if it will point to full pathname for pwd */
	e->vnode.v->ln     = get_lnode(ws,current->fs->pwd->d_inode) ; 
	e->vnode.v->parent = ws->root_e->vnode.v;
	PTRACE(ws, "%s", "end");
	return e;
}


#if 0
static expr_v4_t *  pars_lookup(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	not ready;
	vnode_t * rez_vnode;
	vnode_t * this_l;
	this_l = getFirstVnode(e1);
	while(this_l != NULL )
		{
		}
	assert("pars_lookup:lnode is null",rez_vnode->ln!=NULL);
	memcpy( &curent_dentry.d_name   , w, sizeof(struct qstr));<---------------
	if( ( rez_vnode->ln = vnode->ln->d_inode->i_op->lookup( vnode->ln->d_inode, &curent_dentry) ) == NULL )
		{
			/* lnode not exist: we will not need create it. this is error*/
		}
}
#endif

static expr_v4_t *  pars_expr(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	PTRACE(ws, "%s", "begin");
	ws->cur_level->wrk_exp=e2;
	return e2;
}

static expr_v4_t *  lookup_word(struct reiser4_syscall_w_space * ws, wrd_t * w)
{
	expr_v4_t * e;
	vnode_t * vnode;
	PTRACE(ws, "%s", "begin");
#if 1           /* tmp.  this is fist version.  for II we need do "while" throus expression for all vnode */
	vnode        = ws->cur_level->wrk_exp->vnode.v; 
#else
	vnode       = getFirsVnodeFromExpr(ws->level->wrk_exp); 
	while(vnode!=NULL)
		{
#endif
	e             = alloc_new_expr( ws, EXPR_VNODE );
	e->vnode.v    = lookup_vnode_word( ws, vnode, w );
#if 0
			vnode=getNextVnodeFromExpr(ws->level->wrk_exp); 
		}
 all rezult mast be connected to expression. 
#endif
	return e;
}


static inline expr_v4_t * pars_lookup_curr(struct reiser4_syscall_w_space * ws)
{
	PTRACE(ws, "%s", "begin");
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for pwd of level */
	return ws->cur_level->wrk_exp;
}


static inline expr_v4_t * pars_lookup_root(struct reiser4_syscall_w_space * ws)
{
	PTRACE(ws, "%s", "begin");
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return ws->cur_level->wrk_exp;
}


static vnode_t *  lookup_vnode_word(struct reiser4_syscall_w_space * ws, vnode_t * vnode, wrd_t * w)
{
	int error;
	int result=0;
	struct dentry  * de;
	vnode_t * rez_vnode;
	vnode_t * last_vnode;
	PTRACE(ws, "%s", "begin");

	last_vnode  = NULL;
	rez_vnode   = ws->Head_vnode;
#if 0
	printk(" %s",w->u.name);
#else
	while (rez_vnode)
		{
			if( rez_vnode->parent == vnode && rez_vnode->w == w)
				{
					rez_vnode->count++;
					return rez_vnode;
				}
			last_vnode = rez_vnode;
			rez_vnode  = rez_vnode->next;
		}
//	reiser4_fs        = 0;
	rez_vnode         = alloc_vnode(ws, last_vnode);
	rez_vnode->w      = w;
	rez_vnode->parent = vnode;

	switch (vnode->ln->h.type)
		{
		case LNODE_DENTRY:
			de = vnode->ln->dentry.dentry;
			break;
		case LNODE_INODE:
			de = d_alloc_anon(vnode->ln->inode.inode);
			break;
		case LNODE_PSEUDO:
		case LNODE_LW:
		case LNODE_NR_TYPES:
			break;
		}
	rez_vnode->ln->h.type= LNODE_DENTRY;
	rez_vnode->ln->dentry.dentry= lookup_one_len( w->u.name, de, w->u.len);
#endif
	return rez_vnode;
}


/* execute code: walk tree, call plugins and return value */
static expr_v4_t * make_do_it(struct reiser4_syscall_w_space * ws, expr_v4_t * e1 )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

static expr_v4_t * if_then_else(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 , expr_v4_t * e3  )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

static expr_v4_t * if_then(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	PTRACE(ws, "%s", "begin");
	return e1;
}

static void goto_end(struct reiser4_syscall_w_space * ws)
{
}


/* STRING_CONSTANT to expression */
static expr_v4_t * constToExpr(struct reiser4_syscall_w_space * ws, wrd_t * e1 )
{
	PTRACE(ws, "%s", "begin");
	return NULL;
}

/* concatenate expressions */
static expr_v4_t * connect_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


/* compare expressions */
static expr_v4_t * compare_EQ_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_NE_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_LE_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_GE_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_LT_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_GT_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_OR_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * compare_AND_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	return e1;
}


static expr_v4_t * not_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1)
{
	return e1;
}


/**/
static expr_v4_t * check_exist(struct reiser4_syscall_w_space * ws, expr_v4_t * e1)
{
	return e1;
}


static expr_v4_t * list_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return e2; /* for tmp */
}



static inline expr_v4_t * list_async_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return list_expression( ws, e1 , e2  );
}



static expr_v4_t * assign(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	/* while for each vnode in e1*/
	pump(e1->vnode.v,e2);
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
	expr_v4_t * (*u)(vnode_t *dst, expr_v4_t *src);
};

static expr_v4_t * reiser4_assign( vnode_t *dst, expr_v4_t *src )
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


static tube_t * get_tube_general(vnode_t *sink, expr_v4_t *source)
{
	tube_t * tube=NULL;
	tube = kmalloc(sizeof(struct tube), GFP_KERNEL);

	assert("get_tube_general: no tube",!IS_ERR(tube));
	assert("get_tube_general: src expression wrong",source->h.type == EXPR_VNODE);
	assert("get_tube_general: src no dentry",source->vnode.v->ln->h.type== LNODE_DENTRY);
	assert("get_tube_general: dst no dentry",sink->ln->h.type== LNODE_DENTRY);

	tube->buf = kmalloc(PUMP_BUF_SIZE, GFP_KERNEL);

	tube->readoff     = 0;
	tube->writeoff    = 0;

	tube->type_offset = 0;
	tube->offset      = 0;
	tube->len         = 0;
	tube->used        = 0;
	tube->src         = dentry_open(source->vnode.v->ln->dentry.dentry, NULL, O_RDONLY);;
	tube->dst         = dentry_open(sink->ln->dentry.dentry, NULL, O_WRONLY);;
//	tube->source      = source;
//	tube->sink        = sink;
	START_KERNEL_IO_GLOB;
	return tube;
}

static size_t reserv_space_in_sink(tube_t * tube, size_t len )
{
	return 	vfs_read(tube->src, tube->buf, len, &tube->readoff);
}

static size_t get_available_len(struct file * fl)
{
	return PUMP_BUF_SIZE;
}

static int prep_tube_general(tube_t * tube)
{
	tube->len = reserv_space_in_sink( tube, get_available_len(tube->src) );
	return tube->len;
}

static int source_to_tube_general(tube_t * tube)
{
//	tube->source->fplug->read(tube->offset,tube->len);
	return tube->len;
}

static int tube_to_sink_general(tube_t * tube)
{
//	tube->sink->fplug->write(tube->offset,tube->len);
//	tube->offset+=tube->len;
	return 	vfs_read(tube->dst, tube->buf, tube->len, &tube->writeoff);
}

static void put_tube(tube_t * tube)
{
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
static int  pump( vnode_t *sink, expr_v4_t *source )
{
      tube_t * tube;
      int ret_code;
      int (*prep_tube)(tube_t *);
//      int (*prep_tube)(expr_v4_t *);
      int (*source_to_tube)(tube_t *);
      int (*tube_to_sink)(tube_t *);

      //      pos_t source_pos;
      //      pos_t sink_pos;
      

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
