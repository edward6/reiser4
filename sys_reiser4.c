/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */

/* Please read parser.c and return. */

/* Mr. Demidov, please implement by the end of March. */


/* Issues of VFS consistency:

It seems most issues of consistency can be resolved by locking the parent directory.



 */

#include "parser/parser.h"
#include "parser/y.tab.c"


int yywrap()
{
    return 1;
}

/* @str is a command string for parsing  */
int sys_reiser4(char * str)
{
struct yy_r4_work_space * work_space;

                                                            /* allocate work space for parser 
							       working variables, dependens of task */
	work_space = kmalloc( sizeof( struct yy_r4_work_space ),0 );
	
	                                                    /* initialize fields */
	                                                    /* this two field used for parsing string, one (inline) stay on begin */
	work_space->pline  =  work_space->inline = str;     /*   of token, second (pline) walk to end to token                   */
  
                                                            /* this is copy of work space structure for remember to initialize fields */
	work_space->ws_yystacksize = MAXLEVELCO; /*500*/
	work_space->ws_yymaxdepth  = MAXLEVELCO; /*500*/
	
	i=yyparse(work_space);
	
	return 0;
}


/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
