/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of common constants and data-types used by
 * parser.y
 */

                                 /* level type defines */

#include "../forward.h"
#include "../debug.h"
#include "../dformat.h"
#include "../key.h"
#include "../tslist.h"
#include "../plugin/plugin_header.h"
#include "../plugin/item/static_stat.h"
#include "../plugin/item/internal.h"
#include "../plugin/item/sde.h"
#include "../plugin/item/cde.h"
#include "../plugin/item/extent.h"
#include "../plugin/item/tail.h"
#include "../plugin/file/file.h"
#include "../plugin/symlink.h"
#include "../plugin/dir/hashed_dir.h"
#include "../plugin/dir/dir.h"
#include "../plugin/item/item.h"
#include "../plugin/node/node.h"
#include "../plugin/node/node40.h"
#include "../plugin/security/perm.h"

#include "../plugin/oid/oid40.h"
#include "../plugin/oid/oid.h"

#include "../plugin/space/bitmap.h"
#include "../plugin/space/test.h"
#include "../plugin/space/space_allocator.h"

#include "../plugin/disk_format/disk_format40.h"
#include "../plugin/disk_format/test.h"
#include "../plugin/disk_format/disk_format.h"

#include <linux/fs.h>		/* for struct super_block, address_space  */
#include <linux/mm.h>		/* for struct page */
#include <linux/buffer_head.h>	/* for struct buffer_head */
#include <linux/dcache.h>	/* for struct dentry */
#include <linux/types.h>

typedef enum 
{
	TW_BEGIN,
	ASYN_BEGIN,
	CD_BEGIN,
	OP_LEVEL,
	NOT_HEAD,
	IF_STATEMENT,
	UNORDERED
} def;

#define yylex()  reiser4_lex(ws)
#define  yyacc
//#define  bizon

#define  PARSER_DEBUG

typedef struct vnode vnode_t;

typedef struct wrd wrd_t;

                                 /* sizes defines      */
#define FREESPACESIZE (4096 - sizeof(char*)*2 - sizeof(int) )

#define _ROUND_UP_MASK(n) ((1UL<<(n))-1UL)

#define _ROUND_UP(x,n) (((long)(x)+_ROUND_UP_MASK(n)) & ~_ROUND_UP_MASK(n))

// to be ok for alpha and others we have to align structures to 8 byte  boundary.


#define ROUND_UP(x) _ROUND_UP((x),3)

//struct flow {
//	reiser4_key key;	/* key of start of flow's sequence of bytes */
//	size_t length;		/* length of flow's sequence of bytes */
//	char *data;		/* start of flow's sequence of bytes */
//	int user;		/* if 1 data is user space, 0 - kernel space */
//	rw_op op;               /* */
//};

struct path_walk {
	struct vfsmount *mnt;
	struct dentry *dentry;
};


typedef struct path_walk_name
{
	struct qstr  rest_path;     /**/
	struct qstr  sub_name;
}path_walk_name;

struct wrd
{
	wrd_t * next ;                /* next word                   */
	struct qstr u ;             /* u.name  is ptr to space     */
};

struct vnode
{
	vnode_t * next ;            /* next                          */
	vnode_t * parent;           /* parent                        */
	wrd_t * w ;                 /* pair (parent,w) is unique     */
	lnode * ln;                 /* file/dir name lnode           */
	int count;                  /* ref counter                   */
	int vtype;                  /* Type of name                  */
	size_t off;	            /* current offset read/write of object */
	size_t len;		    /* length of sequence of bytes for read/write (-1 no limit) */
	int vSpace  ;               /* v4  space name or not ???        */
	int vlevel  ;               /* level              ???           */
	int  (*fplug)(lnode * node, const reiser4_plugin_ref * area);
	//	struct qstr restfilename;   /* rest of not resolved name     */
} ;

typedef union expr_v4  expr_v4_t;

typedef struct expr_common 
{
	__u8          type;
	__u8          exp_type;
} expr_common_t;

typedef struct expr_lnode
{
	expr_common_t   h;
	lnode  *lnode;
} expr_lnode_t;

typedef struct expr_flow 
{
	expr_common_t    h;
	flow_t     *   flw;
} expr_flow_t;

typedef struct expr_vnode 
{
	expr_common_t   h;
	vnode_t  *  v;
} expr_vnode_t;


typedef struct expr_wrd 
{
	expr_common_t   h;
	wrd_t  *  s;
} expr_wrd_t;

/* list is same as op2
typedef struct expr_list {
	expr_common   h;
	expr_v4_t  *  next;
	expr_v4_t  *  e;
} expr_list;
*/

typedef struct expr_op3 
{
	expr_common_t   h;
	expr_v4_t  *  op;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op3_t;

typedef struct expr_op2 
{
	expr_common_t   h;
	expr_v4_t  *  op_l;
	expr_v4_t  *  op_r;
} expr_op2_t;

typedef struct expr_op 
{
	expr_common_t   h;
	expr_v4_t  *  op;
} expr_op_t;

typedef struct expr_assign 
{
	expr_common_t   h;
	vnode_t       *  target;
	expr_v4_t       *  source;
	expr_v4_t       *  (* construct)( lnode *, expr_v4_t *  );
} expr_assign_t;

typedef struct expr_list expr_list_t;
struct expr_list 
{
	expr_common_t   h;
	expr_list_t     *  next;
	expr_v4_t       *  source;
} ;

typedef enum 
{
	EXPR_WRD,
	EXPR_VNODE,
	EXPR_LIST,
	EXPR_ASSIGN
	EXPR_LNODE,
	EXPR_FLOW,
	EXPR_OP3,
	EXPR_OP2,
	EXPR_OP
} expr_v4_type;

union expr_v4 
{
	expr_common_t   h;
	expr_wrd_t      wd;
	expr_vnode_t    vnode;
	expr_list_t     list;

        expr_assign_t   assgn;

	expr_lnode_t    lnode;
	expr_flow_t     flow;
	expr_op3_t      op3;
	expr_op2_t      op2;
	expr_op_t       op;
};

/* ok this is space for names, constants and tmp*/
typedef struct freeSpace freeSpace;

struct freeSpace
{
	freeSpace  * freeSpace_next;                 /* next buffer   */
	char       * freeSpace;                      /* pointer to free space */
	char       * freeSpaceMax;                   /* for overflow control */
	char         freeSpaceBase[FREESPACESIZE];   /* current buffer */
};

/*
struct nameidata
{
	struct dentry	*dentry;
	struct vfsmount *mnt;
	struct qstr	last;
	unsigned int	flags;
	int		last_type;
	struct dentry	*old_dentry;
	struct vfsmount	*old_mnt;
};
*/


typedef struct streg  streg_t;

struct streg
{
	int stype;                  /* cur type of level        */
	int level;                  /* cur level                */
        streg_t * next;
        streg_t * prev;
	expr_v4_t * cur_exp;          /* current (pwd)  expression for this level */
	expr_v4_t * wrk_exp;          /* current (work) expression for this level */

//	struct path_walk path_walk;
//	struct dentry * de;          /* current   for this level */
//	struct vfsmount *mnt;
//	struct nameidata_reiser4 nd;        /* current   for this level */
//	lnode * cur_lnode;          /* cur lnode for this level */
//	vnode_t * cur_vnode;          /* cur lnode for this level */

};



struct msglist
{
	int  msgnum;
	long fileoff;
	struct msglist * nextmsg;
} ;

static struct msglist *Fistmsg;



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


/*

TS_LIST_DECLARE( r4_pars );




r4_pars_list_head     HeadVar;

typedef struct _p_VarTab  p_VarTab;

struct _p_VarTab 
{
	r4_pars_list_link     links;	

};


TS_LIST_DEFINE( r4_pars, p_VarTab, links );

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



/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
