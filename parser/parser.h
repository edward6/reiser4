                                 /* level type defines */
#define TW_BEGIN    
#define ASYN_BEGIN
#define CD_BEGIN
#define OP_LEVEL
#define NOT_HEAD
#define IF_STATEMENT


                                 /* sizes defines      */
#define WRDTABSIZE
#define FREESPACESIZE
#define VARTABSIZE
#define 
#define 



#ifndef YYSTYPE
typedef int YYSTYPE;
#endif


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

typedef struct val_list val_list;

struct val_list
{
	val_list *  val_next;
	unsigned    val_type;
	unsigned    val_size;
	void     *  val_space;
} ;


typedef struct freeSpace
{
	freeSpace * freeSpace_next;
	int         freeSpaceSize;
	char *      freeSpace;
	char        freeSpaceBase[FREESPACESIZE];
} freeSpace;

typedef struct wrdtab
{
	wrdtab  *  wrd_next;
	int        wrdTabSize;
	int        wrdTabLast;
	int        wrdTab[WRDTABSIZE];
} wrdtab;

typedef struct var             /* for list of variable */
{
	int vtype   ;   /* Type of name              */
	int vSpace  ;   /* v4  space name or not     */
	int vextn   ;   /* index of names  to wrdTab */
	int vlevel  ;   /* level                     */
	union
	{
		struct inode *  v_inode;    /* inode for object not on r4-fs */
		struct lnode *  v_lnode;    /* lnode for object     on r4-fs */
	} u;
} var;

typedef struct Vartab
{
	wrdtab * Var_next;
	int      VarTabSize;
	int      VarTabLast;
	var      VarTabName[VARTABSIZE];
} Vartab;



typedef struct streg            /* for compile time level information */
{
	int stype;              /* cur type of level        */
	int slab;               /* label 1                  */
	int sflag;              /*                  flag    */
	int slsco;              /* cur count of lists       */
	int slist;              /* cur type  of lists       */
	struct nameidata * curent_nd;
} streg;


typedef struct Strtab
{
	wrdtab * Str_next;
	int      StrTabSize;
	int      StrTabLast;
	strreg   StrTabName[STRTABSIZE];
} Strtab;




struct msglist
{
	int  msgnum;
	long fileoff;
	struct msglist * nextmsg;
} ;

static struct msglist *Fistmsg;

struct yy_r4_work_space
{
	char * ws_inline;    /* this FIXME-NIKITA<-HANS: two field used for parsing string, one (inline) stay on begin */
	char * ws_pline;     /*   of token, second (pline) walk to end to token                   */

	                     /* next field need for yacc */
	int ws_yydebug;
	int ws_yynerrs;
	int ws_yyerrflag;
	int ws_yychar;
	short * ws_yyssp;	/* FIXME-NIKITA<-HANS */
	YYSTYPE * ws_yyvsp;
	YYSTYPE ws_yyval;
	YYSTYPE ws_yylval;
	short   ws_yyss[YYSTACKSIZE];           /* s stack size is ws_yystacksize*/
	YYSTYPE ws_yyvs[YYSTACKSIZE];           /* v stack size is ws_yystacksize*/
	int  ws_yystacksize; /*500*/
	int  ws_yymaxdepth ; /*500*/

	int	ws_yyerrco;
	int	ws_level;              /* current level            */
	int	ws_labco;              /* current label            */
	int	ws_errco;              /* number of errors         */
	int	ws_strco;              /* number of entries in tptr*/
	int	ws_varco;              /* number of variables      */
	int	ws_varsol;             /* begin number of variables*/

	                               /* working fields  */
	struct nameidata * nd;
	char * tmpWrdEnd; 
	char * freeSpace;
	                               /* space for   */
	freeSpace * freeSpHead;
	wrdtab    * WrdTabHead;
	vartab    * VarTabHead;
	streg     * StrTabHead;
	int       * Gencode;
};





static struct
{
	unsigned char numOfParam;
	unsigned char typesOfParam[]       ;
}
	typesOfCommand[]=
{
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


static struct
{
	char    *       wrd;
	int             class;
}
key [] =
	{
		"and"         ,    AND            ,
		
		"bytes"       ,    BYTES          ,
		
		"else"        ,    ELSE           ,
		"eq"          ,    EQ             ,
		"exist"       ,    EXIST          ,
		
		"first"       ,    FIRST          ,
		"first_byte"  ,    FIRST_BYTE     ,
		
		"ge"          ,    GE             ,
		"gt"          ,    GT             ,
		
		"if"          ,    IF             ,
		
		"last"        ,    LAST           ,
		"le"          ,    LE             ,
		"lt"          ,    LT             ,
		
		"last_byte"   ,    LAST_BYTE      ,
		
		"ne"          ,    NE             ,
		"not"         ,    NOT            ,
		
		"offset"      ,    OFFSET         ,
		"offset_back" ,    OFFSET_BACK    ,
		"or"          ,    OR             ,
		
		"process"     ,    PROCESS        ,
		
		"range"       ,    RANGE          ,
		
		"stat"        ,    STAT           ,
		
		"then"        ,    THEN           ,
		"tw"          ,    TRANSCRASH     
	}







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
