#include <sys/types.h>
#include <stdlib.h>
#include <asm/msr.h>
#include <stdio.h>
#include <assert.h>

#define TEST_DEBUG 0
#define TEST_SEED  0x632874

#define FULL_SIZE  0

typedef enum
{
  OP_INSERT,
  OP_SEARCH,
  OP_DELETE,
} OPCODE;

/* The 2.4.10 RB tree code from lib/rbtree.c, linux/rbtree.h */

/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...

  Some example of insert and search follows here. The search is a plain
  normal search over an ordered tree. The insert instead must be implemented
  int two steps: as first thing the code must insert the element in
  order as a red leaf in the tree, then the support library function
  rb_insert_color() must be called. Such function will do the
  not trivial work to rebalance the rbtree if necessary.

-----------------------------------------------------------------------
static inline struct page * rb_search_page_cache(struct inode * inode,
						 unsigned long offset)
{
	rb_node_t * n = inode->i_rb_page_cache.rb_node;
	struct page * page;

	while (n)
	{
		page = rb_entry(n, struct page, rb_page_cache);

		if (offset < page->offset)
			n = n->rb_left;
		else if (offset > page->offset)
			n = n->rb_right;
		else
			return page;
	}
	return NULL;
}

static inline struct page * __rb_insert_page_cache(struct inode * inode,
						   unsigned long offset,
						   rb_node_t * node)
{
	rb_node_t ** p = &inode->i_rb_page_cache.rb_node;
	rb_node_t * parent = NULL;
	struct page * page;

	while (*p)
	{
		parent = *p;
		page = rb_entry(parent, struct page, rb_page_cache);

		if (offset < page->offset)
			p = &(*p)->rb_left;
		else if (offset > page->offset)
			p = &(*p)->rb_right;
		else
			return page;
	}

	rb_link_node(node, parent, p);

	return NULL;
}

static inline struct page * rb_insert_page_cache(struct inode * inode,
						 unsigned long offset,
						 rb_node_t * node)
{
	struct page * ret;
	if ((ret = __rb_insert_page_cache(inode, offset, node)))
		goto out;
	rb_insert_color(node, &inode->i_rb_page_cache);
 out:
	return ret;
}
-----------------------------------------------------------------------
*/

#ifndef	_LINUX_RBTREE_H
#define	_LINUX_RBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

typedef struct rb_node_s
{
	struct rb_node_s * rb_parent;
	int rb_color;
#define	RB_RED		0
#define	RB_BLACK	1
	struct rb_node_s * rb_right;
	struct rb_node_s * rb_left;
}
rb_node_t;

typedef struct rb_root_s
{
	struct rb_node_s * rb_node;
}
rb_root_t;

#define RB_ROOT	(rb_root_t) { NULL, }
#define	rb_entry(ptr, type, member)					\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

extern void rb_insert_color(rb_node_t *, rb_root_t *);
extern void rb_erase(rb_node_t *, rb_root_t *);

static inline void rb_link_node(rb_node_t * node, rb_node_t * parent, rb_node_t ** rb_link)
{
	node->rb_parent = parent;
	node->rb_color = RB_RED;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}

#endif	/* _LINUX_RBTREE_H */


static void __rb_rotate_left(rb_node_t * node, rb_root_t * root)
{
	rb_node_t * right = node->rb_right;

	if ((node->rb_right = right->rb_left))
		right->rb_left->rb_parent = node;
	right->rb_left = node;

	if ((right->rb_parent = node->rb_parent))
	{
		if (node == node->rb_parent->rb_left)
			node->rb_parent->rb_left = right;
		else
			node->rb_parent->rb_right = right;
	}
	else
		root->rb_node = right;
	node->rb_parent = right;
}

static void __rb_rotate_right(rb_node_t * node, rb_root_t * root)
{
	rb_node_t * left = node->rb_left;

	if ((node->rb_left = left->rb_right))
		left->rb_right->rb_parent = node;
	left->rb_right = node;

	if ((left->rb_parent = node->rb_parent))
	{
		if (node == node->rb_parent->rb_right)
			node->rb_parent->rb_right = left;
		else
			node->rb_parent->rb_left = left;
	}
	else
		root->rb_node = left;
	node->rb_parent = left;
}

void rb_insert_color(rb_node_t * node, rb_root_t * root)
{
	rb_node_t * parent, * gparent;

	while ((parent = node->rb_parent) && parent->rb_color == RB_RED)
	{
		gparent = parent->rb_parent;

		if (parent == gparent->rb_left)
		{
			{
				register rb_node_t * uncle = gparent->rb_right;
				if (uncle && uncle->rb_color == RB_RED)
				{
					uncle->rb_color = RB_BLACK;
					parent->rb_color = RB_BLACK;
					gparent->rb_color = RB_RED;
					node = gparent;
					continue;
				}
			}

			if (parent->rb_right == node)
			{
				register rb_node_t * tmp;
				__rb_rotate_left(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			parent->rb_color = RB_BLACK;
			gparent->rb_color = RB_RED;
			__rb_rotate_right(gparent, root);
		} else {
			{
				register rb_node_t * uncle = gparent->rb_left;
				if (uncle && uncle->rb_color == RB_RED)
				{
					uncle->rb_color = RB_BLACK;
					parent->rb_color = RB_BLACK;
					gparent->rb_color = RB_RED;
					node = gparent;
					continue;
				}
			}

			if (parent->rb_left == node)
			{
				register rb_node_t * tmp;
				__rb_rotate_right(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			parent->rb_color = RB_BLACK;
			gparent->rb_color = RB_RED;
			__rb_rotate_left(gparent, root);
		}
	}

	root->rb_node->rb_color = RB_BLACK;
}

static void __rb_erase_color(rb_node_t * node, rb_node_t * parent,
			     rb_root_t * root)
{
	rb_node_t * other;

	while ((!node || node->rb_color == RB_BLACK) && node != root->rb_node)
	{
		if (parent->rb_left == node)
		{
			other = parent->rb_right;
			if (other->rb_color == RB_RED)
			{
				other->rb_color = RB_BLACK;
				parent->rb_color = RB_RED;
				__rb_rotate_left(parent, root);
				other = parent->rb_right;
			}
			if ((!other->rb_left ||
			     other->rb_left->rb_color == RB_BLACK)
			    && (!other->rb_right ||
				other->rb_right->rb_color == RB_BLACK))
			{
				other->rb_color = RB_RED;
				node = parent;
				parent = node->rb_parent;
			}
			else
			{
				if (!other->rb_right ||
				    other->rb_right->rb_color == RB_BLACK)
				{
					register rb_node_t * o_left;
					if ((o_left = other->rb_left))
						o_left->rb_color = RB_BLACK;
					other->rb_color = RB_RED;
					__rb_rotate_right(other, root);
					other = parent->rb_right;
				}
				other->rb_color = parent->rb_color;
				parent->rb_color = RB_BLACK;
				if (other->rb_right)
					other->rb_right->rb_color = RB_BLACK;
				__rb_rotate_left(parent, root);
				node = root->rb_node;
				break;
			}
		}
		else
		{
			other = parent->rb_left;
			if (other->rb_color == RB_RED)
			{
				other->rb_color = RB_BLACK;
				parent->rb_color = RB_RED;
				__rb_rotate_right(parent, root);
				other = parent->rb_left;
			}
			if ((!other->rb_left ||
			     other->rb_left->rb_color == RB_BLACK)
			    && (!other->rb_right ||
				other->rb_right->rb_color == RB_BLACK))
			{
				other->rb_color = RB_RED;
				node = parent;
				parent = node->rb_parent;
			}
			else
			{
				if (!other->rb_left ||
				    other->rb_left->rb_color == RB_BLACK)
				{
					register rb_node_t * o_right;
					if ((o_right = other->rb_right))
						o_right->rb_color = RB_BLACK;
					other->rb_color = RB_RED;
					__rb_rotate_left(other, root);
					other = parent->rb_left;
				}
				other->rb_color = parent->rb_color;
				parent->rb_color = RB_BLACK;
				if (other->rb_left)
					other->rb_left->rb_color = RB_BLACK;
				__rb_rotate_right(parent, root);
				node = root->rb_node;
				break;
			}
		}
	}
	if (node)
		node->rb_color = RB_BLACK;
}

void rb_erase(rb_node_t * node, rb_root_t * root)
{
	rb_node_t * child, * parent;
	int color;

	if (!node->rb_left)
		child = node->rb_right;
	else if (!node->rb_right)
		child = node->rb_left;
	else
	{
		rb_node_t * old = node, * left;

		node = node->rb_right;
		while ((left = node->rb_left))
			node = left;
		child = node->rb_right;
		parent = node->rb_parent;
		color = node->rb_color;

		if (child)
			child->rb_parent = parent;
		if (parent)
		{
			if (parent->rb_left == node)
				parent->rb_left = child;
			else
				parent->rb_right = child;
		}
		else
			root->rb_node = child;

		if (node->rb_parent == old)
			parent = node;
		node->rb_parent = old->rb_parent;
		node->rb_color = old->rb_color;
		node->rb_right = old->rb_right;
		node->rb_left = old->rb_left;

		if (old->rb_parent)
		{
			if (old->rb_parent->rb_left == old)
				old->rb_parent->rb_left = node;
			else
				old->rb_parent->rb_right = node;
		} else
			root->rb_node = node;

		old->rb_left->rb_parent = node;
		if (old->rb_right)
			old->rb_right->rb_parent = node;
		goto color;
	}

	parent = node->rb_parent;
	color = node->rb_color;

	if (child)
		child->rb_parent = parent;
	if (parent)
	{
		if (parent->rb_left == node)
			parent->rb_left = child;
		else
			parent->rb_right = child;
	}
	else
		root->rb_node = child;

 color:
	if (color == RB_BLACK)
		__rb_erase_color(child, parent, root);
}

extern int printf(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));


typedef struct _mm_sl_t   mm_sl_t;
typedef struct _mm_rb_t   mm_rb_t;
typedef struct _result_t  result_t;
typedef struct _param_t   param_t;

struct _mm_sl_t
{
  u_int32_t   _key;
  int         _mapped;
#if FULL_SIZE
  char        _rest[40];
#endif
};

struct _mm_rb_t
{
  u_int32_t   _key;
  int         _mapped;
  rb_node_t   _rb_node;
#if FULL_SIZE
  mm_rb_t    *_right; /* This test is generous: not maintaining the list! */
  char        _rest[40];
#endif
};

struct _result_t
{
  u_int32_t   _init_count;
  u_int32_t   _insert_count;
  u_int32_t   _delete_count;
  u_int32_t   _search_count;

  u_int64_t   _init_cycle;
  u_int64_t   _insert_cycle;
  u_int64_t   _delete_cycle;
  u_int64_t   _search_cycle;
};

struct _param_t
{
  u_int32_t   _total_keys;
  u_int32_t   _operations;
};


#define SLPC_GREATER_THAN(a,b)  ((a) >  (b))
#define SLPC_GREATER_EQUAL(a,b) ((a) >= (b))
#define SLPC_EQUAL_TO(a,b)      ((a) == (b))

#define SLPC_PARAM_NAME     vsrb
#define SLPC_KEY            u_int32_t
#define SLPC_DATA           mm_sl_t*

#define SLPC_USE_SPINLOCK   0
#define SLPC_PAD_COUNT      0
#define SLPC_COUNT_NODES    0
#define SLPC_DEBUG          0

#define SLPC_MAX_COUNT      31
#define SLPC_TARGET_SIZE    256

u_int32_t rb_skips = 0;
u_int32_t sl_skips = 0;

#include "slpc.h"

u_int32_t
any_key (param_t const *params)
{
  u_int32_t x = lrand48 ();

  x >>= 7;
  x %= params->_total_keys;

  return x;
}

int
rand_op (result_t *result, param_t const *params)
{
  double    d = drand48 ();
  double    p_d;
  u_int32_t keys;

  // Balance the number of insertions and deletions such that each
  // operation is approx 33%.  A purely random distribution leads to a
  // totally full or totally empty key space for small total_keys.
  if (d < 0.34)
    {
      return OP_SEARCH;
    }

  keys = (params->_total_keys / 2) + result->_insert_count - result->_delete_count;

  p_d = 0.34 + (0.66 * (double) keys / (double) params->_total_keys);

  if (d < p_d)
    {
      return OP_DELETE;
    }

  return OP_INSERT;
}

u_int32_t
find_rb_key (param_t const *params, mm_rb_t *region, int present)
{
  u_int32_t key;

  for (;;)
    {
      key = any_key (params);

      if (region[key]._mapped == present)
	{
	  return key;
	}

      rb_skips += 1;
    }
}

u_int32_t
find_sl_key (param_t const *params, mm_sl_t *region, int present)
{
  u_int32_t key;

  for (;;)
    {
      key = any_key (params);

      if (region[key]._mapped == present)
	{
	  return key;
	}

      sl_skips += 1;
    }
}

static __inline__
mm_rb_t* rb_search (rb_root_t *root,
		    u_int32_t  key)
{
  rb_node_t *n = root->rb_node;

  while (n)
    {
      mm_rb_t *mm = rb_entry (n, mm_rb_t, _rb_node);

      if (key < mm->_key)
	{
	  n = n->rb_left;
	}
      else if (key > mm->_key)
	{
	  n = n->rb_right;
	}
      else
	{
	  return mm;
	}
    }

  return NULL;
}

static __inline__
void rb_insert (mm_rb_t   *ins_mm,
		rb_root_t *root)
{
  rb_node_t ** p = &root->rb_node;
  rb_node_t * parent = NULL;
  mm_rb_t   * mm;

  while (*p)
    {
      parent = *p;
      mm = rb_entry(parent, mm_rb_t, _rb_node);

      if (ins_mm->_key < mm->_key)
	{
	  p = &(*p)->rb_left;
	}
      else if (ins_mm->_key > mm->_key)
	{
	  p = &(*p)->rb_right;
	}
      else
	{
	  abort ();
	}
    }

  rb_link_node (& ins_mm->_rb_node, parent, p);
  rb_insert_color (& ins_mm->_rb_node, root);
}

void
test_rb_tree (param_t const   *params,
	      mm_rb_t         *region,
	      result_t        *result,
	      rb_root_t       *root)
{
  int i;
  rb_node_t **rb_link;
  rb_node_t  *rb_parent;
  u_int64_t   start;
  u_int64_t   finish;
  u_int32_t   count = params->_total_keys / 2;

  rb_link   = & root->rb_node;
  (* root)  = RB_ROOT;
  rb_parent = NULL;

  rdtscll (start);

  /* Initialize half the key-space. */
  for (i = 0; i < params->_total_keys; i += 2)
    {
      mm_rb_t    *mm = & region[i];
      rb_node_t  *rb_insert = & mm->_rb_node;

      rb_link_node    (rb_insert, rb_parent, rb_link);
      rb_insert_color (rb_insert, root);

      rb_parent = & mm->_rb_node;
      rb_link   = & rb_parent->rb_right;

      region[i]._mapped = 1;
    }

  rdtscll (finish);

  result->_init_count = params->_total_keys / 2;
  result->_init_cycle = finish - start;

  for (i = 0; i < params->_operations; i += 1)
    {
      int op = rand_op (result, params);

      if (op == OP_INSERT)
	{
	  /* Insert */
	  u_int32_t key = find_rb_key (params, region, 0);

	  rdtscll (start);
	  rb_insert (& region[key], root);
	  rdtscll (finish);

	  region[key]._mapped = 1;
	  count += 1;

	  result->_insert_count += 1;
	  result->_insert_cycle += (finish - start);
	}
      else if (op == OP_SEARCH)
	{
	  /* Search */
	  u_int32_t key = any_key (params);
	  mm_rb_t *mm;

	  rdtscll (start);
	  mm = rb_search (root, key);
	  rdtscll (finish);

#if TEST_DEBUG
	  if (mm)
	    {
	      assert (mm->_key == key && mm->_mapped);
	    }
	  else
	    {
	      assert (! region[key]._mapped);
	    }
#endif

	  result->_search_count += 1;
	  result->_search_cycle += (finish - start);
	}
      else
	{
	  u_int32_t key = find_rb_key (params, region, 1);
	  mm_rb_t *mm;

	  /* Delete. */
	  rdtscll (start);
	  mm = rb_search (root, key);
	  rb_erase (& mm->_rb_node, root);
	  rdtscll (finish);

#if TEST_DEBUG
	  assert (mm->_key == key && mm->_mapped);
#endif
	  region[key]._mapped = 0;
	  count -= 1;

	  result->_delete_count += 1;
	  result->_delete_cycle += (finish - start);
	}
    }
}


void
test_sl_tree (param_t const   *params,
	      mm_sl_t         *region,
	      result_t        *result,
	      SLPC_ANCHOR     *root)
{
  int i;
  u_int64_t   start;
  u_int64_t   finish;

  rdtscll (start);

  /* Initialize half the key-space. */
  for (i = 0; i < params->_total_keys; i += 2)
    {
      u_int32_t key = i;
      mm_sl_t  *mm = & region[i];

      slpc_insert_vsrb (root, & key, & mm);

      mm->_mapped = 1;
    }

  rdtscll (finish);

  result->_init_count = params->_total_keys / 2;
  result->_init_cycle = finish - start;

  for (i = 0; i < params->_operations; i += 1)
    {
      int op = rand_op (result, params);
      SLPC_RESULT res;

      if (op == OP_INSERT)
	{
	  /* Insert */
	  u_int32_t key = find_sl_key (params, region, 0);
	  mm_sl_t  *mm = & region[key];

	  rdtscll (start);
	  res = slpc_insert_vsrb (root, & key, & mm);
	  rdtscll (finish);

#if TEST_DEBUG
	  assert (res == SLPC_OKAY && ! mm->_mapped);
#endif
	  region[key]._mapped = 1;

	  result->_insert_count += 1;
	  result->_insert_cycle += (finish - start);
	}
      else if (op == OP_SEARCH)
	{
	  /* Search */
	  u_int32_t key = any_key (params);
	  mm_sl_t *mm;

	  rdtscll (start);
	  res = slpc_search_vsrb (root, & key, & mm);
	  rdtscll (finish);

#if TEST_DEBUG
	  if (res == SLPC_OKAY)
	    {
	      assert (mm->_key == key && mm->_mapped);
	    }
	  else
	    {
	      assert (! region[key]._mapped);
	    }
#endif

	  result->_search_count += 1;
	  result->_search_cycle += (finish - start);
	}
      else
	{
	  u_int32_t key = find_sl_key (params, region, 1);
	  mm_sl_t *mm;

	  /* Delete. */
	  rdtscll (start);
	  res = slpc_delete_key_vsrb (root, & key, & mm);
	  rdtscll (finish);

#if TEST_DEBUG
	  assert (res == SLPC_OKAY && mm->_key == key && mm->_mapped);
#endif
	  region[key]._mapped = 0;

	  result->_delete_count += 1;
	  result->_delete_cycle += (finish - start);
	}
    }
}

double
pct_of (u_int32_t val, u_int32_t other)
{
  return 100.0 * (1.0 - (double) other / (double) val);
}

char*
avg_keys (param_t *params)
{
  static char buf[32];
  u_int32_t a = params->_total_keys / 2;

#if 0
  if (a >= 1e6)
    {
      sprintf (buf, "%.0fm", a / 1e6);
    }
  else if (a >= 1e3)
    {
      sprintf (buf, "%.0fk", a / 1e3);
    }
  else
    {
#endif
      sprintf (buf, "%d", a);
#if 0
    }
#endif

  return buf;
}

int
main(int argc, char **argv)
{
  param_t params;
  result_t sl_result;
  result_t rb_result;
  rb_root_t rb_root;
  SLPC_ANCHOR sl_root;
  SLPC_SLAB   *sl_slab;
  int i, sl_nodes, rb_overhead_per_node = 20, vm_node_size = 48;

  mm_rb_t  *rb_alloc;
  mm_sl_t  *sl_alloc;

  memset (& sl_result, 0, sizeof (sl_result));
  memset (& rb_result, 0, sizeof (rb_result));
  memset (& rb_root,   0, sizeof (rb_root));

  sl_slab = SLPC_SLAB_CREATE ("testslpc3");

  slpc_anchor_init_vsrb (& sl_root, sl_slab);

  if (argc != 3)
    {
      printf ("usage: testslpc3 MAXKEYS OPERATIONS\n");
      abort ();
    }

  params._total_keys  = atoi (argv[1]);
  params._operations  = atoi (argv[2]);

  rb_alloc = (mm_rb_t*) calloc (sizeof (mm_rb_t), params._total_keys);
  sl_alloc = (mm_sl_t*) calloc (sizeof (mm_sl_t), params._total_keys);

  for (i = 0; i < params._total_keys; i += 1)
    {
      rb_alloc[i]._key = i;
      sl_alloc[i]._key = i;
    }

  sl_nodes = SLPC_SLAB_PREALLOC (sl_slab, params._total_keys);

#if 0
  printf ("Linux 2.4.10 data structure analysis:\n\n");
  printf ("VM area count:        %d\n",
	  params._total_keys);
  printf ("VM area allocation:   %d (vm_area_struct @%d bytes)\n",
	  params._total_keys * vm_node_size,
	  vm_node_size);
  printf ("RB memory allocation: %d (+%.1f%%) (+%d bytes per vm_area_struct)\n",
	  params._total_keys * rb_overhead_per_node,
	  100.0 * (double) rb_overhead_per_node / (double) vm_node_size,
	  rb_overhead_per_node);
  printf ("SL memory allocation: %d (+%.1f%%) (%d max skip list nodes @%d bytes)\n",
	  sl_nodes * sizeof(SLPC_NODE),
	  100.0 * (sl_nodes * sizeof(SLPC_NODE)) / (params._total_keys * vm_node_size),
	  sl_nodes,
	  sizeof (SLPC_NODE));
#endif

  printf ("TREE\tAVGKEYS\t\tINSERT\tINS%%\tSEARCH\tSEA%%\tDELETE\tDEL%%\tSPACE\tSPACE%%\n");

  srand48 (TEST_SEED);

  test_rb_tree (& params, rb_alloc, & rb_result, & rb_root);

  srand48 (TEST_SEED);

  test_sl_tree (& params, sl_alloc, & sl_result, & sl_root);

#if TEST_DEBUG
  assert (rb_skips == sl_skips);
#endif

  printf ("%s\t%s\t\t%qd\t%+.1f\t%qd\t%+.1f\t%qd\t%+.1f\t%d\t%.1f\n",
	  "RB",
	  avg_keys (& params),
	  rb_result._insert_cycle / rb_result._insert_count,
	  pct_of (rb_result._insert_cycle / rb_result._insert_count,
		  sl_result._insert_cycle / sl_result._insert_count),
	  rb_result._search_cycle / rb_result._search_count,
	  pct_of (rb_result._search_cycle / rb_result._search_count,
		  sl_result._search_cycle / sl_result._search_count),
	  rb_result._delete_cycle / rb_result._delete_count,
	  pct_of (rb_result._delete_cycle / rb_result._delete_count,
		  sl_result._delete_cycle / sl_result._delete_count),
	  rb_overhead_per_node * params._total_keys,
	  100.0 * (double) rb_overhead_per_node / (double) vm_node_size);

  printf ("%s\t%s\t\t%qd\t%+.1f\t%qd\t%+.1f\t%qd\t%+.1f\t%d\t%.1f\n",
	  "SL",
	  avg_keys (& params),
	  sl_result._insert_cycle / sl_result._insert_count,
	  pct_of (sl_result._insert_cycle / sl_result._insert_count,
		  rb_result._insert_cycle / rb_result._insert_count),
	  sl_result._search_cycle / sl_result._search_count,
	  pct_of (sl_result._search_cycle / sl_result._search_count,
		  rb_result._search_cycle / rb_result._search_count),
	  sl_result._delete_cycle / sl_result._delete_count,
	  pct_of (sl_result._delete_cycle / sl_result._delete_count,
		  rb_result._delete_cycle / rb_result._delete_count),
	  sl_nodes * sizeof(SLPC_NODE),
	  100.0 * (sl_nodes * sizeof(SLPC_NODE)) / (params._total_keys * vm_node_size));

  return 0;
}

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
