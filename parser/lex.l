%Start Normal

LETTER          [A-Za-z_]
ODIG            [0-7]
DIGIT           ([8-9]|{ODIG})
WORD           ({LETTER}|{DIGIT})+
WHITESPACE      [ \t\n]+
SPECIAL         [\[\]\{\}\/\\\,\:\;\*\$\@\!\`\']

BS_B            \\b
BS_R            \\r
BS_T            \\t
BS_N            \\n
BS_F            \\f
BS_BS           \\\\



%%

%{
BEGIN Normal;
%}


\\o{ODIG}{3,3}          { ret( 0 );}

\\b    {x("b");}
\\r    {x("r");}
\\t    {x("t");}
\\n    {x("n");}
\\f    {x("f");}
\\"'"  {x("\'") ;}
\\\\  {x("\\");}

{SPECIAL}{1,1}           { ret( SP_SIM );}
"("({WORD}_)?            { ret( L_BRACET );}
")"({WORD}_)?            { ret(R_BRACET);}
{WORD}                   { ret( WRD );}
{WHITESPACE}             { ret( SP );}

"<""-"                   { ret( L_ASSIGN );}
"-"">"                   { ret( L_SYMLINK );}
.                        { ret( 0 );}
%%

ret(int par)
{

}

