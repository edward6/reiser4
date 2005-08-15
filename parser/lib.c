/*
 * Copyright 2001, 2002, 2003, 2004, 2005 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * functions for parser.y
 */

#include "pars.yacc.h"
#include "lib.h"
#include "pars.cls.h"
#include "lnode.c"
#include <linux/mount.h>
#include <linux/ctype.h>
#include "../coord.h"


static expr_v4_t *
init_root_pwd(struct reiser4_syscall_w_space *, pars_var_t * , char * );

int
lookup_name_sys(struct lnode_reiser4 *parent, wrd_t *w, reiser4_key *key);


#if 0
#define w_printk(e) printk(e)
#else
#define w_printk(e) 
#endif


kmem_cache_t *sys_info_cache;


static void
print_pwd_count(char * mess)
{
	printk ("\n%s rootmnt=%d, root_de=%d,pwdmnt=%d, pwd_de=%d\n",
		mess,
		current->fs->rootmnt->mnt_count,
		current->fs->root->d_count,
		current->fs->pwdmnt->mnt_count,
		current->fs->pwd->d_count);
}



#define reiser4_get( ws, current_fs, mnt, de ) \
{\
	(ws)->nd.mnt =  (current_fs)->(mnt); \
	(ws)->nd.dentry =  (current_fs)->(de) ;\
}

reiser4_inode *reiser4_alloc_info()
{
	reiser4_inode * rez;
	rez = kmem_cache_alloc(sys_info_cache, SLAB_KERNEL);
	return rez;
}


struct reiser4_file *
reiser4_open( lnode ln, int mode)
{
}

size_t
reiser4_write(struct reiser4 *file, char * buf, size_t len, loff_t off)
{
}

size_t
reiser4_read(struct reiser4 *file, char * buf, size_t len, loff_t off)
{
}

void
reiser4_do_truncate(struct reiser4 *file, loff_t off)
{
}

void
reiser4_filp_close(struct reiser4 *file)
{
}



reiser4_inode *reiser4_info_cache_init()
{

	sys_info_cache = kmem_cache_create("info", sizeof(reiser4_inode), 0,
				       SLAB_HWCACHE_ALIGN |
				       SLAB_RECLAIM_ACCOUNT, NULL, NULL);
	if (sys_info_cache == NULL) {
		return RETERR(-ENOMEM);
	}
	return 0;
}




static lnode *
reiser4_get_ln(struct reiser4_syscall_w_space *ws, int type, oid_t oid)
{


	lnode * ln = lget( type, oid );

	lnode_dentry * l_de;


	switch (type) {
	case LNODE_DENTRY:
		l_de = &ln->l_dentry;
		assert("VD",        ws->nd.dentry != NULL );
		assert("VD",           ws->nd.mnt != NULL );
		l_de->mnt    =  ws->nd.mnt;
		l_de->dentry =  ws->nd.dentry;
		break;

	case LNODE_REISER4_INODE:

		ln->l_reiser4.p = reiser4_alloc_info();
		break;
	}
	return ln;
}

#if 0
static lnode_dentry *
reiser4_get_ln(struct reiser4_syscall_w_space *ws,
	       lnode_dentry * l_de)
{
	assert("VD",   l_de != NULL);
	assert("VD",  l_de->h.type == LNODE_DENTRY );
	assert("VD",        ws->nd.dentry != NULL );
	assert("VD",           ws->nd.mnt != NULL );
	//	assert("VD",        l_de->dentry != NULL );
	//	assert("VD",           l_de->mnt != NULL );
	lget( LNODE_DENTRY, oid );
#if 0
	read_lock(&current->fs->lock);
	l_de->mnt    = mntget( ws->nd.mnt );
	l_de->dentry = dget  ( ws->nd.dentry ) ;
	read_unlock(&current->fs->lock);
#else
	l_de->mnt    =  ws->nd.mnt;
	l_de->dentry =  ws->nd.dentry;
#endif
	return l_de;
}
#endif


static void
path4_release(lnode_dentry * l_de )
{
	assert("VD",   l_de != NULL);
	assert("VD",  l_de->h.type == LNODE_DENTRY );
	assert("VD",        l_de->dentry != NULL );
	assert("VD",           l_de->mnt != NULL );
#if 0
	dput( l_de->dentry );
	mntput( l_de->mnt );
#endif
}


#define LEX_XFORM  1001
#define LEXERR2    1002
#define LEX_Ste    1003

#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)
static void yyerror(char * message)
{
	printk("%s\n", message);
}
#else
/* printing errors for parsing */
static void
yyerror( struct reiser4_syscall_w_space *ws  /* work space ptr */,
	 int msgnum  /* message number */, ...)
{
	char errstr[120]={"\nreiser4 parser:"};
	char * s;
	va_list args;
	va_start(args, msgnum);
	switch (msgnum) {
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
			{
				char ss[16];
/*				int s = va_arg(args, int);*/
				sprintf( ss,"%4d ", state);
				strcat( errstr, ss );
			}
			strcat( errstr, " syntax error:" );
			switch(state) {
//		case 4:
//			strcat(errstr," wrong operation");
//			break;
			case 6:
				strcat(errstr," wrong assign operation");
				break;
			case 7:
			case 12:
				strcat(errstr," wrong name");
				break;
			case 27:
				strcat(errstr," wrong logical operation");
				break;
			case 10:
				strcat(errstr," wrong THEN keyword");
				break;
			case 34:
			case 50:
				strcat(errstr," wrong separatop");
				break;
			default:
				strcat(errstr," strange error");
				break;
			}
		}
		break;
	}
	va_end(args);
	printk( "\n%s\n", ws->ws_inline );
	for (s=ws->ws_inline; s<curr_symbol(ws); s++)
		{
			if (*s=='\t' ) {
				printk("\t");
			} else {
				printk(" ");
			}
		}
	printk("^");
	printk(errstr);
	printk("\n");
//	printk("\n%s",curr_symbol(ws));
}
#endif


/* free lists of work space*/
static void
freeList(free_space_t * list /* head of list to be fee */)
{
	free_space_t * curr,* next;
	next = list;
	while (next) {
		curr = next;
		next = curr->free_space_next;
		kfree(curr);
	}
}

static void
pop_all_val(struct reiser4_syscall_w_space * ws ,
	    pars_var_t  * var )
{
	while ( (var->val) != NULL ) {
		pop_var_val_stack( ws, var->val, 0 );
	}
}

/* free work space*/
static int
reiser4_pars_free(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	pars_var_t  * var;
	//	pars_var_value_t * val;
	assert("VD",        ws->ws_level >= 0);
	assert("VD" ,        ws->cur_level->cur_exp != NULL);

	var = ws->Head_pars_var;
	while( var!=NULL ) {
		pop_all_val( ws, var );
		var = var->next;
	}

	free_expr( ws, ws->cur_level->cur_exp );
	free_expr( ws, ws->root_e );
	if ( ws->freeSpHead ) {
		freeList(ws->freeSpHead);
	}
	lnodes_done();
	kmem_cache_destroy(sys_info_cache);

	kfree(ws);
	return 0;
}

static void
print_all_val( pars_var_t  * var )
{
	pars_var_value_t * val;
	val=var->val;
	while ( val != NULL ) {
		val = val->prev;
	}
}

/* free work space*/
static void
print_var_values(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	pars_var_t  * var;
	var = ws->Head_pars_var;
	while( var!=NULL ) {
		print_all_val( var );
		var = var->next;
	}
}

#define INITNEXTFREESPACE(fs)	(fs)->free_space_next = NULL;                                      \
                                (fs)->freeSpaceMax   = (fs)->freeSpaceBase+FREESPACESIZE;         \
			        (fs)->freeSpace      = (fs)->freeSpaceBase

/* allocate work space */
static free_space_t *
free_space_alloc()
{
	free_space_t * fs;
	fs = ( free_space_t * ) kmalloc( sizeof( free_space_t ), GFP_KERNEL ) ;
	assert("VD",fs!=NULL);
	memset( fs , 0, sizeof( free_space_t ));
	INITNEXTFREESPACE(fs);
	return fs;
}

#define GET_FIRST_FREESPHEAD(ws) (ws)->freeSpHead
#define GET_NEXT_FREESPHEAD(curr) (curr)->free_space_next

/* allocate next work space */
static free_space_t *
freeSpaceNextAlloc(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	free_space_t * curr,* next;
	curr=NULL;
	next = GET_FIRST_FREESPHEAD(ws);
	while (next) {
		curr = next;
		next = GET_NEXT_FREESPHEAD(curr);
	}
	next = free_space_alloc();
	if(curr==NULL) 		{
		ws->freeSpHead=next;
	}
	else {
		curr->free_space_next=next;
	}
	next->free_space_next=NULL;
	return next;
}

/* allocate field lenth=len in work space */
static char*
list_alloc(struct reiser4_syscall_w_space * ws /* work space ptr */,
	   int len  /* lenth of structures to be allocated in bytes */)
{
	char * rez;
	if( (ws->freeSpCur->freeSpace+len) > (ws->freeSpCur->freeSpaceMax) ) {
		ws->freeSpCur = freeSpaceNextAlloc(ws);
	}
	rez = ws->freeSpCur->freeSpace;
	ws->freeSpCur->freeSpace += ROUND_UP(len);
	return rez;
}

/* allocate new level of parsing in work space */
static streg_t *
alloc_new_level(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	return ( streg_t *)  list_alloc(ws,sizeof(streg_t));
}

/* allocate structure of new variable of input expression */
static pars_var_t *
alloc_pars_var(struct reiser4_syscall_w_space * ws /* work space ptr */,
	       pars_var_t * last_pars_var /* last of allocated pars_var or NULL if list is empty */)
{
	pars_var_t * pars_var;
	pars_var = (pars_var_t *)list_alloc( ws, sizeof( pars_var_t ) );
	if ( last_pars_var == NULL ) {
		ws->Head_pars_var = pars_var;
	}
	else {
		last_pars_var->next = pars_var;
	}
	pars_var->val  = NULL;
	pars_var->next = NULL;
	return pars_var;
}


static unsigned long atol_ptr(char *str, char ** ret)
{
	unsigned long	val;
	int		base, c;
	char		*s;

	val = 0;
	s = str;
	if ((*s == '0') && (*(s+1) == 'x')) {
		base = 16;
		s += 2;
	} else if (*s == '0') {
		base = 8;
		s++;
	} else {
		base = 10;
	}

	for (; (*s != 0); s++) {
		c = (*s > '9') ? (TOLOWER(*s) - 'a' + 10) : (*s - '0');
		if ((c < 0) || (c >= base)) {
			break;
		}
		val = (val * base) + c;
	}
	*ret = s;
	return(val);
}


/* free lnodes used in expression */
static int
free_expr( struct reiser4_syscall_w_space * ws,
	   expr_v4_t * expr)
{
	expr_list_t * tmp;
	int ret = 0;
	assert("VD", expr!=NULL);
	switch (expr->h.type) {
	case EXPR_WRD:
		break;
	case EXPR_RANGE:
		ret = free_expr( ws, expr->rng.host);
		kfree(expr);
		break;
	case EXPR_PARS_VAR:
		pop_var_val_stack( ws, expr->pars_var.v->val, 1 );
		break;
	case EXPR_LIST:
		tmp=&expr->list;
		while (tmp) {
			assert("VD", tmp->h.type==EXPR_LIST);
			ret |= free_expr( ws, tmp->source );
			tmp = tmp->next;
		}
		break;
	case EXPR_ASSIGN:
		ret = pop_var_val_stack( ws, expr->assgn.target->val, 1 );
		ret |= free_expr( ws, expr->assgn.source );
		break;
	case EXPR_LNODE:
		assert("VD", expr->lnode.lnode != NULL );
		if ( expr->lnode.lnode->h.type == LNODE_DENTRY ) {
			path4_release( &(expr->lnode.lnode->l_dentry) );
		}
		lput( expr->lnode.lnode ); 
		break;
	case EXPR_FLOW:
		break;
	case EXPR_OP2:
		ret  = free_expr( ws, expr->op2.op_r );
		ret |= free_expr( ws, expr->op2.op_l );
		break;
	case EXPR_OP:
		ret = free_expr( ws, expr->op.op );
		break;
	}
	return ret;
}


//ln->inode.inode->i_op->lookup(struct inode *,struct dentry *);
//current->fs->pwd->d_inode->i_op->lookup(struct inode *,struct dentry *);

#if 0
/* alloca the space for lnode */
static lnode *
alloc_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */ )
{
	lnode * ln;
	ln = ( lnode * ) kmalloc( sizeof( lnode ), GFP_KERNEL);
	assert("VD", ln != NULL );
	memset( ln , 0, sizeof( lnode ));
	return ln;
}
#endif

/* make lnode_dentry from inode for root and pwd */
static lnode *
get_lnode(struct reiser4_syscall_w_space * ws , int type )
{
	lnode * ln;
#if 0                      /*def NOT_YET*/
	//	reiser4_key key, * k_rez,* l_rez;
	if ( is_reiser4_inode( ws->nd.dentry->inode ) ) {

		k_rez             = build_sd_key( ws->nd.dentry->inode, &key);
		ln                = lget(  LNODE_REISER4_INODE, get_inode_oid( ws->nd.dentry->inode) );
		//			ln->lw.lw_sb = ws->nd.dentry->inode->isb;
		ln->reiser4_inode.inode = /*????*/  ws->nd.dentry->inode->isb;
		ln->reiser4_inode.inode = /*????*/  ws->nd.dentry->inode->isb;
		PTRACE( ws, "r4: lnode=%p", ln );
	}
	else
#endif
		{
			ln = lget( type, get_inode_oid( ws->nd.dentry->d_inode) );
			
		}
	return ln;
}

/*  allocate work space, initialize work space, tables, take root inode and PWD inode */
static struct reiser4_syscall_w_space *
reiser4_pars_init(void)
{
	struct reiser4_syscall_w_space * ws;

	lnodes_init(); /*?????*/
	reiser4_info_cache_init();

	/* allocate work space for parser working variables, attached to this call */
	ws = kmalloc( sizeof( struct reiser4_syscall_w_space ), GFP_KERNEL );
	assert("VD", ws != NULL);
	memset( ws, 0, sizeof( struct reiser4_syscall_w_space ));

#if defined(CONFIG_REISER4_FS_SYSCALL_YACC)
	ws->ws_yystacksize = MAXLEVELCO; /* must be 500 by default */
	ws->ws_yymaxdepth  = MAXLEVELCO; /* must be 500 by default */
#endif
	                                                    /* allocate first part of working tables
							       and initialise headers */
	ws->freeSpHead          = free_space_alloc();
	ws->freeSpCur           = ws->freeSpHead;
	ws->wrdHead             = NULL;
	ws->cur_level           = alloc_new_level(ws);
	//	ws->root_e              = init_root(ws);
	//	ws->cur_level->cur_exp  = init_pwd(ws);


	ws->root_e              = init_root_pwd( ws, NULL, "/" );
	ws->cur_level->cur_exp  = init_root_pwd( ws, ws->root_e->pars_var.v, current->fs->pwd->d_name.name );



	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
	ws->cur_level->prev     = NULL;
	ws->cur_level->next     = NULL;
	ws->cur_level->level    = 0;
	ws->cur_level->stype    = 0;
	return ws;
}

#if 0
static expr_v4_t *
named_level_down(struct reiser4_syscall_w_space *ws /* work space ptr */,
		 expr_v4_t * e /* name for expression  */,
		 expr_v4_t * e1,
		 long type /* type of level we going to */)
{
	struct qstr *u;
	char * ret
	static int push_var_val_stack( ws, struct pars_var * var, long type )

	rezult->u.data  = kmalloc( SIZEFOR_ASSIGN_RESULT, GFP_KERNEL ) ;
	sprintf( rezult->u.data, "%d", ret_code );

	level_down( ws, , type2 );
	return e1;
}


/* level up of parsing level */
static void
level_up_named(struct reiser4_syscall_w_space *ws /* work space ptr */,
	       expr_v4_t * e1 /* name for expression  */,
	       long type /* type of level we going to */)
{
	pars_var_t * rezult;

	assert("VD", type == CD_BEGIN );

	rezult =  e1->pars_var.v;
	switch ( e1->pars_var.v->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		break;
	case VAR_TMP:
		break;
	}

	/* make name for w in this level. ????????
	   not yet worked */


	rezult =  lookup_pars_var_word( ws , sink, make_new_word(ws, ASSIGN_RESULT ), VAR_TMP);
	rezult->u.data  = kmalloc( SIZEFOR_ASSIGN_RESULT, GFP_KERNEL ) ;
	sprintf( rezult->u.data, "%d", ret_code );
	
?????

	level_up( ws, type );
}

#endif


static expr_v4_t *
target_name( expr_v4_t *assoc_name,
	     expr_v4_t *target )
{
	target->pars_var.v->val->associated = assoc_name->pars_var.v;
	return target;
}

/* level up of parsing level */
static void
level_up(struct reiser4_syscall_w_space *ws /* work space ptr */,
	 long type /* type of level we going to */)
{
	if (ws->cur_level->next==NULL) {
		ws->cur_level->next        = alloc_new_level(ws);
		ws->cur_level->next->next  = NULL;
		ws->cur_level->next->prev  = ws->cur_level;
		ws->cur_level->next->level = ws->cur_level->level+1;
	}
	ws->cur_level           = ws->cur_level->next;
	ws->cur_level->stype    = type;
	ws->cur_level->cur_exp  = ws->cur_level->prev->wrk_exp;                  /* current pwd for new level */
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for new level */
}


/* level down of parsing level */
static  void
level_down(struct reiser4_syscall_w_space * ws /* work space ptr */,
	   long type1 /* type of level that was up( for checking) */,
	   long type2 /* type of level that is down(for checking)*/)
{
	pars_var_value_t * ret,*next;
	assert("VD", type1 == type2 );
	assert("VD", type1 == ws->cur_level->stype );
	assert("VD", ws->cur_level->prev != NULL);
	ret = ws->cur_level->val_level;
	while( ret != NULL )
		{
			next = ret->next_level;
			assert("VD", ret == ret->host->val);
			pop_var_val_stack( ws, ret, 1 );
			ret = next;
		}
	free_expr( ws, ws->cur_level->prev->wrk_exp );
	ws->cur_level->prev->wrk_exp = ws->cur_level->wrk_exp ;           /* current wrk for prev level */
	ws->cur_level                = ws->cur_level->prev;
}

/* copy name from param to free space,*/
static  wrd_t *
make_new_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
	      char *txt /* string to put in name table */)
{
	ws->tmpWrdEnd = ws->freeSpCur->freeSpace;
	strcat( ws->tmpWrdEnd, txt );
	ws->tmpWrdEnd += strlen(txt) ;
	*ws->tmpWrdEnd++ = 0;
	return _wrd_inittab( ws );
}


/* move_selected_word - copy term from input bufer to free space.
 * if it need more, move freeSpace to the end.
 * otherwise next term will owerwrite it
 *  freeSpace is a kernel space no need make getnam().
 * exclude is for special for string: store without ''
 */
static void
move_selected_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
		   int exclude  /* TRUE - for storing string without first and last symbols
				   FALS - for storing names */,
		   int press )
{
	int i;
	/*	char * s= ws->ws_pline;*/
	if (exclude) {
		ws->yytext++;
	}
	for( ws->tmpWrdEnd = ws->freeSpCur->freeSpace; ws->yytext < curr_symbol(ws); ) {
		i=0;
#if 0
		if ( lcls == Ste ) {
			while( *ws->yytext == '\"' ) {
				ws->yytext++;
				i++;
			}
			while ( ws->yytext >  curr_symbol(ws) ) {
				i--;
				ws->yytext--;
			}
		}
		if ( i ) for ( i/=2; i; i-- )      *ws->tmpWrdEnd++='\"';    /*   in source text for each "" - result will "   */
#endif
		/*         \????????   */ 
		if ( press && *ws->yytext == '\\' ) {
#if 0
			char * ret;
			long val, i;
			int j;
			val = atol_ptr(++ws->yytext, &ret);
			ws->yytext = ret;
			ret = (char *)&val;
			i = 0;

			for (; ret <= (char *)&val + sizeof(long)) {
				if (!i && *ret) i++ ;
				if (i) {
				}
			}

#else
			int tmpI;
			ws->yytext++;
			switch ( tolower( (int)*(ws->yytext) ) ) {
			case 'x':                       /*  \x01..9a..e  */
				i = 0;
				tmpI = 1;
				while( tmpI) {
					if (isdigit( (int)*(ws->yytext) ) ) {
						i = (i << 4) + ( *ws->yytext++ - '0' );
					}
					else if( tolower( (int) *(ws->yytext) ) >= 'a' && tolower( (int)*(ws->yytext) ) <= 'e' ) {
						i = (i << 4) + ( *ws->yytext++ - 'a' + 10 );
						}
					else {
						if ( tmpI & 1 ) {
#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)
							yyerror("x format has odd number of symbols");
#else
							yyerror( ws, LEX_XFORM ); /* x format has odd number of symbols */
#endif
						}
						tmpI = 0;
					}
					if ( tmpI && !( tmpI++ & 1 ) ) {
						*ws->tmpWrdEnd++ = (unsigned char) i;
						i = 0;
					}
				}
				break;
			}
#endif
		}
		else *ws->tmpWrdEnd++ = *ws->yytext++;
		if( ws->tmpWrdEnd > (ws->freeSpCur->freeSpaceMax - sizeof(wrd_t)) ) {
			free_space_t * tmp;
			int i;
			assert ("VD",ws->freeSpCur->freeSpace > ws->freeSpCur->freeSpaceBase);
			/* we can reallocate new space and copy all
			   symbols of current token inside it */
			tmp=ws->freeSpCur;
			ws->freeSpCur = freeSpaceNextAlloc(ws);
			assert ("VD", ws->freeSpCur !=NULL);
			i = ws->tmpWrdEnd - tmp->freeSpace;
			memmove( ws->freeSpCur->freeSpace, tmp->freeSpace, i );
			ws->tmpWrdEnd = ws->freeSpCur->freeSpace + i;
		}
	}
	if (exclude) {
		ws->tmpWrdEnd--;
	}
	*ws->tmpWrdEnd++ = '\0';
}


/* compare parsed word with keywords*/
static int
b_check_word(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	int i, j, l;
	j=sizeof(pars_key)/(sizeof(char*)+sizeof(int))-1;
	l=0;
	while( ( j - l ) >= 0 ) {
		i  =  ( j + l /*+ 1*/ ) >> 1;
		switch( strcmp( pars_key[i].wrd, ws->freeSpCur->freeSpace ) ) {
		case  0:
			return( pars_key[i].class );
			break;
		case  1: j = i - 1;               break;
		default: l = i + 1;               break;
		}
	}
	return(0);
}


/* comparing parsed word with already stored words, if not compared, storing it */
static wrd_t *
_wrd_inittab(struct reiser4_syscall_w_space * ws )
{
	wrd_t * cur_wrd;
	wrd_t * new_wrd;
	int len;

	new_wrd =  ws->wrdHead;
	len = (char *)ws->tmpWrdEnd - (char *)ws->freeSpCur->freeSpace - 1 ;
	cur_wrd = NULL;
	while ( !( new_wrd == NULL ) ) {
		cur_wrd = new_wrd;
		if ( cur_wrd->u.len == len ) {
			if( !memcmp( cur_wrd->u.name, ws->freeSpCur->freeSpace, cur_wrd->u.len ) ) {
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
	if (cur_wrd==NULL) {
		ws->wrdHead   = new_wrd;
	}
	else {
		cur_wrd->next = new_wrd;
	}
	return new_wrd;
}

/* lexical analisator for yacc automat */
static int
reiser4_lex(YYSTYPE *par_yylval,
	    YYLTYPE * par_yylloc,
	    struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	char term, n, i = 0;
	int ret = 0;
	char lcls;
//	char * s ;

//	s = curr_symbol(ws);              /* first symbol or Last readed symbol of the previous token parsing */
	if ( *curr_symbol(ws) == 0 ) return  0;        /* end of string is EOF */

	while(ncl[(int)*curr_symbol(ws)]==Blk) {
		next_symbol(ws);
		if ( *curr_symbol(ws) == 0 ) return  0;  /* end of string is EOF */
	}


	lcls    =       ncl[(int)*curr_symbol(ws)];
	ws->yytext  = curr_symbol(ws);
	term = 1;
	while( term ) {
		n=lcls;
		while (  n > 0   ) {
			next_symbol(ws);
			lcls=n;
			n = lexcls[ (int)lcls ].c[ (int)i=ncl[ (int)*curr_symbol(ws) ] ];
		}
		if ( n == OK ) {
			term=0;
		}
		else {
#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)
			yyerror("internal lex table error");
#else
			yyerror ( ws, LEXERR2, (lcls-1)* 20+i );
#endif
			return(0);
		}
	}
	switch (lcls) {
	case Blk:
#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)
		yyerror("wrong lexem");
#else
		yyerror(ws,LEX_Ste);
#endif
		break;
	case Wrd:
	case W_e: /*`......"*/
		if ( lcls == W_e ) {
			move_selected_word( ws, lexcls[(int) lcls ].c[0], 0 );
		}
		else {
			move_selected_word( ws, lexcls[(int) lcls ].c[0], 1 );
		}
		                                                    /* if ret>0 this is keyword */
		if ( !(ret = b_check_word(ws)) ) {                          /*  this is not keyword. tray check in worgs. ret = Wrd */
			ret=lexcls[(int) lcls ].term;
			ws->ws_yylval.wrd = _wrd_inittab(ws);
		}
		break;
	case Int:
	case Ptr:
	case Pru:
	case Ste: 
		move_selected_word( ws, lexcls[(int) lcls ].c[0], 1 );
		ret=lexcls[(int) lcls ].term;
		ws->ws_yylval.wrd = _wrd_inittab(ws);
		break;
		/*
		  move_selected_word( ws, lexcls[ lcls ].c[0], 1 );
		  ret=lexcls[ lcls ].term;
		  ws->ws_yyval.w = _wrd_inittab(ws);
		  break;
		*/
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
	case App:/*<<-*/ /*???*/
	case Lnk:/*->*/
	case Pls:/*+*/
	case Nam:/*<=*/
		ret=lexcls[(int) lcls ].term;
		break;
	case Lpr:
	case Rpr:
		ws->ws_yylval.charType = CD_BEGIN ;
		ret=lexcls[(int) lcls ].term;
		break;
	case Lsq:
	case Rsq:
		ws->ws_yylval.charType = UNORDERED ;
		ret=lexcls[(int) lcls ].term;
		break;
	case Lfl:
	case Rfl:
		ws->ws_yylval.charType = ASYN_BEGIN ;
		ret=lexcls[(int) lcls ].term;
		break;
	default :                                /*  others  */
		ret=*ws->yytext;
		break;
	}
	return ret;
}



/*==========================================================*/

/* allocate new expression @type */
static expr_v4_t *
alloc_new_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
	       int type /* type of new expression */)
{
	expr_v4_t * e;
	e         = ( expr_v4_t *)  list_alloc( ws, sizeof(expr_v4_t));
	e->h.type = type;
	return e;
}

/* store NULL name in word table */
static wrd_t *
nullname(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	return make_new_word(ws,"");
}

#if 0

/* initialize node  for PWD lnode */
static expr_v4_t *
init_pseudo_name(struct reiser4_syscall_w_space * ws /* work space ptr */,
		 char *name /* name of pseudo */)
{
	expr_v4_t * e;
	e                     = alloc_new_expr(ws,EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var(ws,ws->root_e->pars_var.v);
	e->pars_var.v->w      = make_new_word(ws, name);
	e->pars_var.v->parent = ws->root_e->pars_var.v;

	current->total_link_count = 0;
	push_var_val_stack( ws, e->pars_var.v, VAR_LNODE );
	e->pars_var.v->val->u.ln = get_lnode( ws );
	return e;
}

static expr_v4_t *
pars_lookup(struct reiser4_syscall_w_space * ws,
	    expr_v4_t * e1,
	    expr_v4_t * e2)
{
	not ready;
	pars_var_t * rez_pars_var;
	pars_var_t * this_l;
	this_l = getFirstPars_Var(e1);
	while(this_l != NULL ) {
	}
	assert("VD",rez_pars_var->ln!=NULL);
	memcpy( &curent_dentry.d_name   , w, sizeof(struct qstr));<---------------
		if( ( rez_pars_var->ln = pars_var->ln->d_inode->i_op->lookup( pars_var->ln->d_inode, &curent_dentry) ) == NULL ) {
			/* lnode not exist: we will not need create it. this is error*/
		}
}

#endif


/* initialize node  for ROOT and PWD lnode  */
static expr_v4_t *
init_root_pwd(struct reiser4_syscall_w_space * ws /* work space ptr */,
	      pars_var_t * parent,
	      char * name)
{
	expr_v4_t * e;
	e                     = alloc_new_expr( ws, EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var( ws, parent);
	e->pars_var.v->parent = parent; /*????*/
	e->pars_var.v->w          = make_new_word( ws, name ) ;
	current->total_link_count = 0;

	if ( push_var_val_stack( ws, e->pars_var.v, VAR_LNODE ) ) {
		printk("VD-init_pwd: push_var_val_stack error\n");
	}
	else {

		if ( parent == NULL ) {
			//	walk_init_root( "/", (&ws->nd));   /* from namei.c walk_init_root */
			read_lock(&current->fs->lock);
			ws->nd.mnt    = mntget( current->fs->rootmnt); 
			ws->nd.dentry = dget( current->fs->root);
			read_unlock(&current->fs->lock);
		}
		else {
			read_lock(&current->fs->lock);
			ws->nd.mnt    = mntget( current->fs->pwdmnt); 
			ws->nd.dentry = dget( current->fs->pwd);
			read_unlock(&current->fs->lock);
		}
		e->pars_var.v->val->u.ln = get_lnode( ws, LNODE_DENTRY );
		e->pars_var.v->val->u.ln->l_dentry.mnt    = ws->nd.mnt;
		e->pars_var.v->val->u.ln->l_dentry.dentry = ws->nd.dentry;

	}
	return e;
}



#if 0
/* initialize node  for ROOT and PWD lnode  */
static expr_v4_t *
__init_root_pwd(struct reiser4_syscall_w_space * ws /* work space ptr */,
	      pars_var_t * parent,
	      char * name)
{
	expr_v4_t * e;
	e                     = alloc_new_expr( ws, EXPR_PARS_VAR);
	e->pars_var.v         = alloc_pars_var( ws, parent);
	e->pars_var.v->parent = parent;
	if ( parent == NULL ) {
		//	walk_init_root( "/", (&ws->nd));   /* from namei.c walk_init_root */
		read_lock(&current->fs->lock);
		ws->nd.mnt    = mntget( current->fs->rootmnt); 
		ws->nd.dentry = dget( current->fs->root);
		read_unlock(&current->fs->lock);
		print_pwd_count("dget root ");
	}
	else {
		//	path_lookup(".",,&(ws->nd));   /* from namei.c path_lookup */
		read_lock(&current->fs->lock);
		ws->nd.mnt    = mntget(current->fs->pwdmnt);
		ws->nd.dentry = dget(current->fs->pwd);
		read_unlock(&current->fs->lock);
		print_pwd_count("dget pwd ");
	}
	e->pars_var.v->w          = make_new_word( ws, name ) ;
	current->total_link_count = 0;
	if ( push_var_val_stack( ws, e->pars_var.v, VAR_LNODE ) ) {
		printk("VD-init_pwd: push_var_val_stack error\n");
	}
	else {
		e->pars_var.v->val->u.ln                  = get_lnode( ws, LNODE_DENTRY  );
		e->pars_var.v->val->u.ln->l_dentry.mnt    = ws->nd.mnt;
		e->pars_var.v->val->u.ln->l_dentry.dentry = ws->nd.dentry;
	}
	return e;
}
#endif





/*    Object_Name : begin_from name                 %prec ROOT       { $$ = pars_expr( ws, $1, $2 ) ; }
                  | Object_Name SLASH name                           { $$ = pars_expr( ws, $1, $3 ) ; }  */
static expr_v4_t *
pars_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
	  expr_v4_t * e1 /* first expression ( not yet used)*/,
	  expr_v4_t * e2 /* second expression*/)
{
	ws->cur_level->wrk_exp = e2;
	return e2;
}

/* not yet */
static pars_var_t *
getFirstPars_VarFromExpr(struct reiser4_syscall_w_space * ws )
{
	pars_var_t * ret = 0;
	expr_v4_t * e = ws->cur_level->wrk_exp;
	switch (e->h.type) {
	case EXPR_PARS_VAR:
		ret = e->pars_var.v;
		break;
		//	default:

	}
	return ret;
}

lnode *
set_lnode_reiser4_inode(struct reiser4_syscall_w_space *ws, struct super_block *sb, oid_t oid)
{
	lnode *ln;
	ln = reiser4_get_ln( ws, LNODE_REISER4_INODE, oid );
	ln->l_reiser4.l_sb = sb;
	ln->l_reiser4.p = reiser4_alloc_info(ws);
	return ln;
}

static int
is_mount_point_reiser4(struct lnode_reiser4 * ln)
{
	return 0;
}

/* seach @parent/w in internal table. if found return it, else @parent->lookup(@w) */
static pars_var_t *
lookup_pars_var_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
		     pars_var_t * parent /* parent for w       */,
		     wrd_t * w        /* to lookup for word */,
		     int type)
{
	pars_var_t * rez_pars_var;
	struct dentry *de;
	struct inode * inode;
	//	int rez;
	reiser4_context *context;
	reiser4_key key;
//	coord coord;
	lock_handle lh;
	struct super_block * sb;

	pars_var_t * last_pars_var;
	last_pars_var  = NULL;
	rez_pars_var   = ws->Head_pars_var;
	while ( rez_pars_var != NULL ) {
		if( rez_pars_var->parent == parent &&
		    rez_pars_var->w      == w ) {
			rez_pars_var->val->count++;
			return rez_pars_var;
		}
		last_pars_var = rez_pars_var;
		rez_pars_var  = rez_pars_var->next;
	}
	rez_pars_var         = alloc_pars_var(ws, last_pars_var);
	rez_pars_var->w      = w;
	rez_pars_var->parent = parent;

	/* for tmp names only */
	if ((type == VAR_TMP)||(parent->val->vtype == VAR_TMP)) {
		push_var_val_stack( ws, rez_pars_var, VAR_TMP );
		rez_pars_var->val->u.data = NULL;
		return rez_pars_var;
	}

	if (parent->val->vtype == VAR_LNODE) {
		switch (parent->val->u.ln->h.type) {
		case LNODE_DENTRY:
			de = parent->val->u.ln->l_dentry.dentry;
			ws->nd.dentry = de;
			ws->nd.mnt    = parent->val->u.ln->l_dentry.mnt;
			ws->nd.flags  = LOOKUP_NOALT ;
			if ( link_path_walk( w->u.name, &(ws->nd) ) ) /* namei.c */ {
				printk("lookup error: w->u.name=%s\n", w->u.name);
				push_var_val_stack( ws, rez_pars_var, VAR_TMP );
				rez_pars_var->val->u.data = NULL;
			}
			else {
				push_var_val_stack( ws, rez_pars_var, VAR_LNODE );
				oid_t oid = get_inode_oid( ws->nd.dentry->d_inode);
				if (is_reiser4_inode(de->d_inode)) {
					rez_pars_var->val->u.ln = set_lnode_reiser4_inode(ws, de->d_inode->i_sb, oid);
				}
				else {
					rez_pars_var->val->u.ln = reiser4_get_ln( ws, LNODE_DENTRY, oid );
				}
			}
			break;
		case LNODE_INODE:  /* don't use it ! */
			de = d_alloc_anon(parent->val->u.ln->l_inode.inode);
			break;
		case LNODE_REISER4_INODE:

			sb = parent->val->u.ln->l_reiser4.l_sb;
//			sb = parent->val->u.ln->l_reiser4->l_sb;
			if (is_mount_point_reiser4(&parent->val->u.ln->l_reiser4)) {
			}
			else {
				if (lookup_name_sys(&parent->val->u.ln->l_reiser4, w, &key)) {
					push_var_val_stack( ws, rez_pars_var, VAR_LNODE );
					rez_pars_var->val->u.ln = set_lnode_reiser4_inode(ws, de->d_inode->i_sb, get_key_objectid(&key));
				}
			}














#if 0                   /*   NOT_YET  ???? */

//			ln                = lget( LNODE_DENTRY, get_key_objectid(&key ) );

			{
				__u64(*hash) (const unsigned char *name, int len);
				__u64(*fibre) (const char *name, int len);   

			hash = get_plugin_by_coord(sd_coord)->hash;
			fibre = get_plugin_by_coord(sd_coord)->fibre;
			complete_entry_key_by_plugin(hash, fibre, w->u.name, w->u.len, &key);

			result = coord_by_key(get_super_private(sb)->tree,
					      &key,
					      &coord,
					      &lh,
					      ZNODE_READ_LOCK,
					      FIND_EXACT,
					      LEAF_LEVEL,
					      LEAF_LEVEL,
					      CBK_UNIQUE,
					      0);

			__u64(*hash) (const unsigned char *name, int len);
			__u64(*fibre) (const char *name, int len);
			reiser4_context *context;
			
			struct super_block * sb = parent->val->u.ln->l_dentry.dentry->d_inode->i_sb;
			reiser4_key key;
			
			context = init_context(sb);
			hash=kkk????;
			fibre=lll????;
			result = perm_chk(parent->val->u.ln->l_dentry.dentry->d_inode,
					  lookup, parent->val->u.ln->l_dentry.dentry->d_inode, ??dentry);
			
			complete_entry_key_by_plugin(hash, fibre,  w->u.name, w->u.len, &key);
			result = coord_by_key(get_super_private(parent->val->ln->lw.lw_sb)->tree,
					      parent->val->ln->lw.key,
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
			if (result != 0) {
				lw_key_warning(parent->val->ln->lw.key, result);
			}
			else {
				switch(item_type_by_coord(coord)) {
				case STAT_DATA_ITEM_TYPE:
				case DIR_ENTRY_ITEM_TYPE:
					iplug = item_plugin_by_coord(coord);
					if (iplug->b.lookup != NULL) {
						iplug->b.lookup();   /*????*/
					}
					
				case INTERNAL_ITEM_TYPE:
					w_printk("VD-item type is INTERNAL\n");
				case ORDINARY_FILE_METADATA_TYPE:
				case OTHER_ITEM_TYPE:
					w_printk("VD-item type is OTHER\n");
				}
			}
			/*??  lookup_sd     find_item_obsolete */
#endif
		case LNODE_LW: /* not yet work */
#if 0
			key_by_name();
			coord_by_key();
#endif
			break;
		case LNODE_COORD:  /* not use it ! */
			break;
//		case LNODE_PSEUDO:
//			PTRACE(ws, "parent pseudo=%p",parent->val->u.ln->l_pseudo);
//			break;

		}
	}

	return rez_pars_var;
}


/* search pars_var for @w */
static expr_v4_t *
lookup_word(struct reiser4_syscall_w_space * ws /* work space ptr */,
	    wrd_t * w /* word to search for */)
{
	expr_v4_t * e;
	pars_var_t * cur_pars_var;
#if 1           /* tmp.  this is fist version.  for II we need do "while" throus expression for all pars_var */
	cur_pars_var        = ws->cur_level->wrk_exp->pars_var.v;
#else
	cur_pars_var       = getFirstPars_VarFromExpr(ws);
	while(cur_pars_var!=NULL) {
#endif
		e                = alloc_new_expr( ws, EXPR_PARS_VAR );
		e->pars_var.v    = lookup_pars_var_word( ws, cur_pars_var, w , VAR_LNODE);
#if 0
		cur_pars_var=getNextPars_VarFromExpr(ws);
	}
	all rezult mast be connected to expression.
#endif
	return e;
}

/* set work path in level to current in level */
static inline expr_v4_t *
pars_lookup_curr(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
	ws->cur_level->wrk_exp  = ws->cur_level->cur_exp;                        /* current wrk for pwd of level */
	return ws->cur_level->wrk_exp;
}

/* set work path in level to root */
static inline expr_v4_t *
pars_lookup_root(struct reiser4_syscall_w_space * ws)
{
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return ws->cur_level->wrk_exp;
}

static inline expr_v4_t *
pars_lookup_process(struct reiser4_syscall_w_space * ws,
		    val_range_t * val)
{
	ws->cur_level->wrk_exp  = ws->root_e;                                    /* set current to root */
	return ws->cur_level->wrk_exp;
}



#if 0
/*?????*/

node_plugin_by_node(coord->node)->lookup(coord->node, key, FIND_MAX_NOT_MORE_THAN, &twin);
item_type_by_coord(coord)

/*
 * try to look up built-in pseudo file by its name.
 */
reiser4_internal int
lookup_pseudo_file(reiser4_inode *parent /* reiser4 inode of directory to lookup for name in */,
		   wrd_t *w             /* name to look for */,
		   reiser4_key *key     /* place to store key */)
     //		   struct dentry * dentry)
{
	reiser4_plugin *plugin;
	const char     *name;
	struct inode   *pseudo;
	int             result;






	assert("nikita-2999", parent != NULL);
	assert("nikita-3000", dentry != NULL);

	/* if pseudo files are disabled for this file system bail out */
	if (reiser4_is_set(parent->i_sb, REISER4_NO_PSEUDO))
		return RETERR(-ENOENT);

	name = dentry->d_name.name;
	pseudo = ERR_PTR(-ENOENT);
	/* scan all pseudo file plugins and check each */
	for_all_plugins(REISER4_PSEUDO_PLUGIN_TYPE, plugin) {
		pseudo_plugin *pplug;

		pplug = &plugin->pseudo;
		if (pplug->try != NULL && pplug->try(pplug, parent, name)) {
			pseudo = add_pseudo(parent, pplug, dentry);
			break;
		}
	}
	if (!IS_ERR(pseudo))
		result = 0;
	else
		result = PTR_ERR(pseudo);
	return result;
}

#endif

static int
lookup_pars_var_lnode(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      pars_var_t * parent /* parent for w       */,
		      wrd_t * w        /* to lookup for word */)
{
	//	struct dentry  * de, * de_rez;
	int rez;
	//	pars_var_t * rez_pars_var;
	//	reiser4_key key,* k_rez;
	//	coord_t coord;
	//	lock_handle lh;
	//	item_plugin *iplug;

//		case EXPR_PARS_VAR:
//			/* not yet */
//			ws->nd.dentry=parent->ln->dentry.dentry;
//			de_rez = link_path_walk( w->u.name, &(ws->nd) ); /* namei.c */
//			break;

	
	return rez;

}




/* if_then_else procedure */
static expr_v4_t *
if_then_else(struct reiser4_syscall_w_space * ws /* work space ptr */,
	     expr_v4_t * e1 /* expression of condition */,
	     expr_v4_t * e2 /* expression of then */,
	     expr_v4_t * e3 /* expression of else */ )
{
	return e1;
}

/* not yet */
static expr_v4_t *
if_then(struct reiser4_syscall_w_space * ws /* work space ptr */,
	expr_v4_t * e1 /**/,
	expr_v4_t * e2 /**/ )
{
	return e1;
}

/* not yet */
static void
goto_end(struct reiser4_syscall_w_space * ws /* work space ptr */)
{
}


/* STRING_CONSTANT to expression */
static expr_v4_t *
const_to_expr(struct reiser4_syscall_w_space * ws /* work space ptr */,
	      wrd_t * e1 /* constant for convert to expression */)
{
	expr_v4_t * new_expr = alloc_new_expr(ws, EXPR_WRD );
	new_expr->wd.s = e1;
	return new_expr;
}

/* allocate EXPR_OP2  */
static expr_v4_t *
allocate_expr_op2(struct reiser4_syscall_w_space * ws /* work space ptr */,
		  expr_v4_t * e1 /* first expr */,
		  expr_v4_t * e2 /* second expr */,
		  int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr( ws, EXPR_OP2 );
	assert("VD", ret!=NULL);
	ret->h.exp_code = op;
	ret->op2.op_l = e1;
	ret->op2.op_r = e2;
	return ret;
}

/* allocate EXPR_OP  */
static expr_v4_t *
allocate_expr_op(struct reiser4_syscall_w_space * ws /* work space ptr */,
		 expr_v4_t * e1 /* first expr */,
		 int  op        /* expression code */)
{
	expr_v4_t * ret;
	ret = alloc_new_expr(ws, EXPR_OP );
	assert("VD", ret!=NULL);
	ret->h.exp_code = op;
	ret->op.op = e1;
	return ret;
}


/* concatenate expressions */
static expr_v4_t *
concat_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		  expr_v4_t * e1 /* first expr of concating */,
		  expr_v4_t * e2 /* second expr of concating */)
{
	return allocate_expr_op2( ws, e1, e2, CONCAT );
}


/* compare expressions */
static expr_v4_t *
compare_EQ_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_EQ );
}


static expr_v4_t *
compare_NE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_NE );
}


static expr_v4_t *
compare_LE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LE );
}


static expr_v4_t *
compare_GE_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GE );
}


static expr_v4_t *
compare_LT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_LT );
}


static expr_v4_t *
compare_GT_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_GT );
}


static expr_v4_t *
compare_OR_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		      expr_v4_t * e1 /* first expr of comparing */,
		      expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_OR );
}


static expr_v4_t *
compare_AND_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		       expr_v4_t * e1 /* first expr of comparing */,
		       expr_v4_t * e2 /* second expr of comparing */)
{
	return allocate_expr_op2( ws, e1, e2, COMPARE_AND );
}


static expr_v4_t *
not_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
	       expr_v4_t * e1 /* first expr of comparing */)
{
	return allocate_expr_op( ws, e1, COMPARE_NOT );
}


/**/
static expr_v4_t *
check_exist(struct reiser4_syscall_w_space * ws /* work space ptr */,
	    expr_v4_t * e1 /* first expr of comparing */)
{
	return e1;
}

/* union lists */
static expr_v4_t *
union_lists(struct reiser4_syscall_w_space * ws /* work space ptr */,
	    expr_v4_t * e1 /* first expr of connecting */,
	    expr_v4_t * e2 /* second expr of connecting */)
{
	expr_list_t *next, *last;
	assert("VD", e1->h.type == EXPR_LIST);

	last = (expr_list_t *)e1;
	next = e1->list.next;
                   /* find last in list */
	while ( next ) {
		last = next;
		next = next->next;
	}
	if ( e2->h.type == EXPR_LIST ) {                       /* connect 2 lists */
		last->next = (expr_list_t *) e2;
	}
	else {                      /* add 2 EXPR to 1 list */
		next = (expr_list_t *) alloc_new_expr(ws, EXPR_LIST );
		assert("VD", next!=NULL);
		next->next = NULL;
		next->source = e2;
		last->next = next;
	}
	return e1;
}


/*  make list from expressions */
static expr_v4_t *
list_expression(struct reiser4_syscall_w_space * ws /* work space ptr */,
		expr_v4_t * e1 /* first expr of list */,
		expr_v4_t * e2 /* second expr of list */)
{
	expr_v4_t * ret;

	if ( e1->h.type == EXPR_LIST ) {
		ret = union_lists( ws, e1, e2);
	}
	else {
		
		if ( e2->h.type == EXPR_LIST ) {
			ret = union_lists( ws, e2, e1);
		}
		else {				
			ret = alloc_new_expr(ws, EXPR_LIST );
			assert("VD", ret!=NULL);
			ret->list.source = e1;
			ret->list.next = (expr_list_t *)alloc_new_expr(ws, EXPR_LIST );
			assert("VD",ret->list.next!=NULL);
			ret->list.next->next = NULL;
			ret->list.next->source = e2;
		}
	}
	return ret;
}

static expr_v4_t *
list_async_expression(struct reiser4_syscall_w_space * ws,
		      expr_v4_t * e1,
		      expr_v4_t * e2 )
{
	return list_expression( ws, e1 , e2  );
}

/*not yet ready*/
static expr_v4_t *
unname( struct reiser4_syscall_w_space * ws,
	expr_v4_t * e1 )
{
	return e1;
}

/*not yet ready*/
static expr_v4_t *
name( struct reiser4_syscall_w_space * ws,
      expr_v4_t * e1 )
{
	return e1;
}

/*not yet ready*/
static expr_v4_t *
transcrash( struct reiser4_syscall_w_space * ws,
	    expr_v4_t * e1 )
{
	return e1;
}

#if 0
#define INIT_RNG(rn,ex,u_t,r_t,of,sz)\
	(rn)->rng.host = (ex);\
	(rn)->rng.units_type = (u_t);\
	(rn)->rng.range_type = (r_t);\
	(rn)->rng.offset= (of);\
	(rn)->rng.size = (sz);
#endif

static expr_v4_t *
assign(struct reiser4_syscall_w_space * ws,
       expr_v4_t * e1,
       expr_v4_t * e2,
       int mode )
{
	val_range_t * rng;
	expr_v4_t * e;
	expr_v4_t * ret;

	/* while for each pars_var in e1 */

	switch(e1->h.type) {
	case EXPR_WRD:    /*?????????????*/
		break;
	case EXPR_RANGE:
	case EXPR_PARS_VAR:
		ret = pump( ws, e1, e2, NULL, mode );
//		ret = pump( ws, e1->pars_var.v, e2, NULL, mode ); 
		break;
	case EXPR_LIST:
#if 0
		{    not yet ready.
			expr_v4_t * source;
			expr_v4_t * last;
			expr_v4_t * cur;

			make_var(tmp); /*1 create a tmp name, ???????*/
			assign(ws, tmp, e2, mode); /*2 assign the e2 to tmp????*/

			cur = e1;	
			while( cur ) { /*3 loop for each expression in list*/
				source = cur->source;
				assign(ws, source, tmp, mode);
				cur = cur->list.next;
			}
		}
#endif
		break;
	case EXPR_ASSIGN:  /*??????????*/
		break;
	case EXPR_LNODE:   /*?????????*/
		break;
	case EXPR_FLOW:
		break;
	case EXPR_OP2:
		/*

		 */
		break;
	case EXPR_OP:
		break;
	}

	return ret;
}

/* not yet */
static expr_v4_t *
assign_invert(struct reiser4_syscall_w_space * ws,
	      expr_v4_t * e1,
	      expr_v4_t * e2, int mode)
{
	return assign(ws, e1, e2, mode); /* not yet ready */
}

/* not yet */
static expr_v4_t *
symlink(struct reiser4_syscall_w_space * ws,
	expr_v4_t * e1,
	expr_v4_t * e2)
{
	return e2;
}

static rng_command_t *
range_expression2command(struct reiser4_syscall_w_space * ws,
			 int command,
			 wrd_t * val)
{
	rng_command_t * rez;
	char * unused;
	rez = kmalloc(sizeof(rng_command_t), GFP_KERNEL);
	rez->comm = command;
	rez->value = atol_ptr(val->u.name, &unused);
	return rez;
}

static rng_command_t *
range_units_type(struct reiser4_syscall_w_space * ws,
		 int command,
		 int val)
{
	rng_command_t * rez;
	rez = kmalloc(sizeof(rng_command_t), GFP_KERNEL);
	rez->comm = command;
	rez->value = val; 
	return rez;
}

static expr_v4_t *
add_range(struct reiser4_syscall_w_space * ws,
	  expr_v4_t * rng,
	  rng_command_t * command)
{

	switch (command->comm) {
	case COMMAND_UNITS:
		rng->rng.units_type = command->value;
		break;
	case COMMAND_TYPE:
		rng->rng.range_type = command->value;
		break;
	case COMMAND_OFFSET:
		rng->rng.offset = command->value;
		break;
	case COMMAND_LAST:
		rng->rng.size = command->value - rng->rng.offset; /*??? if no offset or the last is before offset*/
		break;
	case COMMAND_FIRST:
		rng->rng.offset = command->value;
		break;
	case COMMAND_LEN:
		rng->rng.size = command->value;
		break;
	default:
	}
	if (rng->rng.size < 0 && rng->rng.size != -1) {
		rng->rng.size = 0;
	}
	kfree(command);
printk("add_range:rng->rng.offset=%ld, rng->rng.size=%d, rng->rng.units_type=%d, rng->rng.range_type=%d\n",
       rng->rng.offset, rng->rng.size, rng->rng.units_type, rng->rng.range_type);
	return rng;
}

static expr_v4_t *
new_range(struct reiser4_syscall_w_space * ws,
	  rng_command_t * command)
{
	expr_v4_t * rez;
	rez         = alloc_new_expr( ws, EXPR_RANGE );
	rez->rng.size = -1;
	rez->rng.units_type = UNITS_BYTE;
	rez->rng.range_type = RANGE_CUT;
	add_range(ws, rez, command);
	return rez;
}

static inline expr_v4_t *
range2expr(struct reiser4_syscall_w_space * ws,
	   expr_v4_t * host,
	   expr_v4_t * val)
{
	val->rng.host = host;
	return val;
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

static expr_v4_t *
reiser4_assign( pars_var_t *dst,
		expr_v4_t *src )
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
    connection.u=common_transfer;
    connection = dst->v->fplug -> select_connection( src, dst );

    /* do transfer */
    return common_transfer( &dst, &src );
}
#endif


static  int
source_not_empty(expr_v4_t *source)
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


static int
push_tube_stack( tube_t * tube,
		 long type,
		 void * pointer )
{
	sourece_stack_t * ret;
	ret = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(ret)) {
		ret->prev        = tube->st_current;
		ret->type        = type;
		ret->u.pointer   = pointer;
		tube->st_current = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static int
push_tube_list_stack_done(tube_t * tube)
{
	tube->next->prev = tube->st_current;
	tube->st_current = tube->last;
	tube->last       = NULL;
	tube->next       = NULL;
	return 0;
}


static int
push_tube_list_stack_init( tube_t * tube,
			   long type,
			   void * pointer )
{
	//	sourece_stack_t * ret;
	tube->last = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(tube->last)) {
		tube->next            = tube->last;
		tube->last->type      = type;
		tube->last->u.pointer = pointer;
		return 0;
	}
	else {
		return PTR_ERR(tube->last);
	}
}

static int
push_tube_list_stack(tube_t * tube,
		     long type,
		     void * pointer )
{
	sourece_stack_t * ret;
	ret = kmalloc( sizeof(struct sourece_stack), GFP_KERNEL );
	if (!IS_ERR(ret)) {
		tube->next->prev = ret;
		ret->type        = type;
		ret->u.pointer   = pointer;
		tube->next       = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static int
change_tube_stack(tube_t * tube,
		  long type,
		  void * pointer )
{
	tube->st_current->type       = type;
	tube->st_current->u.pointer  = pointer;
	return 0;
}

static int
pop_tube_stack( tube_t * tube )
{
	sourece_stack_t * ret;
	if ( tube->st_current == NULL ) {
		return -1;
	}
	else {
		ret              = tube->st_current;
		tube->st_current = tube->st_current->prev;
		kfree( ret );
		return 0;
	}
}

static int
push_var_val_stack(struct reiser4_syscall_w_space *ws /* work space ptr */,
		   struct pars_var * var,
		   long type)
{
	pars_var_value_t * ret;
	ret = kmalloc( sizeof(pars_var_value_t), GFP_KERNEL );
	if (!IS_ERR(ret)) {
		memset( ret , 0, sizeof( pars_var_value_t ));
		ret->prev       = var->val;
		ret->vtype      = type;
		ret->host       = var;
		ret->count      = 1;
		ret->next_level = ws->cur_level->val_level;
		ws->cur_level->val_level = ret;
		var->val        = ret;
		return 0;
	}
	else {
		return PTR_ERR(ret);
	}
}

static void
free_val(struct reiser4_syscall_w_space *ws /* work space ptr */,
	 pars_var_value_t * val /* value for free */)
{
	val->host->val           = val->prev;
	ws->cur_level->val_level = val->next_level;
	kfree( val );
}



static int
pop_var_val_stack( struct reiser4_syscall_w_space *ws /* work space ptr */,
		   pars_var_value_t * val, /* value for free */
		   int tmp_only /* free only if tmp value */)
{
	//	pars_var_value_t * ret;
	if ( val == NULL ) {
		return -1;
	}
	else {
		if (val->destruct!=NULL) {
			val->destruct(ws, val);
		}
		switch(val->vtype) {
		case VAR_EMPTY:
			break;
		case VAR_LNODE:
			if ( !tmp_only )
				{
					assert("VD", val->u.ln!=NULL);
//		if ( !--var->val->count )
					{
						path4_release( &(val->u.ln->l_dentry) );
						lput( val->u.ln );
					}
					free_val(ws, val);
				}
			break;
		case VAR_TMP:
			if (val->u.data!=NULL) {
				kfree( val->u.data );
			}
			free_val(ws, val);
			break;
		}
		return 0;
	}
}


/*  pop onto stack for calculate expressions one step */
static void
put_tube_src(tube_t * tube)
{
	/*  close readed file and pop stack */
	switch (tube->st_current->type) {
	case 	ST_FILE:
		filp_close(tube->st_current->u.file, current->files );
		break;
	case 	ST_FILE4:
		reiser4_filp_close(tube->st_current->u.file4 );
		break;
	case 	ST_DE:
	case 	ST_WD:
	case 	ST_DATA:
		break;
	}
	pop_tube_stack(tube);
}


/* push & pop onto stack for calculate expressions one step */
static int
get_tube_next_src_var_lnode(tube_t * tube)
{
	lnode *ln;
	int ret;
	ln = tube->st_current->u.expr->pars_var.v->val->u.ln;
	assert("VD", ln!=NULL);
	switch(ln->h.type)
		{
		case LNODE_DENTRY:
			{
				struct dentry * de;
				struct file * fl;
				assert("VD", ln->l_dentry.dentry != NULL);
printk("get_tube_next_src_var_lnode:LNODE_DENTRY\n");
				de = ln->l_dentry.dentry;
				if ( S_ISREG(de->d_inode->i_mode) ) {
					fl=  dentry_open( de,ln->l_dentry.mnt, O_RDONLY ) ;
					if ( !IS_ERR(fl) ) {
						change_tube_stack( tube, ST_FILE, fl);
						ret =0;
					}
					else {
						printk("error for open source\n");
						ret = -1;
					}
				}
#if 0         // not yet ready
				else if ( S_ISDIR(de->d_inode->i_mode) ) {
					while(!EOF readdir) {
						fl=  dentry_open( readdir_back_order(de), ln->l_dentry.mnt, O_RDONLY ) ;
						if ( !IS_ERR(fl) ) {
							push_tube_stack( tube, ST_FILE, fl);
							ret = 0 ;
						}
						else {
							printk("error for open source\n");
							ret = -1;
						}
					}
				}
#endif
			}
			break;
		case LNODE_REISER4_INODE: /* */
			{
				struct reiser4_file fl4;
				if (REISER4_ISREG(ln)) {
					fl4 = reiser4_open(ln, O_WRONLY|mode);
					if ( fl4 != null) {
						change_tube_stack( tube, ST_FILE4, fl);
						ret =0;
					}
					else {
						printk("error for open reiser4_source\n");
						ret = -1;
					}
				}
			}
			break;
		case LNODE_LW:            /* for reiser4 partition */
			ret = -1;
			break;
		case LNODE_COORD:
			ret = -1;
			break;
		case LNODE_PSEUDO:
			ret = -1;
			break;
		}
	return ret;
}

/* push & pop onto stack for calculate expressions one step */
static int
get_tube_next_src(tube_t * tube)
{
	expr_v4_t * s;
	expr_list_t * tmp;
	int ret;

	assert ("VD", tube->st_current != NULL );

	/* check stack and change its head */
	switch (tube->st_current->type) {
	case 	ST_FILE:
	case 	ST_FILE4:
	case 	ST_DE:
	case 	ST_WD:
		ret = 0;
		break;
	case 	ST_EXPR:
		s = tube->st_current->u.expr;
		switch (s->h.type) {
		case EXPR_WRD:
			change_tube_stack( tube, ST_WD , s->wd.s );
			break;
		case EXPR_RANGE:

			{
				int ret;
				wrd_t *tmp_wrd;
				pars_var_t *rezult;
				expr_v4_t * rez_expr;

printk("get_tube_next_src:EXPR_RANGE\n");
                                tmp_wrd = make_new_word(tube->ws, "RANGE" );

				rezult =  lookup_pars_var_word( tube->ws , tube->target, tmp_wrd, VAR_TMP);
				
				if ( rezult != NULL ) {
					rezult->val->u.data  = kmalloc(sizeof(struct qstr) + s->rng.size, GFP_KERNEL ) ;
					rezult->val->u.data->len = s->rng.size;
					rezult->val->u.data->name = (char *)rezult->val->u.data + sizeof(struct qstr);
				}
				rez_expr = alloc_new_expr(tube->ws, EXPR_PARS_VAR);
				rez_expr->pars_var.v = rezult;

				pump(tube->ws, rez_expr, s->rng.host, s, O_TRUNC);

				change_tube_stack( tube, ST_EXPR , rez_expr );
			}

			/*
make new assign with rng->size  as the lhs

	alloc the area with val->size and with tmpname,
		assign the host to allocated tmpname with val-offset,val-size parameters,
		set the type of result for EXPR_FLOW or ???, return it.

			*/



			break;
		case EXPR_PARS_VAR:
printk("get_tube_next_src:EXPR_PARS_VAR\n");
			assert("VD", s->pars_var.v!=NULL);
			assert("VD", s->pars_var.v->val!=NULL);
			switch( s->pars_var.v->val->vtype) {
			case VAR_EMPTY:
				break;
			case VAR_LNODE:
				get_tube_next_src_var_lnode(tube);
				ret = 0;
				break;
			case VAR_TMP:
				if ( s->pars_var.v->val->u.data == NULL ) {
					pop_tube_stack( tube );
					ret = 1;
				}
				else {
					change_tube_stack( tube, ST_DATA , s->pars_var.v->val->u.data);
					ret = 0;
				}
				break;
			}
			break;
		case EXPR_LIST:
			tmp = &s->list;
			push_tube_list_stack_init( tube, ST_EXPR , tmp->source );
			while (tmp) {
				tmp = tmp->next;
				push_tube_list_stack( tube, ST_EXPR, tmp->source );
			}
			pop_tube_stack( tube );
			push_tube_list_stack_done( tube );
			ret = 1;
			break;
		case EXPR_ASSIGN:
#if 0   // not yet
			assert("VD", s->assgn.target!=NULL);
			assert("VD", s->assgn.target->ln!=NULL);
			assert("VD", s->assgn.target->val->count>0);
			( s->assgn.target->ln);
			( s->assgn.source );
#endif
			break;
		case EXPR_LNODE:
			assert("VD", s->lnode.lnode!=NULL);
			switch (s->lnode.lnode->h.type) {
			case LNODE_DENTRY:
				{
					struct file *fl;
//					if ( S_ISREG(s->lnode.lnode->dentry.dentry->d_inode) )
					assert ("VD", S_ISREG(s->lnode.lnode->dentry.dentry->d_inode) );
					fl = dentry_open( s->lnode.lnode->l_dentry.dentry,
							  s->lnode.lnode->l_dentry.mnt, O_RDONLY );
					assert("VD", fl!=NULL);
					change_tube_stack( tube, ST_FILE , fl);
				}
				break;
			case LNODE_REISER4_INODE:
				{
					struct reiser4_file *fl;
//					if ( REISER4_ISREG(s->lnode.lnode) )
					assert("VD", REISER4_ISREG(s->lnode.lnode) );
					fl = reiser4_open( s->lnode.lnode, O_RDONLY );
					assert("VD", fl!=NULL);
					change_tube_stack( tube, ST_FILE4 , fl);
				}
				break;
			}
			ret = 0;
			break;
		case EXPR_FLOW:
			break;
#if 0
		case EXPR_OP2:
			change_tube_stack( tube, ST_EXPR , s->op2.op_r );
			push_tube_stack( tube, ST_EXPR , s->op2.op_l );
			push_tube_stack( tube, COMMAND, s->h.exp_code); ??????????
			ret = 1;
			break;
		case EXPR_OP:
			change_tube_stack( tube, ST_EXPR , s->op.op );
			push_tube_stack( tube, COMMAND, s->h.exp_code ); ??????????
			ret = 1;
			break;
#endif
		}
		break;
	}
	return ret;
}


static size_t
target_rng_doing(tube_t * tube)
{
	if (tube->target_rng == NULL) {
		return 0;
	}
	else {
		switch(tube->target_rng->units_type) {
		case UNITS_BYTE:
			return tube->target_rng->offset;
			break;
		case UNITS_LINE:
			/*not yet ready*/
			return 0;
			break;
		case UNITS_DELIMITER:
			/*not yet ready*/
			return 0;
			break;
		case UNITS_ITEM:
			/*not yet ready*/
			return 0;
			break;
		}

	}
}


static tube_t *
get_tube_general(struct reiser4_syscall_w_space *ws)
{
	tube_t * tube;
	tube = kmalloc( sizeof(struct tube), GFP_KERNEL);
	if (tube != NULL) {
		memset( tube , 0, sizeof( struct tube ));
		tube->buf = kmalloc( PUMP_BUF_SIZE, GFP_KERNEL);
		tube->len = PUMP_BUF_SIZE;
		if (tube->buf != NULL) {
			memset( tube->buf  , 0, PUMP_BUF_SIZE);
		}
		tube->st_current  = NULL;
		tube->ws = ws;
	}
	return tube;
}



static size_t
get_source(tube_t * tube,
	   expr_v4_t *source,
	   expr_v4_t *s_rng)
{
	tube->source_rng = s_rng;
	if (s_rng == NULL) {
		tube->read_w_off = 0;
		tube->read_size = -1;
	}
	else {
		tube->read_w_off = s_rng->rng.offset;
		tube->read_size = s_rng->rng.size;
	}
	push_tube_stack( tube, ST_EXPR, (long *)source );
	printk("get_source:tube->read_w_off=%ld, tube->read_size=%d\n",tube->read_w_off, tube->read_size);
}

static val_range_t *
reverse_rng(val_range_t *rng, expr_v4_t *cur)
{
	val_range_t * ret;
	if (rng == NULL) {
		ret = (val_range_t *) cur;
	}
	else {
		rng->host = cur;
		ret = rng;
	}
	return ret;
}

static size_t
reserv_space_in_sink(tube_t * tube,
		     expr_v4_t *sink,
		     int mode)
{
	expr_v4_t *cur;
	lnode * ln;
	cur = sink;
	while (cur->h.type == EXPR_RANGE) {
printk("reserv_space_in_sink:EXPR_RANGE\n");
		tube->target_rng = reverse_rng(tube->target_rng, cur);
		cur = cur->rng.host;
	}
	assert("VD",cur->h.type == EXPR_PARS_VAR);
printk("reserv_space_in_sink:EXPR_PARS_VAR\n");

	if (tube->target_rng != NULL) {
//		tube->writeoff = target_rng_doing(tube);
		tube->writeoff = tube->target_rng->offset;
		tube->write_w_off = tube->writeoff;
		tube->write_size = tube->target_rng->size;
printk("reserv_space_in_sink:tube->target_rng-> (offset=%ld, size=%d, units_type=%d, range_type=%d)\n",
       tube->target_rng->offset, tube->target_rng->size, tube->target_rng->units_type, tube->target_rng->range_type);
	}
	else {
		tube->writeoff = 0;
		tube->write_w_off = 0;
		tube->write_size = -1;
	}

	tube->target = cur->pars_var.v;
	switch( tube->target->val->vtype ) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		ln = tube->target->val->u.ln;
		switch (ln->h.type) {
		case LNODE_DENTRY:
			tube->u.dst = dentry_open( ln->l_dentry.dentry,
						 ln->l_dentry.mnt, O_WRONLY|mode );
			assert("VD",tube->u.dst!=NULL);
			break;
		case LNODE_REISER4_INODE:
			tube->u.dst4 = reiser4_open(ln, O_WRONLY|mode);
			break;
		}
		break;
	case VAR_TMP:
		break;
	}

printk("reserv_space_in_sink:tube->writeoff=%ld,tube->write_w_off=%ld, tube->write_size=%d\n",
	       tube->writeoff, tube->write_w_off, tube->write_size);
}

static size_t
get_available_src_len(tube_t * tube)
{
	size_t s_len;
	int ret = 1;
	while ( tube->st_current != NULL && ret ) {
		ret = 0;
		switch( tube->st_current->type ) {
		case ST_FILE:
printk("get_available_src_len:ST_FILE\n");
			s_len = tube->st_current->u.file->f_dentry->d_inode->i_size;
			break;
		case ST_FILE4:
printk("get_available_src_len:ST_FILE4\n");
			s_len = tube->st_current->u.file4->f4_size;
			break;
		case ST_DE:
			break;
		case ST_WD:
			s_len = tube->st_current->u.wd->u.len;
			break;
		case ST_EXPR:
printk("get_available_src_len:ST_EXPR\n");
			while( tube->st_current != NULL && get_tube_next_src( tube ) ) ;
			ret = 1;
			break;
		case ST_DATA:
			s_len = tube->st_current->u.qstr->len;
			break;
			}
		if (tube->read_w_off && !ret) {
			if ( s_len < tube->read_w_off) {
				tube->read_w_off -= s_len;
				ret = 1;
				put_tube_src(tube);
			}
		}
	}
	if (tube->read_w_off) {
		s_len -= tube->read_w_off;
		tube->readoff = tube->read_w_off;
		tube->read_w_off = 0;
		if (s_len < 0) {
			/* range error. the range offset is a larger than the length of source expression */
			s_len = 0;
		}
		
		if (tube->read_size != -1 && s_len > tube->read_size) {
			s_len = tube->read_size;
		}
	}
	else {
		tube->readoff = 0;
	}

	assert("VD", s_len >= 0);
	if (tube->st_current == NULL) {
		s_len = 0;
	}
	return s_len;
}

static size_t
prep_tube_general(tube_t * tube)
{
	size_t ret;
	if ( tube->st_current != NULL ) {
		ret = get_available_src_len( tube ) ;
		if (tube->len > ret) tube->len = ret; 
	}
	else {
		ret = 0;
	}
	return ret;
}



static size_t
source_to_tube_general(tube_t * tube)
{
	//	tube->source->fplug->read(tube->offset,tube->len);
	size_t ret;
	if (tube->read_size != -1) {
		if (tube->read_size < tube->len ) tube->len = tube->read_size;
	}
	switch( tube->st_current->type ) {
	case 	ST_FILE:
printk("source_to_tube_general:ST_FILE\n");
                START_KERNEL_IO_GLOB;
		ret = vfs_read(tube->st_current->u.file, tube->buf, tube->len, &tube->readoff);
		END_KERNEL_IO_GLOB;
		tube->len = ret;
		break;
	case 	ST_FILE4:
printk("source_to_tube_general:ST_FILE4\n");
                START_KERNEL_IO_GLOB;
		ret = reiser4_read(tube->st_current->u.file4, tube->buf, tube->len, &tube->readoff);
		END_KERNEL_IO_GLOB;
		tube->len = ret;
		break;
	case 	ST_WD:
printk("source_to_tube_general:ST_WD\n");
		if ( tube->readoff < tube->st_current->u.wd->u.len ) {
			assert ("VD", tube->readoff+tube->len <= tube->st_current->u.wd->u.len);
			memcpy( tube->buf,  tube->st_current->u.wd->u.name + tube->readoff, ret = tube->len );
			tube->readoff += ret;
		}
		else ret = 0;
		break;
	case ST_DATA:
printk("source_to_tube_general:ST_DATA\n");
		if ( tube->readoff < tube->st_current->u.qstr->len ) {
			if (( tube->readoff + tube->len) > tube->st_current->u.qstr->len) {
				tube->len = tube->st_current->u.qstr->len -tube->readoff;
			}
			memcpy( tube->buf,  tube->st_current->u.qstr->name + tube->readoff, ret = tube->len );
			tube->readoff += ret;
		}
		else ret = 0;
		break;
	}
	if (tube->read_size != -1 && tube->len > 0) tube->read_size -= tube->len;
	return ret;
}

static size_t
tube_to_sink_general(tube_t * tube)
{
	size_t ret;
printk("tube_to_sink_general:\n");

        if (tube->write_size != -1) {
		if (tube->write_size < tube->len ) tube->len = tube->write_size;
	}
	if (tube->len == 0) {
		return tube->len;
	}
	switch(tube->target->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
printk("tube_to_sink_general:VAR_LNODE\n");
                START_KERNEL_IO_GLOB;
		switch (tube->target->val->u.ln->h.type) {
		case LNODE_DENTRY:
			ret = vfs_write(tube->u.dst, tube->buf, tube->len, &tube->writeoff);
			break;
		case LNODE_REISER4_INODE:
			ret = reiser4_write(tube->u.dst4, tube->buf, tube->len, &tube->writeoff);
			break;
		}
		END_KERNEL_IO_GLOB;
		break;
	case VAR_TMP:
printk("tube_to_sink_general:VAR_TMP\n");
		if (tube->target->val->u.data == NULL) {
			tube->target->val->u.data = kmalloc(sizeof(struct qstr) + tube->len + tube->writeoff, GFP_KERNEL);
			tube->target->val->u.data->name = (char *)tube->target->val->u.data + sizeof(struct qstr);
printk("tube_to_sink_general:VAR_TMP, alloc ");
			/*???????? offset */
		}
		else {
			struct qstr * old_data;
			old_data = tube->target->val->u.data;
			tube->target->val->u.data = kmalloc(tube->len + old_data->len + sizeof(struct qstr), GFP_KERNEL);
			tube->target->val->u.data->name = (char *)tube->target->val->u.data + sizeof(struct qstr);
			memmove(tube->target->val->u.data->name, old_data->name, old_data->len);
			kfree(old_data);
printk("tube_to_sink_general:VAR_TMP, realloc\n");
		}
		memmove(tube->target->val->u.data->name + tube->writeoff, tube->buf, tube->len);
		tube->writeoff += tube->len;
		tube->target->val->u.data->len = tube->writeoff;
		ret = tube->len;
printk("tube_to_sink_general:data=%p, name=%p len=%d\n",
			       tube->target->val->u.data,tube->target->val->u.data->name, tube->target->val->u.data->len);
		break;
	}
	if (tube->write_size != -1) tube->write_size -= tube->len;
	return ret;
}

static void
put_tube(tube_t * tube)
{
	END_KERNEL_IO_GLOB;
	assert("VD",tube->st_current == NULL);
	switch(tube->target->val->vtype) {
	case VAR_EMPTY:
		break;
	case VAR_LNODE:
		switch (tube->target->val->u.ln->h.type) {
		case LNODE_DENTRY:
			do_truncate(tube->u.dst->f_dentry, tube->writeoff);
			filp_close(tube->u.dst, current->files );
			break;
		case LNODE_REISER4_INODE:
			reiser4_do_truncate(tube->u.dst4, tube->writeoff);
			reiser4_filp_close(tube->u.dst4);
			break;
		}



		break;
	case VAR_TMP:
		break;
	}
	kfree(tube->buf);
	kfree(tube);
}

static int
create_result_field(struct reiser4_syscall_w_space * ws,
		    pars_var_t *parent,   /* parent for name */
		    char * name ,         /* created name    */
		    int result_len,       /* length of allocated space for value */
		    size_t result)
{
	int ret;
	wrd_t * tmp_wrd;
	pars_var_t * rezult;

	tmp_wrd = make_new_word(ws, name );
	rezult =  lookup_pars_var_word( ws , parent, tmp_wrd, VAR_TMP);
	if ( rezult != NULL ) {
		rezult->val->u.data  = kmalloc(sizeof(struct qstr) + result_len, GFP_KERNEL ) ;
		rezult->val->u.data->len = result_len;
		rezult->val->u.data->name = (char *)rezult->val->u.data + sizeof(struct qstr);
		if ( rezult->val->u.data->name ) {
			sprintf( rezult->val->u.data->name, "%d", result );
			ret=0;
		}
	}
	else {
		ret = 1;
	}
	return ret;
}

static int
create_result(struct reiser4_syscall_w_space * ws,
	      pars_var_t *parent,   /* parent for name       */
	      int err_code ,        /* error code of assign  */
	      int length)           /* length of assign      */
{
	int ret;
	ret  = create_result_field( ws, parent,
			     ASSIGN_RESULT, SIZEFOR_ASSIGN_RESULT, err_code );
	ret += create_result_field( ws, parent,
			     ASSIGN_LENGTH, SIZEFOR_ASSIGN_LENGTH, length );
	return ret;
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
static expr_v4_t *
pump(struct reiser4_syscall_w_space * ws,
     expr_v4_t *sink,
     expr_v4_t *source,
     expr_v4_t *s_rng,
     int mode )
{
	//	pars_var_t * assoc;
	expr_v4_t * ret;
	tube_t * tube;
	size_t (*prep_tube)(tube_t *);
	size_t (*source_to_tube)(tube_t *);
	size_t (*tube_to_sink)(tube_t *);
	loff_t source_size;
	ssize_t writed_bytes;
	ssize_t readed_bytes;
	int ret_code;


	/* remember to write code for freeing tube, error handling, etc. */
#if 0
	ret_code = sink->fplug -> get_tube(ws);
	prep_tube = sink->fplug->prep_tube (tube);
	source_to_tube = source->fplug->source_to_tube;
	tube_to_sink = sink->fplug->tube_to_sink;
#else
	tube       = get_tube_general(ws);
	if ( tube == NULL ) {
		ret = NULL;
	}
	else {
		prep_tube      = prep_tube_general;
		source_to_tube = source_to_tube_general;
		tube_to_sink   = tube_to_sink_general;
#endif
		reserv_space_in_sink( tube, sink, mode );
		get_source(tube, source, s_rng);
		ret_code = 0;
		while ( tube->st_current != NULL ) {
			source_size = prep_tube( tube );
                        while  ( source_size > 0 ) {
				readed_bytes = source_to_tube( tube ) ;
				source_size -= readed_bytes ;
				writed_bytes = tube_to_sink( tube )  ;
				if ( !(writed_bytes>0) ) {
					if ( writed_bytes < 0 ) {
						printk("IO write error\n");
						ret_code = writed_bytes;
					}
					break;
				}
			}
			put_tube_src( tube );
			if (writed_bytes < 0) {
				ret_code = writed_bytes;
				break;
			}
			if (readed_bytes < 0 ) {
				ret_code = readed_bytes;
				break;
			}
		}
		
#if 0
			while ( ( source_size > 0 ) &&
				(( readed_bytes = source_to_tube( tube )) > 0 ) ) {

		ret=alloc_new_expr( ws, EXPR_PARS_VAR );
		ret->pars_var.v = tube->target;
#endif

		if ( tube->target->val->associated == NULL) {
			create_result( ws, tube->target, ret_code, (size_t)(tube->writeoff - tube->write_w_off) );
			//			ret->pars_var.v = sink;
		}
		else {
			create_result( ws, tube->target->val->associated, ret_code, (size_t)(tube->writeoff -tube->write_w_off) );
			//			ret->pars_var.v = sink->val->associated;
			tube->target->val->associated = NULL;
		}
		put_tube( tube );
      }
      return ret;
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
