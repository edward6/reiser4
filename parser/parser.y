/* Parser for the reiser4() system call */

/* Takes a string and parses it into a set of commands which are
   executed.  */

/*

The innovative features of our syntax will be implemented in a later
version than Reiser4.  (These features include the decompounding of
tuples into / and [], the generalization of keywords into keyobjects,
and the introduction of closure.  If you want to read about them
please go to www.namesys.com/future_vision.html.)  Most of what is
going into Reiser4 (e.g. inheritance) has been done in other fields of
computer science and studiously ignored by file systems developers for
several decades.  Quite simply, we are implementing the low-hanging
fruit first, and the innovative features of the syntax are not them.

Names resolve into sets of keys.  In Reiser4, the sets of keys all
consist of exactly one key, but this will change in future versions.

Keys are not immutable objectids ala inode numbers.  The use of
immutable objectids that all objects could be found by was the
original architecture, and before coding was started this was realized
to require lower performance due to creating immutable, and therefor
poor, locality of reference when resolving them.  Keys currently do
contain unique objectids, but this objectid does not suffice for
finding the object.

Name compounders construct names from subnames.  / and [] ([] is not
implemented in reiser4, see www.namesys.com/future_vision.html for
what it will do in a later version) are name compounders.  Name
compounders can use any name which can be resolved into a key as a
subname.  This provides "closure", in which the type of the result of
every name operation is the same as the type of what the name
operators operate on.  (Persons who create abstract models tend to
place a high value on achieving closure in their design.  For more
about closure, read www.namesys.com/future_vision.html.)

A/B indicates that B is to be passed to A, and A will resolve its
meaning.  This is consistent with Unix usage of /.  B is considered
a subname of A in this example.  Issue: is it consistent with current
Linux VFS?

Reiser4 supports a plugin that implements Unix regular files (regfile),
and a plugin that implements Unix regular directories (regdir).

Plugins may resolve a subname by invoking a plugin of another object,
or by invoking methods built into themselves.  

Special characters are whitespace plus []{}()/\,:;*$@!`' and keywords
are <- and -> 
<-, ->,  are assignment operators.


'A<-B' uses B's read method to read B, and uses A'S write method to
write to A what was read from B.  It is a copy command similar to
sendfile().

The righthand side of an assignment defines a flow.  We calculate the flow,
and then invoke the the write method defined by the lefthand side of
the assignment.

Example: 

A<-B 

assigns the contents of the object named B to A, overwriting the contents of A if A exists.

' and ' indicate that all special characters between them should be
ignored and parsed as a single string. That is, A<-'some text' causes A to have
contents equal to a file named 'some text'.  Quotes are allowed to nest.

" indicates that the next word is inlined text.  Sorry, " is the symbol least useful for something else, so it got used.

A<-"(this is a string not a name of a file) 

assigns (minus the single quotes) the string `this is a string not a name of a file' to A.

A<-"`I think that using " in a language for delimiting quoting is bad style because delimiters should be matching pairs not matching identical singletons if nesting is to work at all.'

assigns the string `I think that using " in a language for delimiting quoting is bad style because delimiters should be matching pairs not matching identical singletons if nesting is to work at all.' to A

A<<-B

appends file B to file A

We need to define multiple aspects of the object when creating it.
Additionally, we need to assign default values to those aspects of the
definition not defined.  The problem arises when we have a multi-part
definition.  We should avoid assigning one part, then assigning
default values for all other parts, then overwriting those default
values, some of which actually cannot be overwritten (e.g. pluginid).

This means we need to name the object, and then perform multiple
assignments in its creation. 

(x_ and )x_ where x is any sequence (including the null sequence) of
non-special characters, are `named parenthesis'.  They have the usual meaning of
parenthesis in most languages, and delimit what should be treated as a
single unit by operators.  If you use named parenthesis you can avoid
the "LISP bird nest" effect.  The disadvantage is that if you leave
off the whitespace following the open parenthesis you will get an
unintended effect.  Note that there must be no space between ( and x. 

Referencing the contents of parenthesis by the name of the parenthesis
is not for v4.

It is an unenforced but encouraged style convention that subnames which contain
meta-information about the object, and pseudo-files that implement
methods of the object, begin with `..'.  IT IS NOT A REQUIREMENT 
THAT THEY START WITH `..',  READ THAT AGAIN!  Sorry, got tired of the complaints about
the non-existent requirement.  It all depends on how you write your plugins that
use the meta-information whether the meta-data starts with `..'.  

Since what is meta-information, what is a method of the object, and
what is contained information, or methods of sub-objects, are not
necessarily always inherently susceptible to precise natural
distinction, and since we desire to allow users maximal stylistic
freedom to extend reiser4 into emulating other syntaxes, this is only
an optional plugin design convention for use when appropriate.

One can specify whether a file is listed by readdir in reiser4.  
Using that feature, subnames of files containing meta-information
about other files are by convention not listed by readdir, but can
be listed by using the command reiser4("A_listing<-A/..list"), and then reading the file A_listing.

For instance, if A is a regfile or regdir, then A/..stat/owner resolves to
a file containing the owner of A, and reading the A directory shows no
file named ..owner.  More generally, all of the fields returned by
stat() have a corresponding name of the form A/..stat/field_name for
all regfiles and regdirs.  The use of'..' avoids clashes between method names and
filenames.  More extreme measures could be taken using something more
obscure than '..' as a prefix, but I remember that Clearcase and WAFL
never really had much in the way of problems with namespace collisions
affecting users seriously, so I don't think one should excessively
inconvenience a syntax for such reasons.  

RESTRUCTURE COMMENT

*A (similar to C language dereference) means take the contents of
that object which A is the name for, and treat those contents as a name.


*`A B' is a reference to a file whose name consists of the characters
A and a space and a B.


A;B indicates that B is not to be executed until A completes.  So, `/' orders subnames within a compound name, and `;' orders operations.


A,B indicates that A and B are independent of each other and
unordered.  

A/B indicates that the plugin for A is to be passed B, 
and asked to handle it in its way, whatever that way is.  

C/..invert<-A +"(some text)+ B 
indicates that C when read shall return the contents of A followed by 'some text' as a delimiter followed by  the contents of B.

if  A  and   B  is object expressions then 
               A+B  is object expression
               A\B  is object expressoin
               A<-B is possible operation


So, let us discuss the following example:

Assume 357 is the user id of Michael Jackson.

The following 3 expressions are equivalent:

ascii_command = "/home/teletubbies/(..new(..name<-glove_location, ..object_t<-audit/encrypted, ..perm_t<-acl); glove_location/..acl<-( uid<-357, access<-denied ); glove_location/..audit<-mailto<-teletubbies@pbs.org; glove_location<-'we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower.')";

DEMIDOV, why the \ in the line below? -Hans
for make nonprintabele sysmbols.
example:  \n \t \013 ...

ascii_command = "/home/teletubbies/(glove_location<-( ..object_t<-audit/encrypted, ..perm_t<-acl); glove_location/..acl<-  ( uid<-357, access<-denied ); glove_location/..audit<-mailto<-teletubbies@pbs.org; glove_location<-'we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower)')";


ascii_command = "/home/teletubbies/(glove_location<-( ..object_t<-audit/encrypted, ..perm_t<-acl); glove_location / ( ..acl<-(uid<-357, access<-denied) ; ..audit<-mailto<-teletubbies@pbs.org); glove_location<-'we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower).')";


The components of this example have the following meanings: 
/home/teletubbies/       - directory name
/(..name                 - specifies that its argument is the name of the new file - parameter for ..new plugin
/glove_location, - name of new file - parameter for name submethod of ..new method
..object_t     -  name of submethod that assigns object type to new files - parameter for ..new plugin
/audit         - plugin for file
/encrypted,    - plugin for backing store for audit plugin
..perm_t       - security plugin to be assigned
)              - end of parameters for ..new plugin
;              - next system call
glove_location - file name
/..acl         - plugin ..acl
(              - begin parameters for ..new plugin of ..acl plugin
uid            - plugin of ..acl plugin
/357           - its value(parameter) (we need find it) (no, this is a value, we must denote it so)
,              - 
access         - ..
/denied        -  value to assign
)              - end of parametr list
)              - ? unbalanced brakes
;
..audit        - plugin - file is unknown!
/..new
/mailto
/"teletubbies@pbs.org"
;
glove_location_file - file name
/

"we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower)." - body of file

reiser4(&ascii_command, ascii_command_length, stack_binary_parameters_referenced_in_ascii_command, stack_length);


*/

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



/*


Assignment, and transaction, will be the commands supported in Reiser4(); more commands will appear in Reiser5. -> and <- will be the assignment operators.

The amount transferred by an assignment is the minimum of the size of the left hand side and the size of the right hand side.  This amount is usually made one of the return values.  

    * lhs (assignment target) values: 

    /..process/..range/(first_byte<-(loff_t),last_byte<-(loff_t),bytes_written<-(ssize_t*) )
              assigns (writes) to the buffer starting at address first_byte in the process address space, ending at last_byte, with the number of bytes actually written 
	      (The assignment source may be smaller or larger than the assignment target.) being written to address bytes_written. 
	      Representation of first_byte,last_byte, and bytes_written is left to the coder to determine. 
	      It is an issue that will be of much dispute and little importance. 
	      Notice / is used to indicate that the order of the operands matters; see www.namesys.com/future_vision.html for details of why this is appropriate syntax design. 
	      Note the lack of a file descriptor.

    /filename 
              assigns to the file named filename, wholly obliterating its body with what is assigned.

    /filename/..range/(first_byte<-(loff_t),last_byte<-(loff_t),bytes_written<-(ssize_t*) )
              writes to the body, starting at first_byte, ending not past last_byte, 
	      recording number of bytes written in bytes_written

    /filename/..range/(first_byte<-(loff_t),bytes_written<-(ssize_t*) )
              writes to the body starting at offset, recording number of bytes written in bytes_written

    * rhs (assignment source) values: 

    /..process/..range/(first_byte<-(loff_t),last_byte<-(loff_t),bytes_read<-(ssize_t*) )
              reads from the buffer starting at address first_byte in the process address space, ending at last_byte. 
	      The number of bytes actually read (assignment source may be smaller or larger than assignment target) is written to address bytes_read. 
	      Representation of first_byte, last_byte, and bytes_read is left to the coder to determine, as it is an issue that will be of much dispute and little importance.

    /filename 
              reads the entirety of the file named filename.

    /filename/..range/(first_byte<-(loff_t),last_byte<-(loff_t),bytes_read<-(ssize_t*) )
              reads from the body, starting at first_byte, ending not past last_byte, 
              recording number of bytes read in bytes_read

    /filename/..range/(first_byte<-(loff_t),bytes_read<-(ssize_t*) )
              reads from the body starting at offset until the end, recording number of bytes read in bytes_read

    /filename/..stat/owner 
              reads from the ownership field of the stat data (stat data is that which is returned by the 
              stat() system call (owner, permissions, etc.) and stored on a per file basis by the FS.)





*/




/*

example:



       /path0/path1/filename/..range/(offset<-100,bytes_written<-0xff001258,last_byte<-256)<-/path0/path2/filename/..range/(first_byte<-0,bytes_readed<-0xff001250) 

       /path0/(path1/filename/..range/(offset<-100,bytes_written<-0xff001258,last_byte<-256)<-path2/filename/..range(first_byte<-0,bytes_readed<-0xff001250) )

       /path0/(path1/filename/..range/(100,256)<-path2/filename/..range(0,256) )


                                                                          ?
       /path0/path1/filename/..range/(offset<-100,bytes_written<-0xff001258),last_byte<-256,/path0/path2/filename/..range(first_byte<-0,p_bytes_readed<-0xff001250) 

ssize_t bytes_readed;

sprintf( string_pointer_bytes_read, "%8.8p", &bytes_readed );

 */





/*
  Declare tokens used by our syntax. Priority increases
  downward. Tokens in the same line have the same priority.
 */
%token TRANSCRASH  /* tw/name[op_list] */
%token EOF 
%token L_ASSIGN /*L_ASSIGN_APPEND*/ L_SYMLINK
/*%token DOTDOT*/
%token EOL
%token PROCESS SPACE
%token SLASH /*SLASH_DOTDOT*/ SLASH2 SLASH3 ORDERED
%token STAT
%token L_PAREN R_PAREN INV_L INV_R
%token RANGE OFFSET OFFSET_BACK FIRST_BYTE LAST_BYTE P_BYTES_WRITTEN P_BYTES_READ LAST BYTES FIRST
%token WORD N_WORD P_WORD PATTERN Constant
%token EQ NE  LE GE   LT  GT   
%token IS
%token AND
%token OR
%token P_RUNNER
%token NOT
%token IF
%token THEN ELSE
%token EXIST


/* unfinished, comment out what is not used but is maybe someday intended */

/*
  Declare following tokens to be left associative.
 */
/*
%left      '~'
%left      '|'
%left      '&'
%left      '!'
%left      EQ NE  LE GE   LT  GT   
%left      '+'   '-'
%left      '*' '/'  '%' SLASH
%left      L_BRACKET R_BRACKET ':'  '{' '}' INV_L INV_R
%left      ','
%left      ';'
*/

/*
  follows operators equivalence:
  /path/path1/a<-/path/path2/b 
  /path/(path1/a<-path2/b)
 */

/*
  Starting production of our grammar.
 */
%start reiser4

%%

reiser4
: operation_list    EOF                          { make_do_it( $1 ); }
;

operation_list      
: operation                                      { $$ = make_op_list(  $1 );            /*make first*/}
| operation_list ';'  operation                  { $$ = add_op_list( $1, $3 );          /*make next*/}
;


/* need make new command:
        ln --	      creates links
        mv --	      renames
        rm --	      removes 
        rmdir --      removes empty directories
 */


operation           
: tw_begin operation_list ']'                           { $$ = end_tw_list( $1 ); level_down( TW_BEGIN );      /*   ..tw/[a<-b;b<-c] */ }
| asyn_begin asynchronous_list '}'                      { $$ = end_async_list( $1 ); level_down( ASYN_BEGIN ); }
| op_level operation_list ')'                           { $$ = $2;  level_down( OP_LEVEL ); }
| assignment                                            { $$ = $1; }
| if_statement                                          { $$ = $1; }
| cd_begin operation_list ')'                           { $$ = $2;  level_down( CD_BEGIN ); }                    /*   path_name/(a<-b;b<-c) */ 
;


tw_begin
: TRANSCRASH SLASH '['                                  { level_up( TW_BEGIN ); begin_tw_list( level ); }
;

asyn_begin
: '{'                                                   { level_up( ASYN_BEGIN ); begin_asynchronouse( level ); }
;

cd_begin
: Object_Name SLASH2 '('                                { level_up( CD_BEGIN ); $$ = make_current_path( $1, level ); } 
;

op_level
: '('                                                   { level_up( OP_LEVEL ); }
;


                                               /* list of operations that will be performed "simultaneously" */
asynchronous_list   
: operation                                             { $$ = make_asyn( $1 ); }
| asynchronous_list ',' operation                       { $$ = add_asyn( $1, $2 ); }
;

 

/* can the below be an asynchronous list?  can we make our language
   have some sort of notion of subassignments, in which it processes a
   list of assignments, and then performs post-processing to fill in
   all unspecified defaults? */


/* hte ASSIGNMENT operator mast be have a value: bytes to written */

assignment          
:  Object_Name L_ASSIGN       Expression                { $$ = assign( $1, $3 ); }            /*  <-  direct assigne  */
|  Object_Name L_ASSIGN INV_L Expression INV_R          { $$ = assign_invert( $1, $4 ); }     /*  <-  invert assign. destination mast have ..invert method  */
|  Object_Name L_SYMLINK      Expression                { $$ = symlink( $1, $3 ); }           /*   ->  symlink   */


Expression          
: Object_Name                                           {}
| op_level assignment ')'                               { $$ = $2; level_down( OP_LEVEL );}   /*  this expresion mast have value of written bytes    */
| Constant                                              { $$ = $1; }
| Expression '+' Expression                             { $$ = connect_expression( $1,  $3 ); }
| Expression EQ Expression                              { $$ = compare_expression( $1, $2, $3 ); }
| Expression NE Expression                              { $$ = compare_expression( $1, $2, $3 ); }
| Expression LE Expression                              { $$ = compare_expression( $1, $2, $3 ); }
| Expression GE Expression                              { $$ = compare_expression( $1, $2, $3 ); }
| Expression LT Expression                              { $$ = compare_expression( $1, $2, $3 ); }
| Expression GT Expression                              { $$ = compare_expression( $1, $2, $3 ); }
/*                    | Expression IS pattern   */
| Expression OR  Expression                             { $$ = compare_expression( $1, $2, $3 ); }
| Expression AND Expression                             { $$ = compare_expression( $1, $2, $3 ); }
| NOT_head Expression ')'                                { $$ = not_expression( $3 ); level_down( NOT_HEAD );}
| op_level Expression ')'                               { $$ = $2; level_down( OP_LEVEL ); }
| EXIST Object_Name                                     { $$ = check_exist( $2 ); }
;

NOT_head
:NOT '('                                                { level_up( NOT_HEAD ); }

if_statement        
: if_Expression Then_Else                               { $$ = $1; make_end_label(); level_down(IF_STATEMENT);}

if_Expression 
: if Expression                                         { $$ = $2; goto_if_false( $1-1, $2 ); }

if: IF                                                  { level_up( IF_STATEMENT ); $$ = reserv_label( 2 ); }

Then_Else           
: then_operation           
| then_operation else operation

then_operation
: THEN operation                                        { goto_end();}
;

else
: ELSE                                                  { else_lab();}
;

/* Object name begin */
Object_Name
: Object_Path_Name                                      {}
| Object_Path_Name SLASH3 range_type                    {}
| '[' Unordered_list ']'                                {}                            /* gruping: [name1 name2 [name3]]  */
| Object_Name ORDERED Object_Name {} /* ordered  rule (Hans want ORDERED is "/", but this is not posible, I keep define it for later ) */
;



Unordered_list
: Object_Name                                           { }
| P_RUNNER                                              {}
| Unordered_list SPACE Unordered_list                      {}
;

Object_Path_Name
: SLASH Object_relative_Name                            {}               /* /foo */
| SLASH PROCESS                                         {}               /* /??? */
| Object_relative_Name                                  {}               /* foo  */
;

Object_relative_Name
: Object_sub_Name                                       {}
| Object_relative_Name  SLASH Object_sub_Name           {}             /* foo/bar/baz */
;

Object_sub_Name
:  WORD                                                 { $$ = pars_pathname($1);} /* foo */
;

range_type
:  RANGE SLASH L_PAREN range_set R_PAREN                {}             /* /..range/(offset<-12345,p_bytes_written<-0xff001258,...) */
|  STAT SLASH WORD                                      {}             /* /..stat/atime */
|  STAT                                                 {}             /* /..stat */
;

range_set
: range                                                 {}
| range_set ',' range                                   {}
;

range
: OFFSET  L_ASSIGN N_WORD                               {}                      /*offset<-12345*/
| OFFSET_BACK  L_ASSIGN N_WORD                          {}                      /*offset_back<-12345*/
| FIRST  L_ASSIGN N_WORD                                {}                      /*first_byte<-123456*/
| LAST  L_ASSIGN N_WORD                                 {}                      /*last_byte<-123456*/
| P_BYTES_WRITTEN  L_ASSIGN P_WORD                      {}                      /*p_bytes_written<-0xff001258*/
| P_BYTES_READ  L_ASSIGN P_WORD                         {}                      /*p_bytes_read<-0xff001258*/
| BYTES                                                 {}                      /*p_units<-bytes*/
/*                    | LINES                                                                       /*p_units<-lines*/
;

/*
pattern             
: pattern '~' pattern               {}
| PATTERN
| Expression 
;
*/
/* Object name end*/



/*

  word. Word literals are sequences of characters enclosed in double quotes.
  The characters in the string may be represented using Character literals are specified just like in C.
  They may contain octal-escape characters (e.g., '\377'), and the usual special character escapes
  ('\b', '\r', '\t', '\n', '\f', '\'', '\\')
  If string is not contain a special symbol ( blank, '\b', '\r', '\t', '\n', '\f', '\'', '\\')
  etc, it can be not enclosed in double quotes. ????

*/
%%


/* Shell meta-characters that, when unquoted, separate words. */

#define shellmeta(c)	(strchr (shell_meta_chars, (c)) != 0)
#define shellbreak(c)	(strchr (shell_break_chars, (c)) != 0)
#define shellquote(c)	((c) == '"' || (c) == '`' || (c) == '\'')
#define shellexp(c)	((c) == '$' || (c) == '<' || (c) == '>')

char *shell_meta_chars = "()<>;&|";
char *shell_break_chars = "()<>;&| \t\n";





static
char nullname [] = {"+"};

static int coment;

static unsigned int
        tlen    [MAXDEF],
        blen    [MAXDEF],
        defco        ,
        memsize      ,
        calls;


static unsigned char 
	*   ptitle  [MAXDEF],
	*   pbuf    [MAXDEF];


					/* data for ncl arrey in grlex.c */




static struct
{
	struct inode *inode;
	int Name_type;

};

static struct
{
	char    *       wrd;
	int             class;
	int level;

} key [] =
	{
		"ÿÿÿÿÿ"      ,  0
	};

#define version "0"



#include <sys/types.h>

#include <sys/stat.h>
#include <linux/ctype.h>

#include "parser.h"

int 	yychar;			/* current input token number */


static int warproc;
struct msglist
{
	int  msgnum;
	long fileoff;
	struct msglist * nextmsg;
} ;
static struct msglist *Fistmsg;



allocate()
{
}

definit()
{
}

reinitial()
{
	int i;
	i=(sizeof(struct var)   * NVAR + sizeof(struct streg) * MAXNEST);

	memset((char*) Var     , 0,  i );

	Str     =   (struct streg *) (Var+(NVAR));
	freeSpace =   (        char *) (Str+(MAXNEST));
	maxtab  =                     freeSpace+(MAXTAB);
	inline  =                     maxtab;
	maxbuf  =                     maxtab+(MAXBUF);

	pline   = inline;


	yyerrco =  0;
	errco   =  0;
	optout  =  0;
	isflg   =  0;
	level   =  0;
	varco   = -1;
	strco   =  9;
	newvar(0);
}

initial()
{
	int i;
	reinitial();
}



insymbol()
{
	int eof;
	s=pline++;
	eof=0;
	if ( !( *s) ) eof=1;
	return(eof);
}


lexem()
{
	unsigned char term,n,i;
	int l,m;
	int ret;
	term=1;

	if (insymbol()) return(0);                      /* first symbl  */

	while ( ncl[*s]==6 )
		{
		if (insymbol()) return(0);              /* skip blank   */
		}


	cls     =       lcls    =       ncl[*s] ;
	yytext  = s;
	while( term )
		{
		while ( ( n = lexcls[ lcls ][ i=ncl[ *pline ] ] ) > 0 && n < 128)
			{
			insymbol();
			lcls=n;
			}
		n=-n;
		switch (n)
			{
			case 0:
				yyerror ( 2222, (lcls-1)* 20+i);
				return(0);
			case 1:
				term=0;
				break;
			default: 
				yyerror ( 3333, (lcls-1)* 20+i);
				return(0);
			}
		}
	 switch (lcls)
		{
		case 1223:
		default :                                /*  others  */
			ret=*yytext;
			break;
		}
	return(ret);
}


move_selected_word()
{
	int i,j;

	for( tmpWrdEnd = freeSpace; yytext <= s; )
		{
			i=0;
			while( *yytext == '\'' )
				{
					yytext++;
					i++;
				} 
			while ( yytext > s )
				{
					i--;
					yytext--;
				}
			if ( i ) for ( i/=2; i; i-- )      *tmpWrdEnd++='\'';    /*   in source text for each '' - result will '   */

			if ( *yytext == '\\' )           /*         \????????   */
				{
					int tmpI;
					yytext++;
					switch ( tolower(*yytext) )
						{
						case 'n':                       /*  \n  */
							*tmpWrdEnd++='\n';
							yytext++;
							break;
						case 'b':                       /*  \n  */
							*tmpWrdEnd++='\b';
							yytext++;
							break;
						case 'r':                       /*  \n  */
							*tmpWrdEnd++='\r';
							yytext++;
							break;
						case 'f':                       /*  \n  */
							*tmpWrdEnd++='\f';
							yytext++;
							break;
						case 't':                       /*  \t  */
							*tmpWrdEnd++='\t';
							yytext++;
							break;
						case 'o':                       /*  \o123  */
							tmpI = 3;
							i = 0;
							while( tmpI-- && isdigit( * ( yytext ) ) )
								{
									i = (i << 3) + ( *yytext++ - '0' );
								}
							*tmpWrdEnd++ = (unsigned char) i;
							break;
						case 'x':                       /*  \x01..9a..e  */
							i = 0;
							tmpI = 1;
							while( tmpI)
								{
									if (isdigit( *yytext ) )
										{
											i = (i << 4) + ( *yytext++ - '0' );
										}
									else if( tolower( *yytext ) >= 'a' && tolower( *yytext ) <= 'e' )
										{
											i = (i << 4) + ( *yytext++ - 'a' + 10 );
										}
									else 
										{
											if ( tmpI & 1 )
												{
													yyerror(); /* x format has odd number of symbols */
												}
											tmpI = 0;
										}
									if ( tmpI && !( tmpI++ & 1 ) )
										{
											*tmpWrdEnd++ = (unsigned char) i;
											i = 0;
										}
								}
							break;
						default:     
							if ( isdigit(*yytext) )            /*  decimal notation \123  */
								{
									int tmpI;
									i=atoi(yytext);
									tmpI=3;
									while( tmpI-- && isdigit( * ( ++yytext ) ) ) ;
									i = i % 256 ;    /* ??????? */
									*tmpWrdEnd++ = (unsigned char) i;
								}
							else
								{                          /*    any symbol */
									*tmpWrdEnd++ = *yytext++;
								}
						}
				}
			else *tmpWrdEnd++ = *yytext++;
	                if( tmpWrdEnd > maxtab )
		                {
					yyerror(1101);
					exit(1101);
		                }
                }
	*tmpWrdEnd++ = '\0';
}



b_check_word()
{
	int i, j, l;
	j=sizeof(key)/4;
	l=0;
	while( ( j - l ) >= 0 )
		{
			i  =  ( j + l + 1 ) >> 1;
			switch( strcmp( key[i].wrd, freeSpace ) )
				{
				case  0: return( key[i].class );  break;
				case  1: j = i - 1;               break;
				default: l = i + 1;               break;
				}
		}     
	return(0);
}

inttab()
{
	int i;
	if (strco)
		for( i=strco-1;i; i--)
			if( !(strcmp(wrdTab(i),freeSpace)) )
				{
					return(i);
				}
	if( strco >= MAXSTRN )             yyerror(1101);
	wrdTab(strco) = freeSpace;
	freeSpace=tmpWrdEnd;
	return(strco++);
}




FILE * errf;
struct {char cccc[40];} * nollpoit;


out(char *msg,int x1,int x2,int x3,int x4,int x5,int x6,int x7,int x8)
{
	if (Pflag) printf(msg,x1,x2,x3,x4,x5,x6,x7,x8);
}


yyerror(nmsg,x1,x2,x3,x4,x5,x6,x7,x8)
int 	nmsg,x1,x2,x3,x4,x5,x6,x7,x8;
{
	int i,j,k;
	char errt[100];
	char far * fss;
	char     * nss;

	i=pline-inline;
	if (!freeSpace) freeSpace=malloc(1024);
	if(i<0 || i>MAXLINE)
		{
			i=0;
			*inline=0;
		}
	if (!errf) errf=fopen(errfname,"r");
	j=0;
	sprintf(freeSpace,"%5d",nmsg);
	strcpy(errt,"0");
	if (errf)
		{
			fseek(errf,0L,SEEK_SET);
			while( nmsg>(j=atoi(errt)) ) if ( !fgets(errt,LENFNAME,errf) ) break;
		}
	if ( errf && nmsg==j )
		sprintf( freeSpace, errt, x1, x2, x3, x4, x5, x6, x7, x8 ) ;
	else    sprintf( freeSpace," %d Syntax error",nmsg);
	j=0;
	if (i || yylineno[yyinlev] )
		{
			printf("\n");
			if (i)
				for(i--;i;i--,j++)
					printf( ( *(inline+j)=='\t' ) ? "\t" : " " );
			printf("\n%s\n",inline);
		}
	if (j || yylineno[yyinlev] )  printf("FILE %-13s LINE %4d "
					     , curfile [ yyinlev ]
					     ,yylineno [ yyinlev ]  );
	printf("ERROR #%s",freeSpace);
	yyerrco++;
}

int pars_pathname($1)
{
	struct nameidata nd;
	struct inode * inode;
	int error;
	reiser4_plugin * r4_plugin;

	error = user_path_walk(yytext, &nd);

	if (error) 
		{
			r4_plugin = lookup_plugin_name( char *plug_label );
		}
	else
		{
			inode = nd.dentry->d_inode;
		}

	return error;
}

getvar(int n,int def)
{                           /* def==1 declare variable  */
	int i;                  /* def==0 find    variable  */
	for( i=varco; i ; i-- )
		if(  Vare(i)==n ) break;
	
	if ( def  )
		{
			if ( i )
				{
					if( i > parco )  yyerror(1015,wrdTab(n));
					else
						if(  !Varc(i)  ) yyerror(1015,wrdTab(n));
				}
			else
				i = newvar(n);
		}
	else
		{
			if ( !i ) yyerror(1018,wrdTab(n));
			else
			{
				if ( Varn( i ) & FRBD ) yyerror(1019,wrdTab(n));
				Varn( i )|=USED;
			}
		}
	return( i );
}

newvar(int n)
{
	int i;
	i=newtmp(getnam(n));
	Vare(i)     = n;
	return(i);
}

newtmp(int n)
{
	int i;
	++varco;
	i=varco;
	if(i >= NVAR)  yyerror(1106);
	Vart(i)     = n;
	Vare(i)     = 0;
	Varn(i)     = 0;
	Varlev(i)   = level;
	Varc(i)     = 0;
	Vara(i)     = 0;
	return(i);
}

lup(int s1)
{
	switch ( Slist   (level) )
		{
                        
			yyerror(456);
		}
	subup();
	level++;
	Stype   (level)   = s1;
	Sdef    (level)   = 0;
	Svar    (level)   = 0;
	Svar1   (level)   = 0;
	Svar2   (level)   = 0;
	Sloop   (level)   = 0;
	Slab    (level)   = 0;
	Spatco  (level)   = 0;
	Sapco   (level)   = 0;
	Sflag   (level)   = 0;
	Slsco   (level)   = 0;
	Slist   (level)   = 0;

}


ldw()
{
	int i;
	ldwl(1,level);
	level--;
}

ldwl(int s1,int lev)
{
	int i,j,k;
	if (lev==0) yyerror(500);
	for (i=Sdef(lev);i;i--) undefin();
	k=varco-Sdef(lev);
	lev--;
	j=0;
	for(i=k; Varlev(i)==lev && i; i--)
		{
			if ( Varc(i) && Vara(i) )
				{
					undefin();
					j++;
				}
		}
	if(s1)
		{
			varco=k;
			if (j) Sdef(lev) -= j;
		}
}



#define YYDEBUG 1

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
