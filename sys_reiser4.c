/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */

/* Please read parser.c and return. */

#include "reiser4.h"
#include "lnode.h"

#include "parser/parser.c"

/* @str is a command string for parsing  
this function allocates work area for yacc, 
initializes fields, calls yacc, free space
and call for execute the generated code */
asmlinkage long
sys_reiser4(char *str)
{
	long ret;
	int *Gencode;

	struct yy_r4_work_space *work_space;

	/* allocate work space for parser 
	   working variables, attached to this call */
	if ((work_space = sys_reiser4_init()) == -ENOMEM) {
		return -ENOMEM;
	}
	/* initialize fields */
	/* this two field used for parsing string, one (inline) stay on begin */
	work_space->pline = work_space->inline = str;	/*   of token, second (pline) walk to end to token                    */

	ret = yyparse(work_space);	/* parse command */
	if (ret != -ENOEM) {
		ret = execut_this_code(work_space);
	}
	sys_reiser4_free(work_space);
	return ret;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   End:
*/
