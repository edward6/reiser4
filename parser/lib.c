/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * functions for parser.y
 */

#ifdef CONFIG_REISER4_FS_SYSCALL_DEBUG
static void yy_exit()
{
}
#endif



static void yyerror( struct reiser4_syscall_w_space *ws, int msgnum , ...)
{
	printk("\nreiser4 parser: error # %d\n", msgnum);
}

static int yywrap()
{
    return 1;
}


static void freeList(freeSpace * list)
{
	freeSpace * curr,* next;
	next = list;
	while (next)
		{
			curr = next;
			next = curr->freeSpace_next;
			kfree(curr);
		}
}


static int sys_reiser4_free(struct reiser4_syscall_w_space * ws)
{
	if (ws->freeSpHead)
		{
			freeList(ws->freeSpHead);
		}
	kfree(ws);
	return 0;
}

#define initNextFreeSpase(fs)	(fs)->freeSpace_next = NULL;                                      \
                                (fs)->freeSpaceMax   = (fs)->freeSpaceBase+FREESPACESIZE;         \
			        (fs)->freeSpace      = (fs)->freeSpaceBase

static freeSpace * freeSpaceAlloc()
{
	freeSpace * fs;
	if ( ( fs = ( freeSpace * ) kmalloc( sizeof( freeSpace ),0 ) ) != NULL )
		{
			initNextFreeSpase(fs);
		}
	return fs;
}

#define get_firts_freeSpHead(ws) (ws)->freeSpHead
#define get_next_freeSpHead(curr) (curr)->freeSpace_next

static freeSpace * freeSpaceNextAlloc(struct reiser4_syscall_w_space * ws)
{
	freeSpace * curr,* next;
	curr=NULL;
	next = get_firts_freeSpHead(ws);
	while (next)
		{
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
	return next;
}

static char* list_alloc(struct reiser4_syscall_w_space * ws, int size)
{
	char * rez;
	if( ws->freeSpCur->freeSpace > (ws->freeSpCur->freeSpaceMax - sizeof(streg)) )
		{
			ws->freeSpCur = freeSpaceNextAlloc(ws);
			assert("VD-LIST_ALLOC",ws->freeSpCur!=NULL);
		}
	rez = (char *)ws->freeSpCur->freeSpace;
	(char *)ws->freeSpCur->freeSpace += ROUND_UP(size);
	return rez;
}


static streg_t *alloc_new_level(struct reiser4_syscall_w_space *ws)
{
	return ( streg *)  list_alloc(ws,sizeof(streg_t));
}

static vnode_t * alloc_vnode(ws, last_vnode);
{
	vnode_t * vnode;

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


static lnode * get_lnode(struct inode * in)
{
	struct lnode * ln;

#if 0
	thi is wrong. fist find lnode in existing list- if false then next:

	ln=allocate_lnode();

	if (is_reiser4_inode(in))
		{
			ln->h.type = LNODE_LW;
			k_rez = build_sd_key( in, &ln->lw.key);
			l_rez = lget( ln, LNODE_LW, get_key_objectid(&ln->lw.key ) );
		}
	else
		{
			ln->h.type = LNODE_INODE;
			ln->inode.inode = in;
		}
#endif
}

static struct reiser4_syscall_w_space * sys_reiser4_init()
{
	struct reiser4_syscall_w_space * ws;
                                                            /* allocate work space for parser 
							       working variables, attached to this call */
	if ( ( ws = kmalloc( sizeof( struct reiser4_syscall_w_space ),0 ) )==NULL )
		{
		  return NULL; /*-ENOMEM;*/
		}
	ws->ws_yystacksize = MAXLEVELCO; /* must be 500 by default */
	ws->ws_yymaxdepth  = MAXLEVELCO; /* must be 500 by default */
	

	                                                    /* allocate first part of working tables
							       and initialise headers */
	ws->freeSpHead          = freeSpaceAlloc();
	ws->wrdHead             = NULL;
	ws->root_e              = lookup_root(ws);
	ws->cur_level           = alloc_new_level(ws);
	ws->cur_level->cur_exp  = lookup_pwd(ws);
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
	ws->cur_level->prev     = NULL;
	ws->cur_level->next     = NULL;
	ws->cur_level->level    = 0;
	ws->cur_level->stype    = 0;
	return ws;
}



static streg * level_up(struct reiser4_syscall_w_space *ws, int type)
{
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

//	ws->cur_level->cur_lnode  = ws->cur_level->prev->cur_lnode; /* current pwd for new level */
//	ws->cur_level->cur_lnode  = ws->wln;                        /* current pwd for new level */
//	ws->wln = ws->cur_level->cur_lnode;

	return ws->cur_level;
}


static void level_down(struct reiser4_syscall_w_space * ws, int type)
{
	assert("VD-level_down",type==ws->cur_level->stype);
//	path_release(ws->cur_level->path_walk->nd); ??????
	ws->cur_level->prev->wrk_exp = ws->cur_level->wrk_exp            /* current wrk for new level */
	ws->cur_level                = ws->cur_level->prev;
}

#define curr_symbol(ws) ((ws)->ws_pline)
#define next_symbol(ws) ((*(curr_symbol(ws)++))?curr_symbol(ws):NULL)
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
							freeSpace * tmp;
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
}



static int b_check_word(struct reiser4_syscall_w_space * ws )
{
	int i, j, l;
	j=sizeof(key)/4;
	l=0;
	while( ( j - l ) >= 0 )
		{
			i  =  ( j + l + 1 ) >> 1;
			switch( strcmp( key[i].wrd, ws->freeSpCur->freeSpace ) )
				{
				case  0: return( key[i].class );  break;
				case  1: j = i - 1;               break;
				default: l = i + 1;               break;
				}
		}
	return(0);
}



/*
//#define get_firts_wrd(ws) (ws)->WrdHead
#define get_first_wrd(ws,PREFIX) ws->##PREFIX##Head
#define DEFINE_INITTAB(PREFIX)                                                                                      \
static __inline__ PREFIX * _##PREFIX##inittab(struct reiser4_syscall_w_space * ws )                                 \
{                                                                                                                   \
	PREFIX * cur_wrd;                                                                                           \
	PREFIX * new_wrd;                                                                                           \
	int len;                                                                                                    \
	new_wrd =  get_first_wrd(ws,PREFIX);                                                                        \
	len = strlen( ws->freeSpCur->freeSpace) ;                                                                   \
	len = ws->tmpWrdEnd - ws->freeSpCur->freeSpace - 1 ;                                                        \
	cur_wrd = NULL;                                                                                             \
	while ( !( new_wrd == NULL ) )                                                                              \
		{                                                                                                   \
			cur_wrd = new_wrd;                                                                          \
			if ( cur_wrd->u.len == len )                                                                \
				{                                                                                   \
					if( !memcmp( cur_wrd->u.name, ws->freeSpCur->freeSpace, cur_wrd->u.len ) )  \
						{                                                                   \
							return cur_wrd;                                             \
						}                                                                   \
				}                                                                                   \
			new_wrd = cur_wrd->next;                                                                    \
		}                                                                                                   \
	new_wrd         = ( PREFIX *)(ws->freeSpCur->freeSpace + ROUND_UP( len+1 ));                                \
	new_wrd->u.name = ws->freeSpCur->freeSpace;                                                                 \
	new_wrd->u.len  = len;                                                                                      \
	ws->freeSpCur->freeSpace= (char*)new_wrd + ROUND_UP(sizeof(PREFIX));                                        \
	new_wrd->next   = NULL;                                                                                     \
	if (cur_wrd==NULL)                                                                                          \
		{                                                                                                   \
			ws->PREFIX##Head   = new_wrd;                                                               \
		}                                                                                                   \
	else                                                                                                        \
		{                                                                                                   \
			cur_wrd->next = new_wrd;                                                                    \
		}                                                                                                   \
	return new_wrd;                                                                                             \
}
*/

static __inline__ wrd_t * _wrd_inittab(struct reiser4_syscall_w_space * ws )
{
	wrd_t * cur_wrd;
	wrd_t * new_wrd;
	int len;
	new_wrd =  ws->wrdHead;
	len = strlen( ws->freeSpCur->freeSpace) ;
	len = ws->tmpWrdEnd - ws->freeSpCur->freeSpace - 1 ;
	cur_wrd = NULL;
	while ( !( new_wrd == NULL ) )
		{
			cur_wrd = new_wrd;
			if ( cur_wrd->u.len == len )
				{
					if( !memcmp( cur_wrd->u.name, ws->freeSpCur->freeSpace, cur_wrd->u.len ) )
						{
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
	return new_wrd;
}


/*
#define GET_INITTAB(work_sp,PREFIX) _##PREFIX##_inittab(work_sp)
DEFINE_INITTAB(wrd_t);
DEFINE_INITTAB(vnode_t);
*/


static int reiser4_lex( struct reiser4_syscall_w_space * ws )
{
	char term,n,i;
	int ret;
	char lcls;
	char * s ;

	if ( ( s = curr_symbol(ws) ) == NULL ) return(0);  /* first symbol or Last readed symbol of the previous token parsing */

	lcls    =       ncl[*s] ;
	ws->yytext  = s;
	term = 1;
	while( term )
		{
			while ( ( n = lexcls[ lcls ].c[ i=ncl[ * ( s = next_symbol(ws) ) ] ] ) > 0   )
				{
					lcls=n;
				}
			if ( n == OK )
				{
					term=0;
				}
			else 
				{
					yyerror ( ws, 2222, (lcls-1)* 20+i, s );
					return(0);
				}
		}
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
			ws->ws_yyval.charType = *ws->yytex ;
			ret=lexcls[ lcls ].term;
			break;
		default :                                /*  others  */
			ret=*ws->yytext;
			break;
		}
	return ret;
}



/*==========================================================*/


allocate_lnode()
{
}


#if 0                           /*   */
static lnode * pars_get_current(struct reiser4_syscall_w_space * ws)
{
	return ws->cur_level->de;
//	ws->nd.de  = ws->cur_level->de;
//	ws->nd.mnt = ws->cur_level->mnt;
//	if ( is_reiser4_inode( ws->nd.dentry.d_inode ) )
	if ( is_reiser4_inode( ws->de->d_inode ) )
		{
			ws->cur_level->cur_lnode->h.type = LNODE_LW;
			k_rez = build_sd_key( ws->nd.dentry.d_inode, &ws->cur_level->cur_lnode->lw.key);
			l_rez = lget( ws->cur_level->cur_lnode, LNODE_LW, ws->cur_level->cur_lnode->lw.key.el[KEY_OBJECTID_INDEX]  );
		}
	else
		{
			ws->cur_level->cur_lnode->h.type = LNODE_INODE;
			ws->cur_level->cur_lnode->inode.inode = ws->nd.dentry.d_inode;
		}
}
struct dentry {
	atomic_t d_count;
	unsigned int d_flags;
	struct inode  * d_inode;	/* Where the name belongs to - NULL is negative */
	struct dentry * d_parent;	/* parent directory */
	struct list_head d_hash;	/* lookup hash list */
	struct list_head d_lru;		/* d_count = 0 LRU list */
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children */
	struct list_head d_alias;	/* inode alias list */
	int d_mounted;
	struct qstr d_name;
	unsigned long d_time;		/* used by d_revalidate */
	struct dentry_operations  *d_op;
	struct super_block * d_sb;	/* The root of the dentry tree */
	unsigned long d_vfs_flags;
	void * d_fsdata;		/* fs-specific data */
	struct dcookie_struct * d_cookie; /* cookie, if any */
	unsigned char d_iname[DNAME_INLINE_LEN_MIN]; /* small names */
} ____cacheline_aligned;

struct dentry_operations {
	int (*d_revalidate)(struct dentry *, int);
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	int (*d_delete)(struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
};
#endif








#if 0                           /*   */


//static lnode * 
static int
set_current_lnode(struct reiser4_syscall_w_space * ws, int type)
{
	lnode * ret;
	switch (type)
		{
#ifdef REISER4_FS_SYSCALL_DEBUG
		case BEGIN_FROM_ROOT:
			printk("\n/");
			break;
		case BEGIN_FROM_CURRENT:
			printk("\n");
			break;
#else
		case BEGIN_FROM_ROOT:
			ws->wln = current->........lnode;
			break;
		case BEGIN_FROM_CURRENT:
			ws->wln= ws->cur_level->cur_lnode;
			break;
#endif
		}
	//	ws->wln = ret;/* count++ */
	return 0;
}


static expr_v4_t *  curr_path(struct reiser4_syscall_w_space * ws)
{
	return ws->cur_level->cur_lnode;
}
#endif







static expr_v4_t * pars_lookup_curr(struct reiser4_syscall_w_space * ws, wrd_t * w)
{
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for pwd of level */
	return pars_lookup( ws, ws->cur_level->wrk_exp, w);
}

static expr_v4_t * pars_lookup_root(struct reiser4_syscall_w_space * ws, wrd_t * w)
{
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return pars_lookup( ws, ws->cur_level->wrk_exp, w);
}



//static vnode_t *  pars_lookup(struct reiser4_syscall_w_space * ws, vnode_t * vnode, wrd_t * w)
static expr_v4_t *  pars_lookup(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{

	not ready;
	vnode_t * this_l;
	this_l = getFirstVnode(e1);

	while(this_l != NULL )
		{

		}

	assert("pars_lookup:lnode is null",rez_vnode->ln!=NULL);
	memcpy( &curent_dentry.d_name   , w, syzeof(struct qstr));
	if( ( rez_vnode->ln = vnode->ln->d_inode->i_op->lookup( vnode->ln->d_inode, &curent_dentry) ) == NULL )
		{
			/* lnode not exist: we will not need create it. this is error*/
		}

}

static expr_v4_t * alloc_new_expr(struct reiser4_syscall_w_space * ws, int type)
{
	expr_v4_t * e;

	e         = ( expr_v4_t *)  list_alloc(ws,sizeof(expr_v4_t));
	e->h.type = type;
	return e;
}


static expr_v4_t *  lookup_root(struct reiser4_syscall_w_space * ws)
{
	expr_v4_t * e;
	e                  = alloc_new_expr(ws,EXPR_VNODE);
	e->vnode.v         = alloc_vnode(ws,NULL);
	e->vnode.v->w      = nullname(ws) ; /* or '/' ????? */
	e->vnode.v->ln     = get_lnode(current->fs->root->d_inode) ;???
	e->vnode.v->parent = NULL;
	return e;
}

static expr_v4_t *  lookup_pwd(struct reiser4_syscall_w_space * ws)
{
	expr_v4_t * e;
	e                  = alloc_new_expr(ws,EXPR_VNODE);
	e->vnode.v         = alloc_vnode(ws,ws->root->vnode);
	e->vnode.v->w      = nullname(ws) ;  /* better if it will point to full pathname for pwd */
	e->vnode.v->ln     = get_lnode(current->fs->pwd->d_inode) ; ???
	e->vnode.v->parent = ws->root->vnode;
	return e;
}



static expr_v4_t *  lookup_word(struct reiser4_syscall_w_space * ws, wrd_t * w)
{
	expr_v4_t * e;
	e             = alloc_new_expr( ws, EXPR_VNODE );
	e->vnode.v    = lookup_vnode_word( ws, w );
	return e;
}


static vnode_t *  lookup_vnode_word(struct reiser4_syscall_w_space * ws, wrd_t * w)
{
	path_walk_name this_path;
	lnode * lnode;
	int error;
	int result=0;
	reiser4_plugin * r4_plugin;
	char * name  ;
	int reiser4_fs;
	struct nameidata nd;
	struct dentry  curent_dentry, * res_de;
	struct inode  * inode;
	vnode_t * vnode;
	vnode_t * rez_vnode;
	vnode_t * last_vnode;

	vnode       = ws->level->wrk_exp->vnode.v;          /* tmp.  this is fist version.  for II we need do "while" throus expression for all vnode */
//	vnode       = getFirsVnodeFromExpr(ws->level->wrk_exp); 
//	while(vnode!=NULL)
//		{
			last_vnode  = NULL;
			rez_vnode   = ws->Head_vnode;
#ifdef 0
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
			reiser4_fs        = 0;
			rez_vnode         = alloc_vnode(ws, last_vnode);
			rez_vnode->w      = w;
			rez_vnode->parent = vnode;
//			vnode=getNextVnodeFromExpr(ws->level->wrk_exp); 
#endif
//		}
// all rezult mast be connected to expression. this change the return type!
	return rez_vnode;
}


/* execute code: walk tree, call plugins and return value */
static expr_v4_t * make_do_it(struct reiser4_syscall_w_space * ws, expr_v4_t * e1 )
{
	return e1;
}

static expr_v4_t * if_then_else(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 , expr_v4_t * e3  )
{
	return e1;
}

static expr_v4_t * if_then(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return e1;
}

static void goto_end(struct reiser4_syscall_w_space * ws)
{
}


/* STRING_CONSTANT to expression */
static expr_v4_t * constToExpr(struct reiser4_syscall_w_space * ws, wrd_t * e1 )
{
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
	return e1; /* for tmp */
}



static inline expr_v4_t * list_async_expression(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2 )
{
	return list_expression( ws, e1 , e2  );
}






#if 0
lookup_sd(struct inode *inode /* inode to look sd for */ ,
	  znode_lock_mode lock_mode /* lock mode */ ,
	  coord_t * coord /* resulting coord */ ,
	  lock_handle * lh /* resulting lock handle */ ,
	  reiser4_key * key /* resulting key */ );


/*	result = lookup_sd_by_key(tree_by_inode(inode), ZNODE_READ_LOCK, &coord, &lh, key);*/

#endif


static expr_v4_t * assign(struct reiser4_syscall_w_space * ws, expr_v4_t * e1, expr_v4_t * e2)
{
	/* while for each vnode in e1*/

	return pump(e1->vnode.v,e2);
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

/*
*/

typedef struct tube tube_t;

struct tube
{
	int type_offset;
	char * offset;
	long len;
	long used;
	expr_v4_t * source;
	/* offset might actually point to sink */
	vnode_t * sink;
/* 	pos_t pos; */
};

static  int source_not_empty(expr_v4_t *source)
{
	return 0;
}

tube_t * get_tube_general(vnode_t *sink, expr_v4_t *source)

{
	tube_t * tube;
	tube = kmalloc(sizeof(struct tube),0)  ;
	if tube==-1 then
		{
		}
	else
		{
			tube->typeoffset = ;
			tube->offset     = 0;
			tube->len        = ;
			tube->used       = ;
			tube->source     = source;
			tube->sink       = sink;
		}
	START_KERNEL_IO;
}

int prep_tube_general(tube_t * tube)
{
	tube->len           = reserv_space_in_sink( tube, get_available_len(tube->source) );
}

int source_to_tube_general(tube_t * tube)
{
	tube->source->fplug->read(tube->offset,tube->len);
}

int tube_to_sink_general(tube_t * tube)
{
	tube->sink->fplug->write(tube->offset,tube->len);
	tube->offset+=tube->len;
}

put_tube(tube_t * tube)
{
	END_KERNEL_IO;
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
static expr_v4_t * pump( vnode_t *sink, expr_v4_t *source )
{
      tube_t * tube;
      int (*prep_tube)(tube_t *);
//      int (*prep_tube)(expr_v4_t *);
      int (*source_to_tube)(tube_t *);
      int (*tube_to_sink)(tube_t *);

      pos_t source_pos;
      pos_t sink_pos;
      

      /* remember to write code for freeing tube, error handling, etc. */
#if 0
      tube = sink->fplug -> get_tube( source, sink);
      prep_tube = sink->fplug->prep_tube (tube);
      source_to_tube = source->fplug->source_to_tube;
      tube_to_sink = sink->fplug->tube_to_sink;
#else
      tube = get_tube_general( source, sink);
      prep_tube = prep_tube_general;
      source_to_tube = source_to_tube_general;
      tube_to_sink = tube_to_sink_general;
#ednif

      while( prep_tube( tube ) ) {
        ret_code = source_to_tube( tube );
        ret_code = tube_to_sink( tube );
      }
      put_tube(tube);
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
