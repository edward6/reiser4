/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* Parser for the reiser4() system call */

/* Takes a string and parses it into a set of commands which are
   executed.  */

/*
     w=\$v v=\$u u=5 z=\$w+$w
               echo $z
          eval echo $z
     eval eval echo $z

eval eval eval echo $z

result is:

$w+$v
$v+$u
$u+5
5+5

tw/transcrash_33[ /home/reiser/(a <- b, c <- d) ] 

        chgrp --      changes group ownership
        chown --      changes ownership
        chmod --      changes permissions
        cp --	      copies
        dd --	      copies and converts
        df --	      shows filesystem disk usage.
        dir --	      gives brief directory listing
        du --	      shows disk usage
        ln --	      creates links
        ls --	      lists directory contents
        mkdir --      creates directories
        mkfifo --     creates FIFOs (named pipes)
        mknod --      creates special files
        mv --	      renames
        rm --	      removes (deletes)
        rmdir --      removes empty directories
        shred --      deletes a file securely
        sync --	      synchronizes memory and disk
*/

 

/* type definitions */
%union 
{
	char charType;
	expr_v4_t * expr;
	wrd_t * wrd;
	/*	String * StrPtr;*/
	//	expr_lnode_t * lnd;
	//	expr_flow_t * flw;
	//	vnode_t * vnode;
}

%type <charType> L_BRACKET R_BRACKET level_up

%type <wrd> WORD
%type <wrd> P_RUNNER 
%type <wrd> STRING_CONSTANT

%type <expr> Object_Name name  target
//%type <vnode> Object_Name name  
%type <expr> Expression 

%type <expr> if_statement 
%type <expr> reiser4
%type <expr> if_statement if_Expression if_Begin
%type <expr> then_operation 

%token TRANSCRASH
%token EOF 
%token L_ASSIGN L_APPEND  L_SYMLINK

%token EOL

%token SEMICOLON          /* ; */
%token COMMA              /* , */

%token PLUS               /* + */

%token L_BRACKET R_BRACKET
//%token R_PARENT           /* ) */
//%token L_PARENT           /* ( */

//%token R_SKW_PARENT       /* ] */
//%token L_SKW_PARENT       /* [ */

//%token R_FLX_PARENT       /* } */
//%token L_FLX_PARENT       /* { */


%token SLASH
%token INV_L INV_R
%token EQ NE  LE GE   LT  GT   
%token IS
%token AND
%token OR
%token P_RUNNER
%token NOT
%token IF
%token THEN ELSE
%token EXIST
%token NAME UNNAME
%token WORD STRING_CONSTANT
%token ROOT


%left UNNAME
%left NAME
%left NOT
%left AND
%left OR
%left EQ NE  LE GE   LT  GT   

%right L_SYMLINK         /* ->  */
%right L_APPEND          /* <<- */
%right L_ASSIGN          /* <-  */

%left PLUS               /* + */

%right ELSE
%left COMMA              /* , */

%left SEMICOLON          /* ; */
%left SLASH              /* / */
%left USLASH              /* / */




/*
For bison:
%pure_parser
*/

/*
  Starting production of our grammar.  8000.00:04:c1:14:50:07.800
 */
%start reiser4

%%

reiser4
: Expression   EOF                                { $$ = make_do_it( ws, $1 ); }
;

Expression
: Object_Name                                     { $$ = $1;}
| STRING_CONSTANT                                 { $$ = constToExpr( ws, $1 ); }
| Expression PLUS       Expression                { $$ = connect_expression( ws, $1, $3 ); }
| Expression SEMICOLON  Expression                { $$ = list_expression( ws, $1, $3 ); }
| Expression COMMA      Expression                { $$ = list_async_expression( ws, $1, $3 ); }
| level_up  Expression R_BRACKET                   { $$ =  $2 ; run_it( ws, $2 ); level_down( ws, $1, $3 );}
//| Expression            Expression                { $$ = list_unordered_expression( ws, $1, $2 ); }
| if_statement                                    { $$ = $1; level_down( ws, IF_STATEMENT ); }
                                                                            /* the ASSIGNMENT operator return a value: bytes written */
|  target  L_ASSIGN        Expression         { $$ = assign( ws, $1, $3 ); }            /*  <-  direct assign  */
|  target  L_APPEND        Expression         { $$ = assign( ws, $1, $3 ); }            /*  <-  direct assign  */
|  target  L_ASSIGN  INV_L Expression INV_R   { $$ = assign_invert( ws, $1, $4 ); }     /*  <-  invert assign. destination must have ..invert method  */
|  target  L_SYMLINK       Expression         { $$ = symlink( ws, $1, $3 ); }           /*   ->  symlink  the SYMLINK operator return a value: bytes ???? */
;

if_statement        
: if_Begin then_operation ELSE Expression %prec PLUS   { $$ = if_then_else( ws, $1, $2, $4 ); }
| if_Begin then_operation                 %prec PLUS   { $$ = if_then( ws, $1, $2) ;         }
;


if_Begin
: if if_Expression                                   { $$ = $2; }
;

if: IF                                            { level_up( ws, IF_STATEMENT ); }
;

if_Expression 
: NOT  Expression                                 { $$ = not_expression( ws, $2 ); } 
| EXIST  Expression                               { $$ = check_exist( ws, $2 ); }
| Expression EQ   Expression                      { $$ = compare_EQ_expression( ws, $1, $3 ); }
| Expression NE   Expression                      { $$ = compare_NE_expression( ws, $1, $3 ); }
| Expression LE   Expression                      { $$ = compare_LE_expression( ws, $1, $3 ); }
| Expression GE   Expression                      { $$ = compare_GE_expression( ws, $1, $3 ); }
| Expression LT   Expression                      { $$ = compare_LT_expression( ws, $1, $3 ); }
| Expression GT   Expression                      { $$ = compare_GT_expression( ws, $1, $3 ); }
| Expression OR   Expression                      { $$ = compare_OR_expression( ws, $1, $3 ); }
| Expression AND  Expression                      { $$ = compare_AND_expression( ws, $1, $3 ); }
;


then_operation
: THEN Expression                %prec PLUS       { goto_end( ws );}
;



target
: Object_Name                                     { $$ = prepare_target( ws, 41 );}



Object_Name 
: WORD                                            { $$ = pars_lookup_curr( ws, $1 ) ; }
| SLASH WORD                     %prec ROOT       { $$ = pars_lookup_root( ws, $2 ) ; }
| Object_Name SLASH WORD                          { $$ = pars_lookup( ws, $1, $3 ) ; }
;

//name
//: WORD                                            { $$ = lookup_word( ws, $1 ); }
//;

level_up
: L_BRACKET                                        { $$=level_up( ws, $1 ); set_curr_path( ws ) }


//Object_Name 
//: begin_from name                                 { $$ = $2 ; }  /*$$=?????*/
//Object_Name 
//: name                                            { $$ = pars_lookup_curr( ws, $1 ) ; }
//| SLASH name                     %prec ROOT       { $$ = pars_lookup_root( ws, $2 ) ; }
//| Object_Name SLASH name                          { $$ = pars_lookup( ws, $1, $3 ) ; }
//;

//name
//: WORD                                            { $$ = lookup_word( ws, $1 ); }
//;
//| Object_Name SLASH name                          { $$ = $3 ; }  /*$$=?????*/
//;

//begin_from
//: SLASH                                           { set_curr_path_to( ws, BEGIN_FROM_ROOT ); }    /* lup */
//;                                                 { set_curr_path_to( ws, BEGIN_FROM_CURRENT ); } /* lup */

//name
//: WORD                                            { $$ = set_curr_path( ws, pars_path_walk( ws, $1 ) ); }    /* change current path to $1 */  /*$$=?????*/
//| level_up  Expression R_BRACKET                   { $$ = set_curr_path( ws, $2 );  /*$$=?????*/; level_down( ws, $1, $3 );}  /*$$=?????*/
//;

//level_up
//: L_BRACKET                                        { $$=level_up( ws, $1 ); }


%%


#define yyversion "4.0.0"
#include "pars.cls.h"
#include "parser.tab.c"
#include "pars.yacc.h"
#include "lib.c"

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   End:
*/
