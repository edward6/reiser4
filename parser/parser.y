/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/* Parser for the reiser4() system call */


%debug
%defines
%locations
%pure-parser

/* type definitions */
%union 
{
	long charType;
	expr_v4_t * expr;
	wrd_t * wrd;
//	val_range_t * rng;
	rng_command_t * rng_com;
}

%{
//#include "lib.c"
%}


%type <charType> L_BRACKET R_BRACKET level_up reiser4 assign

%type <wrd> WORD
%type <wrd> P_RUNNER

%type <wrd> STRING_CONSTANT

%type <expr> Object_Name o_name name
%type <expr> target

%type <charType> rng_type rng_units rng_key

%type <expr> range range_expression

%type <rng_com> range_command

%type <expr> begin_from
%type <expr> Expression

%type <expr> if_statement if_Expression if_Begin
%type <expr> then_operation 

%token TRANSCRASH
%token SEMICOLON          /* ; */
%token COMMA              /* , */
%token L_ASSIGN L_APPEND  L_SYMLINK
%token PLUS               /* + */
%token L_BRACKET R_BRACKET
%token SLASH PROCESS
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
%token NAME UNNAME NAMED
%token WORD STRING_CONSTANT
%token ROOT

%token RANGE OFFSET LENGTH

%token LASTBYTE FIRSTBYTE
%token UNITS BYTE LINE ITEM
%token CUT ZERRO TRANSPARENT




%left SEMICOLON COMMA
%right L_SYMLINK L_APPEND L_ASSIGN 

%left PLUS               /* + */
%left UNNAME NAME
%left EQ NE  LE GE   LT  GT   
%left NOT AND OR

%right ELSE

%left SLASH              /* / */

/*
For bison:
%pure_parser
*/

/*
  Starting production of our grammar.
 */
%start reiser4

%%

reiser4
    : Expression                                      { $$ = $1; /* free_expr( ws, $1 );*/ }
;

Expression
    : Object_Name                                     { $$ = $1;}
    | STRING_CONSTANT                                 { $$ = const_to_expr( ws, $1 ); }
    | UNNAME Expression                               { $$ = unname( ws, $2 ); }
    | NAME Expression                                 { $$ = name( ws, $2 ); }
//    | TRANSCRASH Expression                           { $$ = transcrash( ws, $2 ); }
    | Expression PLUS       Expression                { $$ = concat_expression( ws, $1, $3 ); }
    | Expression SEMICOLON  Expression                { $$ = list_expression( ws, $1, $3 ); }
    | Expression COMMA      Expression                { $$ = list_async_expression( ws, $1, $3 ); }
    | if_statement                                    { $$ = $1; level_down( ws, IF_STATEMENT, IF_STATEMENT ); }
                                                                            /* the ASSIGNMENT operator return a value: the expression of target */
    |  target  assign       Expression %prec L_ASSIGN { $$ = assign( ws, $1, $3, $2 ); }            /*  <-  direct assign  */
//    |  target  L_ASSIGN        Expression             { $$ = assign( ws, $1, $3, O_TRUNC ); }            /*  <-  direct assign  */
//    |  target  L_APPEND        Expression             { $$ = assign( ws, $1, $3, O_APPEND ); }            /*  <-  direct assign  */
//    |  target  L_ASSIGN  INV_L Expression INV_R       { $$ = assign_invert( ws, $1, $4 ); }     /*  <-  invert assign. destination must have ..invert method  */
    |  target  assign INV_L Expression INV_R %prec L_ASSIGN       { $$ = assign_invert( ws, $1, $4, $2 ); }     /*  <-  invert assign. destination must have ..invert method  */
    |  target  L_SYMLINK       Expression             { $$ = symlink( ws, $1, $3 ); }           /*   ->  symlink  the SYMLINK operator return a value: bytes ???? */

//    | named_level_up Expression R_BRACKET             { $$ = named_level_down( ws, $1, $2, $3 ); }
//    | level_up  Expression R_BRACKET                   { $$ = $2; level_down( ws, $1, $3 );}
;
//| Expression            Expression                { $$ = list_unordered_expression( ws, $1, $2 ); }


assign
    : L_ASSIGN                                             {$$ = O_TRUNC; }
    | L_APPEND                                             {$$ = O_APPEND;}


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
    : Object_Name                                     { $$ = $1;}
    | o_name NAMED Object_Name                        { $$ = target_name( $1, $3 );}
;

Object_Name
    : o_name                                          { $$ = $1; }
    | o_name SLASH range                              { $$ = range2expr( ws, $1, $3);}
    | SLASH PROCESS SLASH range                       { $$ = pars_lookup_process( ws, $4 ) ; }
    ;

o_name 
    : begin_from name                 %prec ROOT       { $$ = pars_expr( ws, $1, $2 ) ; }
    | o_name SLASH name                                { $$ = pars_expr( ws, $1, $3 ) ; }
;

begin_from
    : SLASH                                            { $$ = pars_lookup_root( ws ) ; }
    |                                                  { $$ = pars_lookup_curr( ws ) ; }
;

name
    : WORD                                             { $$ = lookup_word( ws, $1 ); }
    | level_up  Expression R_BRACKET                   { $$ = $2; level_down( ws, $1, $3 );}
    ;

range
    : RANGE SLASH level_up range_expression R_BRACKET  { $$ = $4; level_down( ws, $3, $5 );}
    ;

range_expression
    : range_command                                    { $$ = new_range(ws, $1); }
    | range_expression range_command                   { $$ = add_range(ws, $1, $2); }
    ;

range_command
    : rng_key STRING_CONSTANT                          { $$ = range_expression2command(ws, $1, $2);}
    | UNITS rng_units                                  { $$ = range_units_type(ws, COMMAND_UNITS, $2);}
    | rng_type                                         { $$ =  range_units_type(ws, COMMAND_TYPE, $1);}
    ;

rng_key
    : OFFSET                                           { $$ = COMMAND_OFFSET;}
    | LASTBYTE                                         { $$ = COMMAND_LAST;}
    | FIRSTBYTE                                        { $$ = COMMAND_FIRST;}
    | LENGTH                                           { $$ = COMMAND_LEN;}

rng_units
    : BYTE                                             { $$ = UNITS_BYTE;}
    | LINE                                             { $$ = UNITS_LINE;}
    | ITEM                                             { $$ = UNITS_ITEM; }
    ;

rng_type
    : CUT                                              { $$ = RANGE_CUT;}
    | ZERRO                                            { $$ = RANGE_ZERRO;}
    | TRANSPARENT                                      { $$ = RANGE_TRANSPARENT;}
    ;

level_up
    : L_BRACKET                                        { $$ = $1; level_up( ws, $1 ); } 
;

//named_level_up
//    : Object_Name NAMED level_up                   { $$ = $1; level_up_named( ws, $1, $3 );} 
//;

%%



/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   End:
*/
