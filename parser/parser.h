#define TW_BEGIN
#define ASYN_BEGIN
#define CD_BEGIN
#define OP_LEVEL
#define NOT_HEAD
#define IF_STATEMENT


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









#define V_type(x)    Var[(x)].vtype
#define V_extn(x)    Var[(x)].vextn
#define V_levl(x)    Var[(x)].vlevel

struct var             /* for generator list of variable */
{
int
	vtype   ,   /* Type of name              */
	vextn   ,   /* index of names            */
	vlevel  ,   /* level                     */
struct inode *  v_inode;    /*  */

} ;


#define Stype(x)    Str[(x)].stype
#define Slab(x)     Str[(x)].slab
#define Slsco(x)    Str[(x)].slsco
#define Slist(x)    Str[(x)].slist

struct streg            /* for information compile time level */
{
  int
    stype,              /* cur type of level        */
    slab,               /* label 1                  */
    sflag,              /*                  flag    */
    slsco,              /* cur count of lists       */
    slist;              /* cur type  of lists       */
};



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



int 	yylval;
int	yyval;
int	yyerrco;
int	level;              /* current level            */
int	labco;              /* current label            */
int	errco;              /* number of errors         */
int	strco;              /* number of entries in tptr*/
int	varco;              /* number of variables      */
int	varsol;             /* begin number of variables*/






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
