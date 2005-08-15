 /*
 * Copyright, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * parser.y
 */

                                 /* level type defines */
#if !defined( __REISER4_PARSER_H__ )
#define __REISER4_PARSER_H__


#include <linux/fs.h>		/* for struct super_block, address_space  */
#include <linux/mm.h>		/* for struct page */
#include <linux/buffer_head.h>	/* for struct buffer_head */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/types.h>

#include <linux/namei.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <asm-generic/errno.h>

#include "../forward.h"
//#include "../debug.h"
//#include "../dformat.h"
#include "../znode.h"
//#include "../vfs_ops.h"
#include "../inode.h"
#include "../super.h"
#include "../reiser4.h"
#include "lnode.h"
#include "../type_safe_list.h"
#include "../kassign.h"
#include "../coord.h"
#include "../key.h"
#include "../seal.h"
#include "../plugin/item/item.h"
#include "../plugin/security/perm.h"
#include "../plugin/plugin.h"
#include "../plugin/object.h"
#include "../plugin/plugin_header.h"
#include "../plugin/item/static_stat.h"
#include "../plugin/item/internal.h"
#include "../plugin/item/sde.h"
#include "../plugin/item/cde.h"
#include "../plugin/item/extent.h"
#include "../plugin/item/tail.h"
#include "../plugin/file/file.h"
//#include "../plugin/symlink.h"
//#include "../plugin/dir/hashed_dir.h"
#include "../plugin/dir/dir.h"
#include "../plugin/item/item.h"
#include "../plugin/node/node.h"
#include "../plugin/node/node40.h"
#include "../plugin/security/perm.h"
#include "../plugin/space/bitmap.h"
#include "../plugin/space/space_allocator.h"
#include "../plugin/disk_format/disk_format40.h"
#include "../plugin/disk_format/disk_format.h"



typedef enum {
	TW_BEGIN,
	ASYN_BEGIN,
	CD_BEGIN,
	OP_LEVEL,
	NOT_HEAD,
	IF_STATEMENT,
	UNORDERED
} def;

//#define printf(p1,...) PTRACE(ws,p1,...)

#define YYREISER4_DEF

#define YYSTACKSIZE 500
#define YYMAXDEPTH 500
//#define yydebug ws->ws_yydebug 
//#define yynerrs ws->ws_yynerrs
#define yyerrflag ws->ws_yyerrflag
//#define yychar ws->ws_yychar
#define yyssp ws->ws_yyssp
#define yyvsp ws->ws_yyvsp
#define yyval ws->ws_yyval
#define yylval ws->ws_yylval
#define yyss ws->ws_yyss
#define yyvs ws->ws_yyvs
#define yyls ws->ws_yyls
#define yylsp ws->ws_yylsp
//#define yystacksize ws->ws_yystacksize






#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)

#define YYPARSE_PARAM struct reiser4_syscall_w_space  * ws
#define YYLEX_PARAM ws
#define YYINITDEPTH 200
#define YYERROR_VERBOSE

#ifdef YYSTACK_USE_ALLOCA
#undef YYSTACK_USE_ALLOCA
#define YYSTACK_USE_ALLOCA 0
#endif

#ifdef __GNUC__
#undef __GNUC__
#endif 

//#define YYSTACK_ALLOC(i) kmalloc((i),GFP_KERNEL )
#define free kfree
#define  bizon
#define YYFPRINTF
#else
#define register
#define  yyacc
#endif /*defined(CONFIG_REISER4_FS_SYSCALL_BISON)*/

#define printf prink




#define yylex(a,b,c)  reiser4_lex((a),(b),(c))


#define  PARSER_DEBUG


#if 1
#define PTRACE(ws, format, ... )						\
({										\
	ON_TRACE(TRACE_PARSE, "parser:%s %p %s: " format "\n",	                \
		 __FUNCTION__, ws, (ws)->ws_pline, __VA_ARGS__);		\
})
#else
#define PTRACE(ws, format, ... )						\
({										\
	printk("parser:%s %p %s: " format "\n",	                \
		 __FUNCTION__, ws, (ws)->ws_pline, __VA_ARGS__);		\
})
#endif

#define PTRACE1( format, ... )				        		\
({										\
	ON_TRACE(TRACE_PARSE, "parser:%s  " format "\n",	                \
		 __FUNCTION__,  __VA_ARGS__);					\
})


#define ASSIGN_RESULT "assign_result"
#define ASSIGN_LENGTH "assign_length"

#define SIZEFOR_ASSIGN_RESULT 16
#define SIZEFOR_ASSIGN_LENGTH 16





typedef struct pars_var pars_var_t;
typedef union expr_v4  expr_v4_t;
typedef struct wrd wrd_t;
typedef struct tube tube_t;
typedef struct sourece_stack sourece_stack_t;
typedef struct val_range val_range_t;

typedef enum {
	ST_FILE,
	ST_FILE4,
	ST_EXPR,
	ST_DE,
	ST_WD,
	ST_DATA
} stack_type;

typedef enum {
	noV4Space,
	V4Space,
	V4Plugin
} SpaceType;

typedef enum {
	CONCAT,
	COMPARE_EQ,
	COMPARE_NE,
	COMPARE_LE,
	COMPARE_GE,
	COMPARE_LT,
	COMPARE_GT,
	COMPARE_OR,
	COMPARE_AND,
	COMPARE_NOT
} expr_code_type;


                                 /* sizes defines      */
#define FREESPACESIZE_DEF PAGE_SIZE*4
#define FREESPACESIZE (FREESPACESIZE_DEF - sizeof(char*)*2 - sizeof(int) )

#define _ROUND_UP_MASK(n) ((1UL<<(n))-1UL)

#define _ROUND_UP(x,n) (((long)(x)+_ROUND_UP_MASK(n)) & ~_ROUND_UP_MASK(n))

// to be ok for alpha and others we have to align structures to 8 byte  boundary.


#define ROUND_UP(x) _ROUND_UP((x),3)



struct tube {
	struct reiser4_syscall_w_space * ws;
	int type_offset;
	char * offset;       /* pointer to reading position */
	size_t len;            /* lenth of current operation
                               (min of (max_of_read_lenth and max_of_write_lenth) )*/
	long used;
	char * buf;          /* pointer to bufer */
	loff_t readoff;      /* reading offset   */
	loff_t read_w_off;   /* reading window offset   */
	loff_t writeoff;     /* writing offset   */
	loff_t write_w_off;  /* writing window offset   */
	ssize_t write_size;   /* writing max size   */
	ssize_t read_size;    /* reading max size   */

 	sourece_stack_t * last;        /* work. for special case to push list of expressions */
	sourece_stack_t * next;        /* work. for special case to push list of expressions */
	sourece_stack_t * st_current;  /* stack of source expressions */
	val_range_t * target_rng;        /* reversed ranges for target */
	val_range_t * source_rng;        /* range for source */
	pars_var_t * target;
	union {
		struct file *dst;    /* target file  (only for target->val->lnode->l_inode)    */
		struct reiser4_file *dst4; /* target reiser4_file */
	} u;
};

struct wrd {
	wrd_t * next ;                /* next word                   */
	struct qstr u ;               /* u.name  is ptr to space     */
};


struct path_walk {
	struct dentry *de;
	struct vfsmount *mnt;
};

typedef struct expr_common {
	__u8          type;
	__u8          exp_code;
} expr_common_t;


/* types for vtype of struct pars_var */
typedef enum {
	VAR_EMPTY,
	VAR_LNODE,
	VAR_TMP
} VAR_TYPE;

typedef struct pars_var_value pars_var_value_t;

struct pars_var {
	pars_var_t * next ;         /* next                                */
	pars_var_t * parent;        /* parent                              */
	wrd_t * w ;                 /* name: pair (parent,w) is unique     */
	pars_var_value_t * val;
};



typedef enum {
	RANGE_CUT,
	RANGE_ZERRO,
	RANGE_TRANSPARENT,
	RANGE_NR 
}  range_type;

typedef enum {
	UNITS_BYTE,
	UNITS_LINE,
	UNITS_DELIMITER,
	UNITS_ITEM,
	UNITS_NR
} units_type;

typedef enum {
	COMMAND_UNITS,
	COMMAND_TYPE,
	COMMAND_OFFSET,
	COMMAND_LAST,
	COMMAND_FIRST,
	COMMAND_LEN,
	COMMAND_NR
} rng_command;

typedef struct {
	int comm;
	ssize_t value;
} rng_command_t;

struct val_range {
	expr_common_t   h;
	expr_v4_t *host;   // mast be not used - check and delete 17.06.05
        loff_t offset;      /* offset of read/write window for host object  */
	ssize_t size;        /* size of read/write window for host object  */
	unsigned char  range_type;     /* type of window for lhs of assign operator:
					*
					* RANGE_CUT - on rhs the contens of window
					* replaced with the lhs expression.
					* Not depended for size of both side of assign operator.
					* the tail of target (outside of window), will be rewrite
					* to the end of new data.
					*
					* RANGE_ZERRO -if target size is smaller than source,
					* the rest bytes of source will be ignored
					*
					* RANGE_TRANSPARENT -if target size is smaller than source,
					* the rest bytes of source will  be ignored. Same as "zerro" */

	unsigned char units_type;      /* 0 - byte
					  1 - records
					  2 - delimiter (delimiter mast be specified)
					  3 - items */
	unsigned char delimiter; /* it can be a string? */
} ;

struct reiser4_syscall_w_space;

struct pars_var_value {
	pars_var_value_t * prev;    /* previous value in stack for variable */
	pars_var_value_t * next_level; /* next tmp value  in this level */
	pars_var_t * host;          /* host variable structure for this value */
	pars_var_t * associated;    /* var for associate the value */
	int vtype;                  /* Type of value                       */
	union {
		lnode *ln;                  /* file/dir name lnode                 */
		struct qstr *data;                 /*  ptr to data in mem (for result of assign) */
	} u;
	int count;                  /* ref counter                         */
	int vSpace  ;               /* v4  space name or not ???           */
	int vlevel  ;               /* level :     lives of the name       */
	int (*destruct)(struct reiser4_syscall_w_space *, pars_var_value_t * );
} ;

typedef struct expr_lnode {
	expr_common_t   h;
	lnode  *lnode;
} expr_lnode_t;

typedef struct expr_flow {
	expr_common_t    h;
	flow_t     *   flw;
} expr_flow_t;

typedef struct expr_pars_var {
	expr_common_t   h;
	pars_var_t  *  v;
} expr_pars_var_t;


typedef struct expr_wrd {
	expr_common_t   h;
	wrd_t  *  s;
} expr_wrd_t;

#if 0
typedef struct expr_op3 {
	expr_common_t   h;
	expr_v4_t  *  op;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op3_t;
#endif

typedef struct expr_op2 {
	expr_common_t   h;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op2_t;

typedef struct expr_op {
	expr_common_t   h;
	expr_v4_t  *  op;
} expr_op_t;

typedef struct expr_assign {
	expr_common_t   h;
	pars_var_t       *  target;
	expr_v4_t       *  source;
} expr_assign_t;

typedef struct expr_list expr_list_t;
struct expr_list {
	expr_common_t   h;
	expr_list_t     *  next;
	expr_v4_t       *  source;
} ;

typedef enum {
	EXPR_WRD,
	EXPR_PARS_VAR,
	EXPR_RANGE,
	EXPR_LIST,
	EXPR_ASSIGN,
	EXPR_LNODE,
	EXPR_FLOW,
	//	EXPR_OP3,
	EXPR_OP2,
	EXPR_OP
} expr_v4_type;

union expr_v4 {
	expr_common_t   h;
	expr_wrd_t      wd;
	val_range_t       rng;
	expr_pars_var_t pars_var;
	expr_list_t     list;
        expr_assign_t   assgn;
	expr_lnode_t    lnode;
	expr_flow_t     flow;
//	expr_op3_t      op3;
	expr_op2_t      op2;
	expr_op_t       op;
};

/* this is space for names, constants and tmp*/
typedef struct free_space free_space_t;

struct free_space {
	free_space_t * free_space_next;                /* next buffer   */
	char         * freeSpace;                      /* pointer to free space */
	char         * freeSpaceMax;                   /* for overflow control */
	char           freeSpaceBase[FREESPACESIZE];   /* current buffer */
};

struct reiser4_file
{
	struct super_block * f4_sb; /* mount point */
	coord * f4_coord;
	reiser4_key * f4_key;      /* current key */
	size_t	f4_size;
	loff_t f4_offset;           /* current offset to read from or to write to*/
};



struct sourece_stack {
	sourece_stack_t * prev;
	long type;                     /* type of current stack head */
	union {
		struct file   * file;
		struct reiser4_file   * file4;
		expr_v4_t     * expr;
//		struct dentry * de;    /*  ??????? what for  */
		wrd_t         * wd;
		struct qstr   * qstr;
		long          * pointer;
	} u;
};

typedef struct streg  streg_t;

struct streg {
        streg_t * next;
        streg_t * prev;
	expr_v4_t * cur_exp;          /* current (pwd)  expression for this level */
	expr_v4_t * wrk_exp;          /* current (work) expression for this level */
	pars_var_value_t * val_level;
	int stype;                  /* cur type of level        */
	int level;                  /* cur level                */
};


static struct {
	unsigned char numOfParam;
	unsigned char typesOfParam[4]       ;
} typesOfCommand[] = {
	{0,{0,0,0,0}}
};

static struct {
	void (*	call_function)(void) ;
	unsigned char type;            /* describe parameters, and its types */
} 	Code[] = {
};

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
