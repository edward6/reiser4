                                 /* level type defines */
#define TW_BEGIN    
#define ASYN_BEGIN
#define CD_BEGIN
#define OP_LEVEL
#define NOT_HEAD
#define IF_STATEMENT
#define UNORDERED


#define  yyacc



                                 /* sizes defines      */
#define WRDTABSIZE
#define FREESPACESIZE (4096 - sizeof(char*)*2 - sizeof(int) )
#define VARTABSIZE


/*
#ifndef YYSTYPE
typedef int YYSTYPE;
#endif
*/

#define wrdTab(x)   ws->f_WrdTab(x)

#define V_type(x)   f_v_type(ws,x)
#define V_extn(x)   f_v_extn(ws,x)
#define V_levl(x)   f_v_levl(ws,x)

#define Stype(x)    f_s_type(ws,x)
#define Slab(x)     f_s_lab(ws,x)
#define Slsco(x)    f_s_lsco(ws,x)
#define Slist(x)    f_s_list(ws,x)



#define inline   ws->ws_inline
#define pnline   ws->ws_pnline
#define yyerrco  ws->ws_yyerrco
#define errco    ws->ws_errco
#define labco    ws->ws_labco 
#define strco    ws->ws_strco 
#define varco    ws->ws_varco 
#define yylex()  reiser4_lex(work_space)



/*
%union 
{
	long longType;
	struct Label * Label;
	struct String * StrPtr;
	struct expr_v4 * expr;
	struct var * Var;
}
*/


typedef enum {
	EXPR_LNODE,
	EXPR_OP2,
	EXPR_OP,
	EXPR_STRING
} expr_type;

/** declare hash table of lnode_lw's */
/*TS_HASH_DECLARE( ln, lnode );*/

typedef union expr_v4  expr_v4;



struct String
{
};

struct Label{
};

typedef struct expr_common {
	/** 
	 * type of expression node
	 */
	__u8          type;
	/** 
	 * type of expression
	 */
	__u8          exp_type;
	union expr_v4 (* de_struct)(union expr_v4 *);
} expr_common;

typedef struct expr_lnode {
	expr_common   h;
	lnode  *lnode;
} expr_lnode;

typedef struct expr_genirik {
	expr_common   h;
	reiser4_key    key;                 /* key of start of flow's sequence of bytes */
	size_t len;			   /* length of flow's sequence of bytes */
} expr_generic;

typedef struct expr_iden {
	expr_common   h;
	String  *  s;
} expr_iden;

typedef struct expr_string {
	expr_common   h;
	String  *  s;
} expr_string;

typedef struct expr_list {
	expr_common   h;
	expr_v4  *  next;
} expr_list;

typedef struct expr_op2 {
	expr_common   h;
	expr_v4  *  op_l;
	expr_v4  *  op_r;
} expr_op2;

typedef struct expr_op {
	expr_common   h;
	expr_v4  *  op;
} expr_op;

typedef struct expr_assign {
	expr_common   h;
	expr_v4  *  target;
	expr_v4  *  source;
	union expr_v4 (* construct)( union expr_v4 *, union expr_v4 *  );
} expr_assign;






union expr_v4 {
	expr_common   h;
	expr_lnode    lnode;
	expr_generic  gen;
	expr_iden     iden;
	expr_string   str;
	expr_list     next;
	expr_op2      op2;
	expr_op       op;
        expr_assign   assign;
};





/* ok this is space for names, constants and tmp*/
typedef struct freeSpace freeSpace_t;

struct freeSpace
{
	freeSpace_t * freeSpace_next;
	char        * freeSpace;
	int           freeSpaceSize;
	char          freeSpaceBase[FREESPACESIZE];
};





/* this is copy for remember
struct qstr {
	const unsigned char * name;
	unsigned int len;
	unsigned int hash;
};
*/


typedef struct var var_t;

struct var
{
	struct qstr u ;             /* txt.name  is ptr to space     */
	var_t * next ;              /* next var                      */
	lnode *  vlnode;            /* lnode for object     on r4-fs */
	int vtype   ;               /* Type of name                  */
	int vSpace  ;               /* v4  space name or not         */
	int vlevel  ;               /* level                     */
};

typedef struct streg                /* for compile time level information */
{
	int stype;                  /* cur type of level        */
	int slab;                   /* label 1                  */
	int sflag;                  /*                  flag    */
	int slsco;                  /* cur count of lists       */
	int slist;                  /* cur type  of lists       */
	lnode * scurrent;           /* cur path for this level  */
	                            /* struct nameidata * curent_nd; */
} streg;



struct msglist
{
	int  msgnum;
	long fileoff;
	struct msglist * nextmsg;
} ;

static struct msglist *Fistmsg;
#define MAXLEVELCO 500

struct yy_r4_work_spaces
{
	char * ws_inline;    /* this two field used for parsing string, one (inline) stay on begin */
	char * ws_pline;     /*   of token, second (pline) walk to end to token                   */


#ifdef yyacc
	                     /* next field need for yacc */
	int ws_yydebug;
	int ws_yynerrs;
	int ws_yyerrflag;
	int ws_yychar;
	short * ws_yyssp;   /* state of automat */
	YYSTYPE * ws_yyvsp;
	YYSTYPE ws_yyval;
	YYSTYPE ws_yylval;
	short   ws_yyss[YYSTACKSIZE];           /* s stack size is ws_yystacksize*/
	YYSTYPE ws_yyvs[YYSTACKSIZE];           /* v stack size is ws_yystacksize*/
	int  ws_yystacksize; /*500*/
	int  ws_yymaxdepth ; /*500*/
#else
	/* declare for bison */
#endif
	int	ws_yyerrco;
	int	ws_level;              /* current level            */
	int	ws_labco;              /* current label            */
	int	ws_errco;              /* number of errors         */
	int	ws_strco;              /* number of entries in tptr*/
	int	ws_varco;              /* number of variables      */
	int	ws_varsol;             /* begin number of variables*/

	                               /* working fields  */
	char * tmpWrdEnd; 
	char * freeSpace;
	char * yytext;
	                               /* space for   */
	freeSpace_t * freeSpHead;
	/*
	varTab    * VarTabHead;
	*/
	streg     * StrTabHead;

	var_t     * WrdHead;
	
	lnode     * current_path;
	
	lnode * root_lnode;

	/*	int       * Gencode;*/
};





static struct
{
	unsigned char numOfParam;
	unsigned char typesOfParam[4]       ;
}
	typesOfCommand[]=
		{ 
			{0,{0,0,0,0}}
		};



static struct 
{
	void (*	call_function)(void) ;
	unsigned char type;            /* describe parameters, and its types */
}
	Code[] =
{
};



TS_LIST_DECLARE( r4_pars );




r4_pars_list_head     HeadVar;

typedef struct _p_VarTab  p_VarTab;

struct _p_VarTab 
{
	r4_pars_list_link     links;	

};


TS_LIST_DEFINE( r4_pars, p_VarTab, links );

/*
 * 
 * r4_pars_list_init             Initialize a list_head
 * r4_pars_list_clean            Initialize a list_link
 * r4_pars_list_is_clean         True if list_link is not in a list
 * r4_pars_list_push_front       Insert to the front of the list
 * r4_pars_list_push_back        Insert to the back of the list
 * r4_pars_list_insert_before    Insert just before given item in the list
 * r4_pars_list_insert_after     Insert just after given item in the list
 * r4_pars_list_remove           Remove an item from anywhere in the list
 * r4_pars_list_remove_clean     Remove an item from anywhere in the list and clean link_item
 * r4_pars_list_remove_get_next  Remove an item from anywhere in the list and return the next element
 * r4_pars_list_remove_get_prev  Remove an item from anywhere in the list and return the prev element
 * r4_pars_list_pop_front        Remove and return the front of the list, cannot be empty
 * r4_pars_list_pop_back         Remove and return the back of the list, cannot be empty
 * r4_pars_list_front            Get the front of the list, cannot be empty
 * r4_pars_list_back             Get the back of the list, cannot be empty
 * r4_pars_list_next             Iterate front-to-back through the list
 * r4_pars_list_prev             Iterate back-to-front through the list
 * r4_pars_list_end              Test to end an iteration, either direction
 * r4_pars_list_splice           Join two lists at the head
 * r4_pars_list_empty            True if the list is empty
 * r4_pars_list_object_ok        Check that list element satisfies double 
 *                                list invariants. For debugging.
 *
 * To iterate over such a list use a for-loop such as:
 *
 *   r4_pars_list_head *head = ...;
 *   r4_pars *item;
 *
 *   for (item = r4_pars_list_front (head);
 *             ! r4_pars_list_end   (head, item);
 *        item = r4_pars_list_next  (item))
 *     {...}
 * */



struct tree_struct
{
	union tree_node *chain;
	union tree_node *type;
};


struct tree_string
{
	struct tree_stuct common;
	int  length;
	char *pointer;
};


struct tree_identifier
{
	struct tree_stuct common;
	int length;
	char *pointer;
};

struct tree_var
{
	struct tree_stuct common;
	union tree_node *purpose;
	union tree_node *value;
};





struct tree_list
{
	struct tree_stuct common;
	union tree_node *purpose;
	union tree_node *value;
};

struct tree_vec
{
	struct tree_stuct common;
	int length;
	union tree_node *a[1];
};




struct tree_type
{
	struct tree_stuct common;
	union tree_node (* construct)( union tree_node *, union tree_node *  );
	union tree_node (* de_struct)(union tree_node *);

};

union tree_node
{
	struct tree_stuct common;
	
	struct tree_string string;
	
	struct tree_identifier identifier;
	
	struct tree_type type;
	
	struct tree_list list;
	struct tree_vec vec;
	struct tree_exp exp;

};



static struct
{
	char    *       wrd;
	int             class;
}
key [] =
	{
		{ "and"         ,    AND            },
		
		{ "bytes"       ,    BYTES          },
		
		{ "else"        ,    ELSE           },
		{ "eq"          ,    EQ             },
		{ "exist"       ,    EXIST          },
		
		{ "first"       ,    FIRST          },
		
		{ "ge"          ,    GE             },
		{ "gt"          ,    GT             },
		
		{ "if"          ,    IF             },
		
		{ "last"        ,    LAST           },
		{ "le"          ,    LE             },
		{ "lt"          ,    LT             },
		
		{ "ne"          ,    NE             },
		{ "not"         ,    NOT            },
		
		{ "offset"      ,    OFFSET         },
		{ "offset_back" ,    OFFSET_BACK    },
		{ "or"          ,    OR             },
		
		{ "/process"     ,    SLASH_PROCESS },

		{ "/range"       ,    SLASH_RANGE    },
		
		{ "/stat"        ,    SLASH_STAT     },

		{ "then"        ,    THEN           },
		{ "tw/"          ,    TRANSCRASH     }
	};


void level_down( int );
void level_down( int  );
void goto_end(void);
void else_lab(void);
void make_end_label(void);


freeSpace_t * freeSpaceAlloc(void);
/*strtab * StrTabAlloc(void)*/

lnode * make_do_it( lnode * );
lnode * objToExpr( lnode * );
lnode * constToExpr( lnode * ); 
lnode * connect_expression( lnode *, lnode * ); 
lnode * compare_EQ_expression( lnode * , lnode *  );
lnode * compare_NE_expression( lnode * , lnode *  );
lnode * compare_LE_expression( lnode * , lnode *  );
lnode * compare_GE_expression( lnode * , lnode *  );
lnode * compare_LT_expression( lnode * , lnode *  );
lnode * compare_GT_expression( lnode * , lnode *  );
lnode * compare_OR_expression( lnode * , lnode *  );
lnode * compare_AND_expression( lnode * , lnode *  );
lnode * not_expression( lnode *  ); 
lnode * check_exist( lnode *  );
lnode * associate( lnode * , lnode *  );
lnode * end_tw_list( struct Label * , lnode *  );
lnode * list_expression( lnode * , lnode *  );
lnode * list_async_expression( lnode * , lnode *  );
lnode * end_async_list( struct Label *, lnode *  );
lnode * assign( lnode * , lnode *  );
lnode * assign_invert( lnode * , lnode *  );
lnode * symlink( lnode * , lnode *  );
lnode * contens_of( lnode *  );
lnode * contens_to( lnode *  );
lnode * make_cd( lnode * ,  int );
lnode * init_Unordered(struct var * );
lnode * add_Unordered( lnode * ,struct var *);
lnode * range_lnode ( lnode * , $2 );
lnode * pars_path_walk( struct var * );
lnode * make_proc_lnode();

struct Label * begin_tw_list( int );
struct Label * begin_asynchronouse( int ); 
struct Label * goto_if_false( struct Label *, lnode *  );
struct Label * reserv_label( int );

void move_selected_word(struct yy_r4_work_spaces *  );
int b_check_word(struct yy_r4_work_spaces * );
var_t * inttab(struct yy_r4_work_spaces * );
int reiser4_lex( struct yy_r4_work_spaces * );

lnode * get_root_lnode(struct yy_r4_work_spaces * );

int pars_path_walk(struct yy_r4_work_space * ws, struct ???Name * NamePtr);

lnode * make_inode_from_plugin( reiser4_plugin , nd );

int getvar(struct yy_r4_work_space * ws, int n, int def);
int newvar(struct yy_r4_work_space * ws, int n);
int newtmp(struct yy_r4_work_space * ws, int n);

void lup(struct yy_r4_work_space *ws, int s1);

int ldw(struct yy_r4_work_space * ws);
int reiser4_assign( sink_t *dst, flow_t *src );
int common_transfer( sink_t *target, flow_t *source );

/*******************************************/


/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
