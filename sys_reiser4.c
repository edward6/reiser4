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

/* comment me */
int sys_reiser4(char * str)
{
/* comment me */
  work_space = kmalloc();
  
/* comment me */
  work_space->pline  =  work_space->inline = str;
  


/* comment me */
	struct nameidata * nd;

/* comment me */
	int	ws_yyerrco;
	int	ws_level;              /* current level            */
	int	ws_labco;              /* current label            */
	int	ws_errco;              /* number of errors         */
	int	ws_strco;              /* number of entries in tptr*/
	int	ws_varco;              /* number of variables      */
	int	ws_varsol;             /* begin number of variables*/

	struct var   ** Var;
	struct streg ** Str;
	char         ** WrdTab;

	int ws_yydebug;
	int ws_yynerrs;
	int ws_yyerrflag;
	int ws_yychar;
	short * ws_yyssp;
	YYSTYPE * ws_yyvsp;
	YYSTYPE ws_yyval;
	YYSTYPE ws_yylval;
	short * ws_yyss;             /*[YYSTACKSIZE]*/
	YYSTYPE * ws_yyvs;           /*[YYSTACKSIZE]*/
	//	short yyss[YYSTACKSIZE];
	//	YYSTYPE yyvs[YYSTACKSIZE];
	int  ws_yystacksize; /*500*/
	int  ws_yymaxdepth ; /*500*/







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
