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




%union 
{
	long longType;
	struct Label * Label;
	struct String * StrPtr;
	struct expr_v4 * expr;
	struct var * Var;
	/*	var_t * Var;*/
}

%type <Var> WORD N_WORD P_WORD P_RUNNER 
%type <Var> Unordered

%type <StrPtr> STRING_CONSTANT

%type <expr> if_statement 
%type <expr> Expression 
%type <expr> Object_Name
%type <expr> reiser4
%type <expr> if_statement if_Expression
%type <expr> Then_Else
%type <expr> then_operation 
%type <expr> Object_Path_Name 
%type <expr> Unordered_list
%type <expr> range_set
/*
%type <expr> Object_relative_Name 
/*
%type <expr> sl 
*/


%type <Label> tw_begin
%type <Label> asyn_begin 
%type <Label> cd_begin

%type <longType> if else 
%type <longType> range_type
%type <longType> range


%token TRANSCRASH BLANK
%token EOF 
%token L_ASSIGN L_APPEND  L_SYMLINK

%token EOL

%token SEMICOLON          /* ; */
%token COMMA              /* , */

%token PLUS               /* + */

%token R_PARENT           /* ) */
%token L_PARENT           /* ( */

%token R_SKW_PARENT       /* ] */
%token L_SKW_PARENT       /* [ */

%token R_FLX_PARENT       /* } */
%token L_FLX_PARENT       /* { */

/*%token BLANK_SLASH_BLANK  /*  /  */
/*%token BLANK              /*   */

%token SLASH_PROCESS SPACE
%token SLASH SLASH_L_PARENT ORDERED
%token SLASH_STAT
%token INV_L INV_R
%token SLASH_RANGE OFFSET OFFSET_BACK P_BYTES_WRITTEN P_BYTES_READ LAST BYTES FIRST
%token EQ NE  LE GE   LT  GT   
%token IS
%token AND
%token OR
%token P_RUNNER
%token NOT
%token IF
%token THEN ELSE
%token EXIST
%token NAME NONAME

%token WORD N_WORD P_WORD STRING_CONSTANT


%left SEMICOLON          /* ; */
%left COMMA              /* , */
%right L_ASSIGN          /* <-  */
%right L_APPEND          /* <<- */
%right L_SYMLINK         /* <->  */
%left PLUS               /* + */

%left EQ NE  LE GE   LT  GT   
%left OR
%left AND
%left NOT

%right ELSE


/*
%left BLANK_SLASH_BLANK
*/



/*
  Starting production of our grammar.
 */
%start reiser4

%%

reiser4
: Expression   EOF                                { $$ = make_do_it( $1 ); }
;


Expression
: Object_Name                                     { $$ = objToExpr( $1 );}
| STRING_CONSTANT                                 { $$ = constToExpr( $1 ); }

| Expression  PLUS  Expression                     { $$ = connect_expression( $1, $3 ); }

/* requires an IF operation to be meaningful */

| Expression EQ   Expression                      { $$ = compare_EQ_expression( $1, $3 ); }
| Expression NE   Expression                      { $$ = compare_NE_expression( $1, $3 ); }
| Expression LE   Expression                      { $$ = compare_LE_expression( $1, $3 ); }
| Expression GE   Expression                      { $$ = compare_GE_expression( $1, $3 ); }
| Expression LT   Expression                      { $$ = compare_LT_expression( $1, $3 ); }
| Expression GT   Expression                      { $$ = compare_GT_expression( $1, $3 ); }


| Expression OR   Expression                      { $$ = compare_OR_expression( $1, $3 ); }
| Expression AND  Expression                      { $$ = compare_AND_expression( $1, $3 ); }

| NOT  Expression                                 { $$ = not_expression( $2 );} 
| op_level  Expression  R_PARENT                  { $$ = $2; level_down( OP_LEVEL ); }

/* requires an IF to be meaningful? */

| EXIST  Object_Name                              { $$ = check_exist( $2 ); }


| UnordBeg Unordered_list R_SKW_PARENT            { $$ = $2; level_down( UNORDERED ); }      /* grouping: [name1 name2 [name3]]  */

| Object_Name SLASH Object_Name                   { $$ = associate( $1, $3 );}    


| tw_begin Expression R_SKW_PARENT                { $$ = end_tw_list( $1, $2 ); level_down( TW_BEGIN ); }     /*   ..tw/[a<-b;b<-c] */ 
| Expression SEMICOLON  Expression                { $$ = list_expression( $1, $3 ); }
| Expression COMMA      Expression                { $$ = list_async_expression( $1, $3 ); }


| asyn_begin Expression R_FLX_PARENT              { $$ = end_async_list( $1, $2 ); level_down( ASYN_BEGIN ); }

| if_statement                                    { $$ = $1; }
| cd_begin Expression R_PARENT                    { $$ = $2;  level_down( CD_BEGIN ); }                    /*   path_name/(a<-b;b<-c) */ 
/* the ASSIGNMENT operator and the SYMLINK operator return a value: bytes written */
|  Object_Name  L_ASSIGN        Expression        { $$ = assign( $1, $3 ); }            /*  <-  direct assign  */
|  Object_Name  L_APPEND        Expression        { $$ = assign( $1, $3 ); }            /*  <-  direct assign  */
|  Object_Name  L_ASSIGN  INV_L Expression INV_R  { $$ = assign_invert( $1, $4 ); }     /*  <-  invert assign. destination must have ..invert method  */
|  Object_Name  L_SYMLINK       Expression        { $$ = symlink( $1, $3 ); }           /*   ->  symlink   */
|  UNNAME Expression                              { $$ = contens_of( $2 ); }
|  NAME Expression                                { $$ = contens_to( $2 ); }





tw_begin
: TRANSCRASH SLASH L_SKW_PARENT                   { level_up( TW_BEGIN ); $$ = begin_tw_list( ws->ws_level ); }
;

asyn_begin
: L_FLX_PARENT                                    { level_up( ASYN_BEGIN ); $$ = begin_asynchronouse( ws->ws_level ); }
;

op_level
: L_PARENT                                        { level_up( OP_LEVEL ); }
;

cd_begin
: Object_Name SLASH_L_PARENT                      { level_up( CD_BEGIN ); $$ = make_cd( $1,  ws->ws_level ); } 
;




 
UnordBeg
: L_SKW_PARENT                                    { level_up( UNORDERED ); }
;

Unordered_list
: Unordered                                       { $$ = init_Unordered( $1 ); }
| Unordered_list  Unordered                       { $$ = add_Unordered( $1, $2 ); }

Unordered
: P_RUNNER                                        { $$ = $1; /*????????????*/}
| Object_Name                                     { $$ = $1; }

if_statement        
: if_Expression Then_Else                         { $$ = $1; make_end_label(); level_down(IF_STATEMENT);}
;

if_Expression 
: if Expression                                   { $$ = $2; goto_if_false( $1-1, $2 ); }
;

if: IF                                            { level_up( IF_STATEMENT ); $$ = reserv_label( 2 ); }
;

Then_Else
: then_operation
| then_operation else Expression %prec PLUS

then_operation
: THEN Expression                %prec PLUS       { goto_end();}
;

else
: ELSE                                            { else_lab();}
;

/* Object name begin */


Object_Name
: Object_Path_Name                                { $$ = $1 }
| Object_Path_Name  range_type                    { $$ = range_lnode ( $1, $2 ); }   /*  */
;

Object_Path_Name
: WORD                                            { $$ = pars_path_walk( $1 ); }
| SLASH_PROCESS                                   { $$ = make_proc_lnode(); }
;

/*
Object_relative_Name
: WORD                                            { $$ = pars_path_walk( $1 ); }


: sl WORD                                         { $$ = add_path( $1, pars_path_walk($2) );}
| Object_relative_Name  SLASH WORD                { $$ = add_path( $1, pars_path_walk($3) );}         
;

sl
: SLASH                                           { $$ = pars_path_init( ROOT ); }
|                                                 { $$ = pars_path_init( CURRENT ); }
*/
;



range_type
:  SLASH_RANGE SLASH L_PARENT range_set R_PARENT  {}             /* /..range/(offset<-12345,p_bytes_written<-0xff001258,...) */
|  SLASH_STAT SLASH WORD                          {}             /* /..stat/atime */
|  SLASH_STAT                                     {}             /* /..stat */
;

range_set
: range                                           {}
| range_set COMMA range                           {}
;

range
: OFFSET  L_ASSIGN N_WORD                         {}                      /*offset<-12345*/
| OFFSET_BACK  L_ASSIGN N_WORD                    {}                      /*offset_back<-12345*/
| FIRST  L_ASSIGN N_WORD                          {}                      /*first_byte<-123456*/
| LAST  L_ASSIGN N_WORD                           {}                      /*last_byte<-123456*/
| P_BYTES_WRITTEN  L_ASSIGN P_WORD                {}                      /*p_bytes_written<-0xff001258*/
| P_BYTES_READ  L_ASSIGN P_WORD                   {}                      /*p_bytes_read<-0xff001258*/
| BYTES                                           {}                      /*p_units<-bytes*/
/*| LINES                                                                       /*p_units<-lines*/
;


/* Object name end*/




%%

#define yyversion "4.0.0"

#include "parser.h"
#include "pars.cls.h"
#include "lib.c"
/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
