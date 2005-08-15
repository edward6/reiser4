/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */


#define yyversion "4.0.0"

#include "parser.h"

#include "parser.tab.h"
#include "lib.c"
//#include "pars.cls.h"
//#include "lib.c"
//#include "parser.tab.c"

#if defined(CONFIG_REISER4_FS_SYSCALL_BISON)
#include "parser.tab.c"
#else
#if defined(CONFIG_REISER4_FS_SYSCALL_YACC)
#include "parser.code.c"
#endif
#endif



/* @p_string is a command string for parsing
this function allocates work area for yacc,
initializes fields, calls yacc, free space
and call for execute the generated code */

asmlinkage long
sys_reiser4(char *p_string)
{
	long ret;
	int *Gencode;
	char * str;
	struct reiser4_syscall_w_space * work_space ;
	str=getname(p_string);
	if (!IS_ERR(str)) {
		print_pwd_count("\n--------------------\ninit");

		/* allocate work space for parser
		   working variables, attached to this call */
		if ( (work_space = reiser4_pars_init() ) == NULL ) {
			return -ENOMEM;
		}
		/* initialize fields */
		/* this field used for parsing string, one (inline) stay on begin of string*/
		work_space->ws_pline  = str;
		work_space->ws_inline = work_space->ws_pline;
		ret = yyparse(work_space);	/* parse command */
		reiser4_pars_free(work_space);
		putname(str);
		print_pwd_count("end");

	}
	else {
		ret = PTR_ERR(str);
	}
	return ret;
}

EXPORT_SYMBOL(sys_reiser4);

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
