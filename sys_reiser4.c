/* System call for accessing enhanced semantics of the Reiser Filesystem Version 4 (reiser4). */

/* This system call feeds a string to parser.c, parser.c converts the
   string into a set of commands which are executed, and then this
   system call returns after the completion of those commands. */

/* Please read parser.c and return. */

/* Mr. Demidov, please implement by the end of March. */


#include "parser/parser.h"
#include "parser/y.tab.c"


int yywrap()
{
    return 1;
}

freeSpace * freeSpaceAlloc()
{
	freeSpace * fs;
	if ( ( fs = ( freeSpace * ) kmalloc( sizeof( freeSpace ) ) ) != 0 )
		{
			fs->freeSpace_next = NULL;
			fs->freeSpaceSize  = FREESPACESIZE;
			fs->freeSpace      = fs->freeSpaceBase;
		}
	return fs;
}

wrdtab * WrdTabAlloc()
{
	wrdtab * wrd;

	if ( ( wrd = ( wrdtab *    ) kmalloc( sizeof( wrdtab    ) ) ) != 0 )
		{
			wrd->wrd_next   = NULL;
			wrd->wrdTabSize = WRDTABSIZE;
			wrd->wrdTabLast = 0;
		}
	return wrd;
}

vartab * VarTabAlloc()
{
	vartab * var;
	if ( ( var = ( vartab *    ) kmalloc( sizeof( vartab    ) ) ) != 0 )
		{
			var->Var_next   = NULL;
			var->VarTabSize = VARTABSIZE;
			var->VarTabLast = 0;
		}
	return var;
}

/* allocate next part of table StrTab */
strtab * StrTabAlloc()
{
	strtab * str;
	if ( ( str = ( strtab  *   ) kmalloc( sizeof( strtab   ) ) ) != 0 )
		{
			str->Str_next   = NULL;
			str->StrTabSize = STRTABSIZE;
			str->StrTabLast = 0;
		}
	return str;
}


/* @str is a command string for parsing  
this function allocates work area for yacc, 
initializes fields, calls yacc, free space
and call for execute the generated code */
asmlinkage long  sys_reiser4(char * str)
{
	long ret;
	int * Gencode;

	struct yy_r4_work_space * work_space;

                                                            /* allocate work space for parser 
							       working variables, attached to this call */
	if ( ( work_space = kmalloc( sizeof( struct yy_r4_work_space ),0 ) )==0 )
		{
			return -ENOMEM;
		}
	work_space->ws_yystacksize = MAXLEVELCO; /* must be 500 by default */
	work_space->ws_yymaxdepth  = MAXLEVELCO; /* must be 500 by default */
	
	                                                    /* initialize fields */
	                                                    /* this two field used for parsing string, one (inline) stay on begin */
	work_space->pline  =  work_space->inline = str;     /*   of token, second (pline) walk to end to token                    */





	                                                    /* allocate first part of working tables and assign to headers */
	work_space->freeSpHead = freeSpaceAlloc();
	work_space->WrdTabHead = WrdTabAlloc();
	work_space->VarTabHead = StrTabAlloc();
	work_space->StrTabHead = VarTabAlloc();


	if (work_space->freeSpHead && work_space->WrdTabHead && work_space->VarTabHead && work_space->StrTabHead)
		{
			ret = yyparse(work_space);                 /* parse command */
			Gencode = getGeneratedCode(work_space);
		}
	else
		{
			ret= -ENOMEM;
		}
	if (work_space->freeSpHead)
		{
			freeList(work_space->freeSpHead);
		}
	if (work_space->WrdTabHead)
		{
			freeList(work_space->WrdTabHead);
		}
	if (work_space->VarTabHead)
		{
			freeList(work_space->VarTabHead);
		}
	if (work_space->StrTabHead)
		{
			freeList(work_space->StrTabHead);
		}
	free(work_space);

	                      			                                     /* execute list of commands 
					                                              of course, we can return address of this generated code 
										      or execute it by next one system call */
	if ( ret != -ENOEM )
		{
			ret = execut_this_code(Gencode);
		}	
	return ret;
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
