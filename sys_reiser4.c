 /* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */


#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include <asm-generic/errno.h>

#ifdef CONFIG_REISER4_FS_SYSCALL

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "kassign.h"
#include "coord.h"
#include "seal.h"
#include "plugin/item/item.h"
#include "plugin/security/perm.h"
#include "plugin/plugin.h"
#include "plugin/object.h"
#include "znode.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"
#include "reiser4.h"

#include "lnode.h"

#include "parser/parser.h"

//#include "parser/parser.tab.h"
//#include "parser/pars.cls.h"
//#include "parser/pars.yacc.h"
//#include "parser/lib.c"

#define YYREISER4_DEF

#include "parser/parser.code.c"


/* @p_string is a command string for parsing  
this function allocates work area for yacc, 
initializes fields, calls yacc, free space
and call for execute the generated code */

asmlinkage long
sys_reiser4(char *p_string)
{
	long ret;
	int *Gencode;

	struct reiser4_syscall_w_space *work_space;

	printk("sys_reiser4.parser(0) command string is ------>%s<--------\n",p_string);

	/* allocate work space for parser 
	   working variables, attached to this call */

	if ((work_space = sys_reiser4_init()) == NULL) {
		return -ENOMEM;
	}

	/* initialize fields */
	/* this field used for parsing string, one (inline) stay on begin of token*/
	work_space->ws_pline = p_string;

	ret = yyparse(work_space);	/* parse command */
	//	if (ret != -1 /*-ENOMEM*/) {
	//		ret = execute_this_code(work_space);
	//	}
	sys_reiser4_free(work_space);
	return ret;

	return 0;
}

#else

asmlinkage long
sys_reiser4(void *p_string)
{
	return -ENOSYS;
}

#endif

#include "debug.h"
#include "lnode.h"

#include <linux/ctype.h>

#define PROTO_LEVELS (100)

typedef enum proto_token_type {
	TOKEN_NAME,
	TOKEN_SLASH,
	TOKEN_ASSIGNMENT,
	TOKEN_LPAREN,
	TOKEN_RPAREN,
	TOKEN_STRING,
	TOKEN_NUMBER,
	TOKEN_LESS_THAN,
	TOKEN_GREATER_THAN,
	TOKEN_EQUAL_TO,
	TOKEN_INVALID
} proto_token_type_t;

typedef struct proto_token {
	proto_token_type_t  type;
	int                 pos;
	union {
		struct {
			int len;
		} name, string;
		struct {
			long val;
		} number;
	} u;
} proto_token_t;

typedef enum proto_val_type {
	VAL_DENTRY,
	VAL_NUMBER,
	VAL_STRING,
	VAL_ERROR,
	VAL_VOID
} proto_val_type_t;

typedef struct proto_val {
	proto_val_type_t type;
	union {
		struct dentry *dentry;
		long   number;
		char  *string;
		struct {
			char  *error;
			int    error_pos;
		} error;
	} u;
} proto_val_t;

typedef struct proto_level {
	const char    *error;
	int            error_pos;
	proto_val_t    val;
	struct dentry *cur;
} proto_level_t;

typedef enum proto_flags {
	CTX_PARSE_ERROR = (1 << 0)
} proto_flags_t;

typedef struct proto_ctx {
	__u32          flags;
	const char    *command;
	int            len;
	int            pos;
	int            depth;
	proto_level_t *level;
	struct dentry *cwd;
	struct dentry *root;
} proto_ctx_t;

static int parse_exp(proto_ctx_t *ctx);

#define PTRACE(ctx, format, ... )						\
({										\
	ON_TRACE(TRACE_PARSE, "parse: %02i at %i[%c]: %s: " format "\n",	\
		 ctx->depth,							\
		 ctx->pos, char_at(ctx, ctx->pos) ? : '.',			\
		 __FUNCTION__ , __VA_ARGS__);					\
})

static char char_at(proto_ctx_t *ctx, int pos)
{
	if (pos < ctx->len)
		return ctx->command[pos];
	else
		return 0;
}

static proto_level_t *get_level(proto_ctx_t *ctx)
{
	assert("nikita-3233", ctx->depth < PROTO_LEVELS);
	return &ctx->level[ctx->depth];
}

static proto_val_t *get_val(proto_ctx_t *ctx)
{
	return &get_level(ctx)->val;
}

static void proto_val_move(proto_val_t *dst, proto_val_t *src)
{
	xmemmove(dst, src, sizeof *dst);
	src->type = VAL_VOID;
}

static void proto_val_put(proto_val_t *val)
{
	switch(val->type) {
	case VAL_DENTRY:
		if (val->u.dentry != NULL) {
			dput(val->u.dentry);
			val->u.dentry = NULL;
		}
		break;
	case VAL_STRING:
		if (val->u.string != NULL) {
			kfree(val->u.string);
			val->u.string = NULL;
		}
		break;
	case VAL_NUMBER:
	case VAL_ERROR:
	case VAL_VOID:
		break;
	}
	val->type = VAL_VOID;
}

static void proto_val_up(proto_ctx_t *ctx)
{
	assert("nikita-3236", ctx->depth > 0);
	proto_val_move(&ctx->level[ctx->depth - 1].val, get_val(ctx));
}

static void post_error(proto_ctx_t *ctx, char *error)
{
	proto_val_t *val;

	PTRACE(ctx, "%s", error);

	get_level(ctx)->error = error;
	get_level(ctx)->error_pos = ctx->pos;
	ctx->flags |= CTX_PARSE_ERROR;
	val = get_val(ctx);
	proto_val_put(val);
	val->type = VAL_ERROR;
	val->u.error.error = error;
	val->u.error.error_pos = ctx->pos;
}

static proto_token_type_t extract_string(proto_ctx_t *ctx, int *outpos,
					 proto_token_t *token)
{
	int len;
	int pos;

	pos = *outpos;
	for (len = 0; ; ++ len, ++ pos) {
		char ch;

		ch = char_at(ctx, pos);
		if (ch == '"') {
			token->type = TOKEN_STRING;
			token->u.string.len = len;
			*outpos = pos + 1;
			PTRACE(ctx, "%i", len);
			break;
		} else if (ch == 0) {
			token->type = TOKEN_INVALID;
			post_error(ctx, "eof in string");
			break;
		}
	}
	return token->type;
}

static proto_token_type_t extract_number(proto_ctx_t *ctx, int *pos,
					 proto_token_t *token)
{
	char ch;

	ch = char_at(ctx, *pos);

	if (!isdigit(ch)) {
		token->type = TOKEN_INVALID;
		-- *pos;
	} else {
		long val;

		val = (ch - '0');
		++ *pos;
		token->type = TOKEN_NUMBER;
		for (;; ++ *pos) {

			ch = char_at(ctx, *pos);
			if (!isdigit(ch))
				break;
			val = val * 10 + (ch - '0');
		}
		PTRACE(ctx, "%li", val);
		token->u.number.val = val;
	}
	return token->type;
}

static proto_token_type_t extract_name(proto_ctx_t *ctx, int *pos,
				       proto_token_t *token)
{
	int len;

	for (len = 0;  ; ++ *pos, ++ len) {
		char ch;

		ch = char_at(ctx, *pos);
		if (isspace(ch))
			break;
		if (ch == 0)
			break;
		if (strchr("/<+", ch) != NULL)
			break;
	}
	if (len == 0) {
		token->type = TOKEN_INVALID;
	} else {
		token->type = TOKEN_NAME;
		token->u.name.len = len;
		PTRACE(ctx, "%i", len);
	}
	return token->type;
}

static proto_token_type_t next_token(proto_ctx_t *ctx,
				     proto_token_t *token)
{
	proto_token_type_t ttype;
	int pos;

	for (; isspace(char_at(ctx, ctx->pos)) ; ++ ctx->pos)
	{;}

	pos = token->pos = ctx->pos;
	switch (char_at(ctx, pos ++)) {
	case '/':
		ttype = TOKEN_SLASH;
		break;
	case '(':
		ttype = TOKEN_LPAREN;
		break;
	case ')':
		ttype = TOKEN_RPAREN;
		break;
	case '"':
		ttype = extract_string(ctx, &pos, token);
		break;
	case '<':
		if (char_at(ctx, pos + 1) == '=') {
			ttype = TOKEN_ASSIGNMENT;
			++ pos;
		} else
			ttype = TOKEN_LESS_THAN;
		break;
	case 0:
		ttype = TOKEN_INVALID;
		post_error(ctx, "eof");
		-- pos;
		break;
	case '#':
		ttype = extract_number(ctx, &pos, token);
		break;
	default:
		-- pos;
		ttype = extract_name(ctx, &pos, token);
		break;
	}
	token->type = ttype;
	ctx->pos = pos;
	PTRACE(ctx, "%i", ttype);
	return ttype;
}

static void back_token(proto_ctx_t *ctx, proto_token_t *token)
{
	assert("nikita-3237", ctx->pos >= token->pos);
	/* it is -that- simple */
	ctx->pos = token->pos;
}

static void ctx_done(proto_ctx_t *ctx)
{
	if (ctx->level != NULL) {
		kfree(ctx->level);
		ctx->level = NULL;
	}
	if (ctx->cwd != NULL) {
		dput(ctx->cwd);
		ctx->cwd = NULL;
	}
	if (ctx->root != NULL) {
		dput(ctx->root);
		ctx->root = NULL;
	}
}

static int ctx_init(proto_ctx_t *ctx, const char *command)
{
	int result;

	xmemset(ctx, 0, sizeof *ctx);
	ctx->command = command;
	ctx->len = strlen(command);
	ctx->level = kmalloc(sizeof (ctx->level[0]) * PROTO_LEVELS,
			     GFP_KERNEL);
	xmemset(ctx->level, 0, sizeof (ctx->level[0]) * PROTO_LEVELS);
	if (ctx->level != NULL) {

		read_lock(&current->fs->lock);
		ctx->cwd  = dget(current->fs->pwd);
		ctx->root = dget(current->fs->root);
		read_unlock(&current->fs->lock);

		result = 0;
	} else
		result = -ENOMEM;
	if (result != 0)
		ctx_done(ctx);
	return result;
}

static void inlevel(proto_ctx_t *ctx)
{
	if (ctx->depth >= PROTO_LEVELS - 1) {
		/* handle stack overflow */
	}
	++ ctx->depth;
	xmemset(get_level(ctx), 0, sizeof *get_level(ctx)); 
	get_val(ctx)->type = VAL_VOID;
}

static void exlevel(proto_ctx_t *ctx)
{
	assert("nikita-3235", ctx->depth > 0);
	proto_val_put(get_val(ctx));
	if (get_level(ctx)->cur != NULL) {
		dput(get_level(ctx)->cur);
		get_level(ctx)->cur = NULL;
	}
	-- ctx->depth;
}

static void build_string_val(proto_ctx_t *ctx, 
			     proto_token_t *token, proto_val_t *val)
{
	int len;

	assert("nikita-3238", 
	       token->type == TOKEN_STRING || token->type == TOKEN_NAME);

	len = token->u.string.len;
	val->u.string = kmalloc(len + 1, GFP_KERNEL);
	if (val->u.string != NULL) {
		strncpy(val->u.string, ctx->command + token->pos, len);
		val->u.string[len] = 0;
	}
}

static struct dentry *lookup(struct dentry * parent, const char * name)
{
	unsigned long hash;
	struct qstr qname;
	struct dentry *result;
	unsigned int c;

	qname.name = name;
	c = *(const unsigned char *)name;

	hash = init_name_hash();
	do {
		name++;
		hash = partial_name_hash(c, hash);
		c = *(const unsigned char *)name;
	} while (c != 0);
	qname.len = name - (const char *) qname.name;
	qname.hash = end_name_hash(hash);

	result = __d_lookup(parent, &qname);
	if (result == NULL) {
		struct inode *dir;

		dir = parent->d_inode;
		down(&dir->i_sem);
		result = d_lookup(parent, &qname);
		if (result == NULL) {
			struct dentry * new;

			new = d_alloc(parent, &qname);
			if (new != NULL) {
				result = dir->i_op->lookup(dir, new);
				if (result != NULL)
					dput(new);
				else if (new->d_inode != NULL)
					result = new;
				else {
					dput(new);
					result = ERR_PTR(-ENOENT);
				}
			} else
				result = ERR_PTR(-ENOMEM);
		}
		up(&dir->i_sem);
	}
	return result;
}

static int parse_path(proto_ctx_t *ctx)
{
	int result;
	int done;

	proto_token_t  token;

	struct dentry *parent;
	struct dentry *child;

	parent = dget(get_level(ctx)->cur);

	done = result = 0;
	do {
		next_token(ctx, &token);

		PTRACE(ctx, "%i", token.type);

		switch (token.type) {
		case TOKEN_NAME: {
			proto_val_t name;

			build_string_val(ctx, &token, &name);
			child = lookup(parent, name.u.string);
			if (!IS_ERR(child)) {
				dput(parent);
				parent = child;
			} else {
				result = PTR_ERR(child);
				if (result == -ENOENT)
					post_error(ctx, "not found");
				else
					post_error(ctx, "lookup failure");
			}
			proto_val_put(&name);
			break;
		}
		case TOKEN_SLASH:
			continue;
		case TOKEN_LPAREN:
			back_token(ctx, &token);
			inlevel(ctx);
			get_level(ctx)->cur = dget(parent);
			result = parse_exp(ctx);
			if (result == 0) {
				if (get_val(ctx)->type == VAL_DENTRY) {
					dput(parent);
					parent = dget(get_val(ctx)->u.dentry);
				} else {
					post_error(ctx, "lnode expected");
					result = -EINVAL;
				}
			}
			exlevel(ctx);
			break;
		case TOKEN_STRING:
		case TOKEN_NUMBER:
		case TOKEN_ASSIGNMENT:
		case TOKEN_RPAREN:
		case TOKEN_LESS_THAN:
		case TOKEN_GREATER_THAN:
		case TOKEN_EQUAL_TO:
			back_token(ctx, &token);
			done = 1;
			break;
		case TOKEN_INVALID:
		default:
			post_error(ctx, "confused");
			result = -EINVAL;
			done = 1;
			break;
		}
	} while(result == 0 && !done);

	if (result == 0) {
		proto_val_t *val;

		val = get_val(ctx);
		val->type = VAL_DENTRY;
		val->u.dentry = parent;
	} else
		dput(parent);
	return result;
}

int proto_assign(struct dentry *lefthand, struct dentry *righthand)
{
	return 0;
}

static int parse_exp_tail(proto_ctx_t *ctx)
{
	proto_val_t   *lhs;
	proto_token_t  token;
	int result;
	int done;

	assert("nikita-3239", ctx->depth > 0);

	lhs = get_val(ctx);
	assert("nikita-3240", lhs->type == VAL_DENTRY);

	done = result = 0;
	do {
		next_token(ctx, &token);

		PTRACE(ctx, "%i", token.type);

		switch(token.type) {
		case TOKEN_ASSIGNMENT: {
			/* a/b <- c/d */
			inlevel(ctx);
			result = parse_exp(ctx);
			if (result == 0) {
				proto_val_t *rhs;

				rhs = get_val(ctx);
				if (rhs->type == VAL_DENTRY) {
					result = proto_assign(lhs->u.dentry, 
							      rhs->u.dentry);
				} else {
					post_error(ctx, "lnode expected");
					result = -EINVAL;
				}
			}
			exlevel(ctx);
			break;
		}
		case TOKEN_RPAREN:
			/* a/b ) c/d */
			back_token(ctx, &token);
			break;
		case TOKEN_NAME:
			/* a/b c/d */
		case TOKEN_SLASH:
			/* a/b / c/d */
		case TOKEN_LPAREN:
			/* a/b ( c/d */
		case TOKEN_LESS_THAN:
		case TOKEN_GREATER_THAN:
		case TOKEN_EQUAL_TO:
		case TOKEN_STRING:
			/* a/b "foo!" c/d */
		case TOKEN_NUMBER:
		case TOKEN_INVALID:
		default:
			post_error(ctx, "unexpected");
			result = -EINVAL;
			break;
		}
	} while(result == 0 && !done);
	return result;
}

static int parse_exp(proto_ctx_t *ctx)
{
	proto_token_t  token;
	int            result;
	proto_level_t *level;

	level = get_level(ctx);

	result = 0;
	next_token(ctx, &token);

	PTRACE(ctx, "%i", token.type);

	switch(token.type) {
	case TOKEN_NAME:
		back_token(ctx, &token);
		inlevel(ctx);
		get_level(ctx)->cur = dget(level->cur);
		result = parse_path(ctx);
		proto_val_up(ctx);
		exlevel(ctx);
		if (result == 0)
			result = parse_exp_tail(ctx);
		break;
	case TOKEN_SLASH:
		inlevel(ctx);
		get_level(ctx)->cur = dget(ctx->root);
		result = parse_path(ctx);
		proto_val_up(ctx);
		exlevel(ctx);
		if (result == 0)
			result = parse_exp_tail(ctx);
		break;
	case TOKEN_LPAREN: {
		proto_token_t rparen;

		inlevel(ctx);
		result = parse_exp(ctx);
		proto_val_up(ctx);
		exlevel(ctx);
		if (next_token(ctx, &rparen) != TOKEN_RPAREN) {
			post_error(ctx, "expecting `)'");
			result = -EINVAL;
		}
		break;
	}
	case TOKEN_STRING: {
		build_string_val(ctx, &token, get_val(ctx));
		break;
	}
	case TOKEN_NUMBER: {
		proto_val_t *val;

		val = get_val(ctx);
		val->type = VAL_NUMBER;
		val->u.number = token.u.number.val;
		break;
	}
	case TOKEN_ASSIGNMENT:
	case TOKEN_RPAREN:
	case TOKEN_LESS_THAN:
	case TOKEN_GREATER_THAN:
	case TOKEN_EQUAL_TO:
	case TOKEN_INVALID:
	default:
		post_error(ctx, "huh");
		result = -EINVAL;
		break;
	}
	return result;
}

static int execute(proto_ctx_t *ctx)
{
	int result;

	inlevel(ctx);
	get_level(ctx)->cur = dget(ctx->cwd);
	result = parse_exp(ctx);
	exlevel(ctx);
	assert("nikita-3234", ctx->depth == 0);
	if (result == 0 && char_at(ctx, ctx->pos) != 0)
		post_error(ctx, "garbage after expression");

	if (ctx->flags & CTX_PARSE_ERROR) {
		int i;

		printk("Syntax error in ``%s''\n", ctx->command);
		for (i = PROTO_LEVELS - 1; i >= 0; --i) {
			proto_level_t *level;

			level = &ctx->level[i];
			if (level->error != NULL) {
				printk("    %02i: %s at %i\n", 
				       i, level->error, level->error_pos);
			}
		}
		result = -EINVAL;
	}
	return result;
}

asmlinkage long proto_reiser4(const char __user * command)
{
	int    result;
	char * inkernel;

	inkernel = getname(command);
	if (!IS_ERR(inkernel)) {
		proto_ctx_t ctx;

		result = ctx_init(&ctx, inkernel);
		if (result == 0) {
			reiser4_current_trace_flags |= TRACE_PARSE;
			result = execute(&ctx);
			reiser4_current_trace_flags &= ~TRACE_PARSE;
			ctx_done(&ctx);
		}
		putname(inkernel);
	} else
		result = PTR_ERR(inkernel);
	return result;
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
