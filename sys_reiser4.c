/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */

/* Please read parser.c and return. */

/* Mr. Demidov, please implement by the end of March. */


/* Issues of VFS consistency:

It seems most issues of consistency can be resolved by locking the parent directory.



 */

//#include "y.tab.c"

#include "parser/parser.h"

struct yy_r4_work_space * work_space;

int yywrap()
{
    return 1;
}

int sys_reiser4(char * str)
{

  work_space = kmalloc();
  
  work_space->pline  =  work_space->inline = str;
  




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
