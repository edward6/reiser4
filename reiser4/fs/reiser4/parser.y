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
meaning.  This is consistent with Unix usage of /.  B is considered a
subname of A in this example.  Issue: is it consistent with current
Linux VFS?

Reiser4 supports a plugin that implements Unix regular files (regfile),
and a plugin that implements Unix regular directories (regdir).

Plugins may resolve a subname by invoking a plugin of another object,
or by invoking methods built into themselves.  

Special characters are whitespace plus []{}()/\,:;*$@!`' and keywords
are <- and -> and <= and =>

<-, ->, <=, and => are assignment operators.  

Example: 

A<-B 

assigns the contents of the object named B to A, overwriting the contents of A if A exists.

A<=B assigns the string 'B' to A.

` and ' indicate that all special characters between them should be
ignored and parsed as a single string.  That is, A<-`some text' causes A to have
contents equal to a file named `some text'.  Quotes are allowed to nest.

A and B are the names of files.

A<<-B

appends file B to file A


A<<=B

appends the string B to file A

*A (similar to C language dereference) means take the contents of
that object which A is the name for, and treat those contents as a name.

*`A B' is a reference to a file whose name consists of the characters
A and a space and a B.


(x_ and )x_ where x is any sequence (including the null sequence) of
non-special characters, are `named parenthesis' if x is not null, and
unnamed parenthesis otherwise.  They have the usual meaning of
parenthesis in most languages, and delimit what should be treated as a
single unit by operators.  If you use named parenthesis you can avoid
the "LISP bird nest" effect.  The disadvantage is that if you leave
off the whitespace following the open parenthesis you will get an
unintended effect.  Note that there must be no space between ( and x. 

Referencing the contents of parenthesis by name of the parenthesis is
not implemented yet.

It is an unenforced
but encouraged style convention that subnames which contain
meta-information about the object, and pseudo-files that implement
methods of the object, begin with `..'.  Since what is meta-information, what is a method of the object, and
what is contained information, or methods of sub-objects, are not
necessarily always inherently susceptible to precise natural
distinction, and since we desire to allow users maximal stylistic
freedom to extend reiser4 into emulating other syntaxes, this is only
an optional plugin design convention for use when appropriate.

Subnames of meta-information are by convention not listed by readdir.

For instance, if A is a regfile or regdir, then A/..owner resolves to
a file containing the owner of A, and reading the A directory shows no
file named ..owner.  More generally, all of the fields returned by
stat() have a corresponding name of the form A/..stat_field_name for
all regfiles and regdirs.  The use of'..' avoids clashes between method names and
filenames.  More extreme measures could be taken using something more
obscure than '..' as a prefix, but I remember that Clearcase and WAFL
never really had much in the way of problems with namespace collisions
affecting users seriously, so I don't think one should excessively
inconvenience a syntax for such reasons.  

We need to define multiple aspects of the object when creating it.
Additionally, we need to assign default values to those aspects of the
definition not defined.  The problem arises when we have a multi-part
definition.  We should avoid assigning one part, then assigning
default values for all other parts, then overwriting those default
values, some of which actually cannot be overwritten (e.g. pluginid).

This means we need to name the object, and then perform multiple
assignments in its creation.  'A<-B' uses B's read method to read B,
and uses A'S write method to write to A what was read from B.  It
is a copy command similar to sendfile().

'A;B' indicates that 'B' is not to be executed until 'A' completes.
'A,B' indicates that 'A' and 'B' are independent of each other and
unordered.  'A/B' indicates that the plugin for 'A' is to be passed
'B', and asked to handle it in its way, whatever that way is.  '*A'
indicates that one is supposed to substitute in the contents of A into
the command string.  'C/..inherit/"**A 'some text' **B"' indicates
that C when read shall return the contents of A followed by 'some text'
as a delimiter followed by the contents of B.

So, let us discuss the following example:

Assume 357 is the user id of Michael Jackson.

ascii_command = '/home/teletubbies/..new/(..name/glove_location, ..object_t/audit/encrypted, (..perm_t/acl));glove_location/..acl/..new(uid/357, access/denied)); ..audit/..new/mailto/\`teletubbies@pbs.org'; glove_location/..copy/\`We stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower).';

which is equivalent to

ascii_command = '/home/teletubbies/glove_location<-( ..object_t/audit/encrypted, (..perm_t/acl));glove_location/..acl<-(uid/357, access/denied)); ..audit/mailto<-\`teletubbies@pbs.org'; glove_location<-`we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower\').';

The components of this example have the following meanings: 
/home/teletubbies/       - directory name
..new                    - plugin of file(file shut be have a method "new"?)
/(..name                 - specifies that its argument is the name of the new file - parameter for ..new plugin
/glove_location, - name of new file - parameter for name submethod of ..new method
..object_t     -  name of submethod that assigns object type to new files - parameter for ..new plugin
/audit         - plugin for file
/encrypted,    - plugin for backing store for audit plugin
(..perm_t/acl) - ?
)              - end of parameters for ..new plugin
;              - next system call
glove_location - file name
/..acl         - plugin ..acl
/..new         - parametr for plugin ..acl
(              - begin parametrs for ..new plugin of ..acl plugin
uid            - plugin of ..acl plugin
/357           - its value(parameter) (we need find it)
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
/..copy
/

"we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower)." - body of file

reiser4(&ascii_command, ascii_command_length, stack_binary_parameters_referenced_in_ascii_command, stack_length);

struct {
char * loot_inventory_buffer = &loot_inventory_buffer;
int buffer_length = sizeof(loot_inventory_buffer);
int * bytes_written = &bytes_written;
}  __attribute__ ((__packed__)) stack_binary_parameters_referenced_in_ascii_command;

ascii_command = "..memory_address_space/range/%p/%d/%p,/home/teletubbies/complete_loot_inventory)"

reiser4(&ascii_command, ascii_command_length, stack_binary_parameters_referenced_in_ascii_command, sizeof(stack_binary_parameters_referenced_in_ascii_command));


*/


%token   TRANSCRASH L_ASSIGN EOF RANGE OFFSET DOTDOT
%token   EOL
%token PROCESS
%token STRING
%token PROCESS
%token OCTAL_NUMBER
%token HEX_DECIMAL_NUMBER
%token DECIMAL_NUMBER
%token IF
%token THEN
%token EXIST

%left      '~'
%left      '|'
%left      '&'
%left      '!'
%left      EQ NE   LE GE   LT  GT  '<' '>' '='         /* EQ NE   LE GE   LT  GT */
%left      '+'   '-'
%left      '*'  '/'   '%' SLASH
%left      '(' ':' ')' '{' '}'
%left      ','
%left      ';'

%start reiser4

%%

/* why does this exist? */
reiser4             : call_list ';'  EOL

/* why does this exist? */
call_list           : systemcall
                    | '{' call_list '}'
                    | call_list ';'  systemcall


systemcall          : transcrash
                    | asynchronous_list
                    | assign
                    | Object_Name
                    | if_statement

asynchronous_list   : systemcall
                    | asynchronous_list ',' systemcall


transcrash          : TRANSCRASH SLASH '[' call_list ']'
    ;

assign              :  Object_Name L_ASSIGN  Object_Name
/* can the below be an asynchronous list?  can we make our language
   have some sort of notion of subassignments, in which it processes a
   list of assignments, and then performs post-processing to fill in
   all unspecified defaults? */
                    | L_ASSIGN '(' Object_Name ','  Object_Name ')'

Object_Name         : SLASH Object_relativ_Name Ordering
                    | SLASH process range first_last_bytes

Object_relative_Name  : Object_sub_Name
                    | Object_relativ_Name SLASH Object_sub_Name 


Object_sub_Name     : Grouping
                    | path_sub_name 
                    | plugin_name '(' parameter_list ')'

/* the use of DOTDOT is a style convention, not a grammar feature,
   please discuss with me and read comments above. */

plugin_name         : DOTDOT Name

Ordering            :
                    |   range first_last_bytes
                    |   offset offset_byte
                    |   stat  owner
                    |   nonbody
;

first_last_bytes    : SLASH value SLASH value SLASH value

process             : SLASH PROCESS

range               : SLASH DOTDOT RANGE

offset              : SLASH DOTDOT OFFSET

Grouping            : '[' Unordered_List ']'
                    | '_'  Name '[' Unordered_List ']'
                    | unordered


Unordered_List      : unordered
                    | Unordered_List logic_operator Unordered_List
                    | '(' Unordered_List ')'

logic_operator      : AND
                    | OR
                    |

unordered          : header pattern
                    | value
;

header              : value  IS
;

pattern             : Grouping
                    | simple_pattern
;


simple_pattern      : simple_pattern  simple_pattern               {}
                    | value
;

value               : WORD
                    | Object_Name EQ value
                    | Object_Name NE value
                    | Object_Name LE value
                    | Object_Name GE value
                    | Object_Name LT value
                    | Object_Name GT value

                    | EXIST Object_Name 
                    | Object_sub_Name
                    | PATT


number : OCTAL_NUMBER
      | HEX_DECIMAL_NUMBER
      | DECIMAL_NUMBER



if_statement        : IF value THEN systemcall 

/*
Object_Name  : Grouping
    | Ordering
    | plugin_call
    | Key_Object
    | Storage_Key
    | Orthogonal
    :  process range first_last_bytes  
first_last_bytes: SLASH first_byte SLASH last_byte SLASH bytes

*/


/*

  Strings. String literals are sequences of characters enclosed in double quotes.
  The characters in the string may be represented using Character literals are specified just like in C.
  They may contain octal-escape characters (e.g., '\377'), and the usual special character escapes
  ('\b', '\r', '\t', '\n', '\f', '\'', '\\')
  If string is not contain a special simbol, sach as blank, '\b', '\r', '\t', '\n', '\f', '\'', '\\'
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



};

static struct
{
char    *       wrd;
int             class;
}
	key [] =
{
	"ÿÿÿÿÿ"      ,  0
};

char copywrite[]=
{
""
};


#define version "4.0"

#include <sys/types.h>

#include <sys/stat.h>
#include <linux/ctype.h>

struct { char  copyr[80]; } * chknull;

int 	yychar;			/* current input token number */
short	yyerrflag = 0;		/* error recovery flag */


static
char    errfname    [LENFNAME]    =   {   "mes"       },
	hlpfname    []            =   {   "help"      };

static int warproc;
struct msglist
{
	int  msgnum;
	long fileoff;
	struct msglist * nextmsg;
} ;
static struct msglist *Fistmsg;

static char comma   []  =   {   ","     };
static char bsln    []  =   {   "\n"   };
static char morda   []  =   {   ""     };
static char tree    []  =   {   "3"     };
static char blank   []  =   {   " "     };
static char one     []  =   {   "1"     };
static char zzzzz   []  =   {   "ÿÿ"     };

unsigned int    n1024   = 50000;

allocate()
{
	unsigned int i,j,l,k,n,m;
	l=n1024;

	while( !( Proct = malloc(l) ) )  l-=512;

	i = sizeof( struct var  )    * NVAR;
	j = sizeof( struct streg )   * MAXNEST;
	n = sizeof( struct proctype) * MAXPROC + 2 * MAXPROC;
	m = n + MAXPROC*10;
	k = i + j + MAXBUF*2 + MAXTAB ;

	if ( l<=k+m )         yyerror(501);
	if ( (l-k-m)<1024 )   yyerror(501);

	procsort=   (char *)    Proct + sizeof( struct proctype) * MAXPROC;

	partab  =   (char *)    Proct + n ;
	parmax  =   (char *)    Proct + m ;

	Var     =   (char *)    parmax ;
	macarea =   (char *)    Var   + k ;
	maxmac  =   (char *)    Var   + l ;
	freetab =   (char *)    Var ; /* temp assigne */

	k=n1024;
	while(( codarea = _fmalloc(k))==NULL )
		{
		k-=1024;
		if (k<1024)
			{
			k=0;
			yyerror(501);
			break;
			}
		}

	codm    = codarea + k;

	tptr[0] = nullname;
	tptr[1] = comma  ;
	tptr[2] = bsln   ;
	tptr[3] = morda  ;
	tptr[4] = tree   ;
	tptr[5] = blank  ;
	tptr[6] = one    ;
	tptr[8] = zzzzz   ;

	Fistmsg = NULL;
}

definit()
{
	defco       =  0;
	ptitle [0]  = "ÿ";
	tlen   [0]  = 1;
	blen   [0]  = 1;
	pbuf   [0]  = ptitle[0];
	freemac     = macarea;
}

reinitial()
{
	int i;
	i=(sizeof(struct var)   * NVAR + sizeof(struct streg) * MAXNEST);

	memset((char*) Var     , 0,  i );

	Str     =   (struct streg *) (Var+(NVAR));
	freetab =   (        char *) (Str+(MAXNEST));
	maxtab  =                     freetab+(MAXTAB);
	inline  =                     maxtab;
	maxbuf  =                     maxtab+(MAXBUF);

	yyerrco =  0;
	warproc =  0;
	errco   =  0;
	optout  =  0;
	isupco  =  0;
	optupco =  0;
	isflg   =  0;
	level   =  0;
	patco   =  0;
	varco   = -1;
	parco   =  0;

	strco   =  9;
	newvar(0);
}

initial()
{
	int i;
	reinitial();
	parend  =   partab ;
	codend  =  codarea;
	Nproc   =  0;
	Nparm   =  0;
}


openfiles()
{
	char ssres[LENFNAME];
	char * zz, * xx;
	int i;
	strcpy(ssres,curfile[yyinlev] );
	xx =  ssres ;
	zz = strchr(xx,'.');
	i=zz-xx;
	*(ssres+i)='\0';
	if (Aflag)
		{
		strcat (ssres,".smb");
		fres=fopen(ssres,"wt");
		if(!fres) yyerror(601,ssres);
		}
	else fres=NULL;
	*(ssres+i)='\0';
	if (Iflag)
		{
		strcat (ssres,".int");
		intf=open(ssres,O_BINARY|O_CREAT|O_RDWR,S_IREAD|S_IWRITE);
		if(!intf) yyerror(601,ssres);
		}
	else intf=0;
	labco=0;
}

closefiles()
{
	char ssres[LENFNAME];
	char * zz, * xx;
	int i;
	yyinlev=0;
	strcpy(ssres,curfile[yyinlev] );
	xx =  ssres ;
	zz = strchr(xx,'.');
	i=zz-xx;
	*(ssres+i)='\0';
	if (fres!=NULL)
		{
		outsmb();
		fprintf(fres,"\n\tEND\n");
		fclose(fres);
		fres=0;
		strcat (ssres,".smb");
		if (yyerrco) unlink(ssres);
		}
	*(ssres+i)='\0';
	if (intf)
		{
		outint();
		close(intf);
		intf=0;
		strcat (ssres,".int");
		if (yyerrco) unlink(ssres);
		}
	fclose(yyin[yyinlev]);
}

prepr(int ipar,char * s1)
{
int i;
char *s2,*s3,sss[LENFNAME];

   switch(ipar)
	{
	case 1:                         /*  include          */
		if (yyinlev++>7)                             yyerror(602);
		for(s2 = s1;ncl[*s2] == 6;s2++);  /* skip first blanks */

		for(s3 = s2;*s3;s3++);            /* find end-of-string */
		for(s3--;ncl[*s3] == 6;s3--);
										  /* delete ends blanks */
		*(s3+1) = '\0';

		if(strlen(s2) > (LENFNAME-1 - strlen(inclname)))
			{
			yyerror(2002);
			return;
			}

		strcpy(curfile[yyinlev],s2);
		strcpy(sss,"");
		if( *s2 == '<')                   /* standart include DIR */
			if( *s3 == '>')
				{
				*s3 = '\0';
				s2++;
				strcpy(sss,inclname);
				}
			else
				{
				yyerror(2001);
				return;
				}
		strcat(sss,s2);

		if ((yyin[yyinlev]=fopen(sss,"r"))==NULL)    yyerror(1108,sss);
		if (Dflag) put(File,strlen(curfile[yyinlev]),curfile[yyinlev]);
		break;
	case 2:                         /*  file name param   */
		strcpy(curfile[yyinlev],s1   );
		strcpy(sss             ,s1   );
		if ((yyin[yyinlev]=fopen(sss,"r"))==NULL) yyerror(1108,curfile[yyinlev]);
		pline = inline;
		outlinnum[0]=0;
		break;
	}
	yylineno[yyinlev] = 0;
	for(i=10246,s2=copywrite;*s2;s2++)
		i-=*s2;
}


prdef()                    /* title and buffer of define */
{
	int       i,ii,j,l;
	char    * s2, * ss;

	if ( (freemac+strlen(inline)-8)>maxmac) yyerror(1501);

	for(s2 = inline + 8;  (i=ncl[*s2]) == 6          ;  s2++);
	if ( i != 1 ) yyerror(1500);
	for(ss = s2        ; (i=ncl[*s2]) == 1 || i == 2 ;  s2++);

	ii = s2 - ss;

	strncpy(freemac,ss,ii);
	*(freemac+ii) = '\0';

	l=0;
	if (defco)
		{
		j=defco;
		do
			{
			i  =  ( j + l + 1 ) >> 1;
			switch( strcmp( freemac,ptitle[i]) )
				{
				case  1: l = i + 1;  break;
				case  0: yyerror(1017,ptitle[i]);
					return(0);
					break;
				default: j = i - 1;	break;
				}
			}       while( (j-l)>=0 );
		}

	defco++;
	if (defco==MAXDEF)  yyerror(1114);

	for(i=defco;i>l;i--)
		{
		j=i-1;
		tlen   [i] = tlen   [j];
		ptitle [i] = ptitle [j];
		blen   [i] = blen   [j];
		pbuf   [i] = pbuf   [j];
		}

	tlen  [l] = ii;
	ptitle[l] = freemac;

	freemac += ii+1;

	for(;ncl[*s2] == 6;s2++);
	strcpy(freemac,s2);
	blen[l]   = strlen(freemac);
	i = blen[l];
	pbuf[l] = freemac;
	freemac += i +1;
	return(0);
}


newline()
{
	int i, ii , k ;
	char * ss;
	if (!Pflag && yyerrco ) return(1);                  /* simulate EOF   */
/*
	if (!(Pflag || yyinlev))   text in memory !
*/
	while( fgets( inline, MAXLINE-2, yyin[yyinlev] ) == NULL )
		{
		if (yyinlev)
			{
			fclose (yyin[yyinlev--]);           /*   end of cur  file */
			if (Dflag) put(Fileend);
			}
		else     return(1);                     /*   end of root file */
		}
	i=strlen(inline);
	if ( *( inline + i - 1 ) == '\n' && i>1 )   *(inline+i-1)='\0';
	yylineno[yyinlev]++;
	if (Dflag)
		{
			put(Line,yylineno[yyinlev],yyinlev);
		if ( !(yyinlev) && Tflag )
			put(Brkpnt,yylineno[yyinlev]);
		}
	pline = inline ;
	return(0);
}


getline()
{
	int i,ii,k;
	*inline=0;
	coment=1;
	while(!(*inline))
		{
		if ( newline() ) return(1);
		}

	if(defco)
		{
		for(s = inline;  *s ; s++)
			{
			for(         ; *s && ncl[*s] != 1 ; s++);
			if (*s)
				{
				for( pline=s ; (i=ncl[*s]) == 1 || i == 2   ; s++);
				ii = s - pline;
				if ((k=finddef(pline,ii))!=-1)
					{
					strcpy( maxbuf           ,s       );
					strcpy( pline            ,pbuf[k] );
					strcpy( pline  + blen[k] ,maxbuf  );
					if(strlen(inline) > MAXBUF)
						{
						pline = inline;
						*(inline + 79) = '\0';
						yyerror(1502);
						*(pline+2)='\0';
						*(pline+1)='\0';
						*pline='\n';
						}
					s=pline-1;
					}
				}
			else break;
			}
		}
	pline = inline ;
	s=pline++;
	if (*s=='#') { *s='\n';*pline=0;}
	return(0);
}



insymbol()
{
	int eof;
	s=pline++;
	eof=0;
	if ( !( *s) ) eof=getline();
	return(eof);
}


lexem()
{
	unsigned char term,n,i;
	int l,m;
	term=1;

	if (insymbol()) return(0);                      /* first symbl  */

	while ( ncl[*s]==6 )
		{
		if (insymbol()) return(0);              /* skip blank   */
		}

	while ( *s=='/' && *pline=='*' )            /* skip coment  */
		{
		if (insymbol()) return(0);
		if (insymbol()) return(0);
		if ( *s=='*' && *pline=='/' )
			{
			if (insymbol()) return(0);
			if (insymbol()) return(0);
			}
		}



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
			case 2:
				if (*pline!='E') term=0;
				  else
					{
					lcls=16;
					if (insymbol()) return(0);
					}
				break;
			default: 
			}
		}
	 switch (lcls)
		{
		case :                                /*  others  */
			l=*yytext;
			break;
		}
	return(l);
}


movdigtofr()
{
	int i;
	for(freetend=freetab;yytext<=s;)
		{
		i=0;
		if (yytext<=s)
			{
			*freetend++ = *yytext++;
			}
		if( freetend > maxtab )
			{
			yyerror(1101);
			exit(1101);
			}
		}
	*freetend++ = '\0';
}

movstrtofr()
{
	int i,j;
	for(freetend=freetab;yytext<=s;)
		{
		i=0;
		while(*yytext =='\'')
			{
			yytext++;
			i++;
			}
		if (yytext>s) i--;
		i/=2;
		if(i) for (;i;i--)      *freetend++='\'';
		if (yytext<=s)
			{
			if (*yytext=='\\')
				{
				yytext++;
				if (tolower(*yytext)=='n')
					{
					*freetend++='\n';
					yytext++;
					}
				else
					{
					if ( isdigit(*yytext) )
						{
						i=atoi(yytext);
						while( isdigit( * ( ++yytext ) ) ) ;
						while (i>255) i-=256;
						*freetend++ = (unsigned char) i;
						}
					else
						{
						*freetend++ = *yytext++;
						}
					}
				}
			else *freetend++ = *yytext++;
			}
		if( freetend > maxtab )
			{
			yyerror(1101);
			exit(1101);
			}
		}
	*freetend++ = '\0';
}



movkeytofr()
{
	int i;
	for(freetend=freetab;yytext<=s;)
		{
		if (yytext<=s)
			{
			*freetend++=tolower(*yytext);
			yytext++;
			}
		if( freetend > maxtab )
			{
			yyerror(1101);
			exit(1101);
			}
		}
	*freetend++ = '\0';
}


chkkey()
{
	int i, j, l;
	j=sizeof(key)/4;
	l=0;
	do
		{
		i  =  ( j + l + 1 ) >> 1;
		switch( strcmp(key[i].wrd,freetab) )
			{
			case  0: return(key[i].class);  break;
			case  1: j = i - 1;             break;
			default: l = i + 1;             break;
			}
		}       while( (j-l)>=0 );
	return(0);
}

inttab()
{
	int i;
	if (strco)
	for( i=strco-1;i; i--)
		if( !(strcmp(tptr[i],freetab)) )
		{
		return(i);
		}
	if( strco >= MAXSTRN )             yyerror(1101);
	tptr[strco] = freetab;
	freetab=freetend;
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
	if (!freetab) freetab=malloc(1024);
	if(i<0 || i>MAXLINE)
		{
		i=0;
		*inline=0;
		}
	if (!errf) errf=fopen(errfname,"r");
	j=0;
	sprintf(freetab,"%5d",nmsg);
	strcpy(errt,"0");
	if (errf)
		{
		fseek(errf,0L,SEEK_SET);
		while( nmsg>(j=atoi(errt)) ) if ( !fgets(errt,LENFNAME,errf) ) break;
		}
	if ( errf && nmsg==j )
				sprintf( freetab, errt, x1, x2, x3, x4, x5, x6, x7, x8 ) ;
		 else   sprintf( freetab," %d Syntax error",nmsg);
	if (Pflag )
		{
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
		printf("ERROR #%s",freetab);
		}
	else
		{
		if (!yyerrco)
			{

	mailsend (yylineno [yyinlev], i, curfile [yyinlev], freetab);

			}
		}
	yyerrco++;
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
			if( i > parco )  yyerror(1015,tptr[n]);
			else
				if(  !Varc(i)  ) yyerror(1015,tptr[n]);
			}
		else
			i = newvar(n);
		}
	else
		{
		if ( !i ) yyerror(1018,tptr[n]);
		else
			{
			if ( Varn( i ) & FRBD ) yyerror(1019,tptr[n]);
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
		case LISTID :
		case LISTIDW:
		case LISTCHK :
		case LISTDEFW:
		case LISTDEFS:
		case LISTDEFC:
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
