/* A Bison parser, made by GNU Bison 1.875.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc_reiser4.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TRANSCRASH = 258,
     SEMICOLON = 259,
     COMMA = 260,
     L_ASSIGN = 261,
     L_APPEND = 262,
     L_SYMLINK = 263,
     PLUS = 264,
     L_BRACKET = 265,
     R_BRACKET = 266,
     SLASH = 267,
     PROCESS = 268,
     INV_L = 269,
     INV_R = 270,
     EQ = 271,
     NE = 272,
     LE = 273,
     GE = 274,
     LT = 275,
     GT = 276,
     IS = 277,
     AND = 278,
     OR = 279,
     P_RUNNER = 280,
     NOT = 281,
     IF = 282,
     THEN = 283,
     ELSE = 284,
     EXIST = 285,
     NAME = 286,
     UNNAME = 287,
     NAMED = 288,
     WORD = 289,
     STRING_CONSTANT = 290,
     ROOT = 291,
     RANGE = 292,
     OFFSET = 293,
     LENGTH = 294,
     LASTBYTE = 295,
     FIRSTBYTE = 296,
     UNITS = 297,
     BYTE = 298,
     LINE = 299,
     ITEM = 300,
     CUT = 301,
     ZERRO = 302,
     TRANSPARENT = 303
   };
#endif
#define TRANSCRASH 258
#define SEMICOLON 259
#define COMMA 260
#define L_ASSIGN 261
#define L_APPEND 262
#define L_SYMLINK 263
#define PLUS 264
#define L_BRACKET 265
#define R_BRACKET 266
#define SLASH 267
#define PROCESS 268
#define INV_L 269
#define INV_R 270
#define EQ 271
#define NE 272
#define LE 273
#define GE 274
#define LT 275
#define GT 276
#define IS 277
#define AND 278
#define OR 279
#define P_RUNNER 280
#define NOT 281
#define IF 282
#define THEN 283
#define ELSE 284
#define EXIST 285
#define NAME 286
#define UNNAME 287
#define NAMED 288
#define WORD 289
#define STRING_CONSTANT 290
#define ROOT 291
#define RANGE 292
#define OFFSET 293
#define LENGTH 294
#define LASTBYTE 295
#define FIRSTBYTE 296
#define UNITS 297
#define BYTE 298
#define LINE 299
#define ITEM 300
#define CUT 301
#define ZERRO 302
#define TRANSPARENT 303




/* Copy the first part of user declarations.  */


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 15 "fs/reiser4/parser/parser.y"
typedef union YYSTYPE {
	long charType;
	expr_v4_t * expr;
	wrd_t * wrd;
//	val_range_t * rng;
	rng_command_t * rng_com;
} YYSTYPE;
/* Line 191 of yacc.c.  */
#line 180 "fs/reiser4/parser/parser.tab.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined (YYLTYPE) && ! defined (YYLTYPE_IS_DECLARED)
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */
#line 23 "fs/reiser4/parser/parser.y"

//#include "lib.c"


/* Line 214 of yacc.c.  */
#line 207 "fs/reiser4/parser/parser.tab.c"

#  define YYSTACK_ALLOC(size) kmalloc((size),GFP_KERNEL)

#   define YYSIZE_T size_t

#  define YYSTACK_FREE kfree



#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYLTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss_1;
  YYSTYPE yyvs_1;
    YYLTYPE yyls_1;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
void * YYSTACK_RELOCATE(void *Stack, int old_size, int new_size)
{
void * ptr;
ptr = kmalloc(new_size, GFP_KERNEL);
memcpy(ptr, Stack, old_size);
kfree(Stack);
Stack=ptr;
return (ptr);
}
#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  18
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   119

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  49
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  21
/* YYNRULES -- Number of rules. */
#define YYNRULES  58
/* YYNRULES -- Number of states. */
#define YYNSTATES  97

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   303

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned char yyprhs[] =
{
       0,     0,     3,     5,     7,     9,    12,    15,    19,    23,
      27,    29,    33,    39,    43,    45,    47,    52,    55,    58,
      60,    63,    66,    70,    74,    78,    82,    86,    90,    94,
      98,   101,   103,   107,   109,   113,   118,   121,   125,   127,
     128,   130,   134,   140,   142,   145,   148,   151,   153,   155,
     157,   159,   161,   163,   165,   167,   169,   171,   173
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      50,     0,    -1,    51,    -1,    59,    -1,    35,    -1,    32,
      51,    -1,    31,    51,    -1,    51,     9,    51,    -1,    51,
       4,    51,    -1,    51,     5,    51,    -1,    53,    -1,    58,
      52,    51,    -1,    58,    52,    14,    51,    15,    -1,    58,
       8,    51,    -1,     6,    -1,     7,    -1,    54,    57,    29,
      51,    -1,    54,    57,    -1,    55,    56,    -1,    27,    -1,
      26,    51,    -1,    30,    51,    -1,    51,    16,    51,    -1,
      51,    17,    51,    -1,    51,    18,    51,    -1,    51,    19,
      51,    -1,    51,    20,    51,    -1,    51,    21,    51,    -1,
      51,    24,    51,    -1,    51,    23,    51,    -1,    28,    51,
      -1,    59,    -1,    60,    33,    59,    -1,    60,    -1,    60,
      12,    63,    -1,    12,    13,    12,    63,    -1,    61,    62,
      -1,    60,    12,    62,    -1,    12,    -1,    -1,    34,    -1,
      69,    51,    11,    -1,    37,    12,    69,    64,    11,    -1,
      65,    -1,    64,    65,    -1,    66,    35,    -1,    42,    67,
      -1,    68,    -1,    38,    -1,    40,    -1,    41,    -1,    39,
      -1,    43,    -1,    44,    -1,    45,    -1,    46,    -1,    47,
      -1,    48,    -1,    10,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned char yyrline[] =
{
       0,   105,   105,   109,   110,   111,   112,   114,   115,   116,
     117,   119,   123,   124,   133,   134,   138,   139,   143,   148,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     165,   169,   170,   174,   175,   176,   180,   181,   185,   186,
     190,   191,   195,   199,   200,   204,   205,   206,   210,   211,
     212,   213,   216,   217,   218,   222,   223,   224,   228
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TRANSCRASH", "SEMICOLON", "COMMA", 
  "L_ASSIGN", "L_APPEND", "L_SYMLINK", "PLUS", "L_BRACKET", "R_BRACKET", 
  "SLASH", "PROCESS", "INV_L", "INV_R", "EQ", "NE", "LE", "GE", "LT", 
  "GT", "IS", "AND", "OR", "P_RUNNER", "NOT", "IF", "THEN", "ELSE", 
  "EXIST", "NAME", "UNNAME", "NAMED", "WORD", "STRING_CONSTANT", "ROOT", 
  "RANGE", "OFFSET", "LENGTH", "LASTBYTE", "FIRSTBYTE", "UNITS", "BYTE", 
  "LINE", "ITEM", "CUT", "ZERRO", "TRANSPARENT", "$accept", "reiser4", 
  "Expression", "assign", "if_statement", "if_Begin", "if", 
  "if_Expression", "then_operation", "target", "Object_Name", "o_name", 
  "begin_from", "name", "range", "range_expression", "range_command", 
  "rng_key", "rng_units", "rng_type", "level_up", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    49,    50,    51,    51,    51,    51,    51,    51,    51,
      51,    51,    51,    51,    52,    52,    53,    53,    54,    55,
      56,    56,    56,    56,    56,    56,    56,    56,    56,    56,
      57,    58,    58,    59,    59,    59,    60,    60,    61,    61,
      62,    62,    63,    64,    64,    65,    65,    65,    66,    66,
      66,    66,    67,    67,    67,    68,    68,    68,    69
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     1,     2,     2,     3,     3,     3,
       1,     3,     5,     3,     1,     1,     4,     2,     2,     1,
       2,     2,     3,     3,     3,     3,     3,     3,     3,     3,
       2,     1,     3,     1,     3,     4,     2,     3,     1,     0,
       1,     3,     5,     1,     2,     2,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      39,    38,    19,    39,    39,     4,     0,     2,    10,     0,
      39,     0,     3,    33,     0,     0,     6,     5,     1,    39,
      39,    39,    39,    17,    39,    39,     0,    18,    14,    15,
      39,    39,     0,    39,    58,    40,    36,    39,     0,     8,
       9,     7,    30,    39,    20,    21,    39,    39,    39,    39,
      39,    39,    39,    39,    13,    39,    11,     0,    37,    34,
      32,    33,     0,    35,    16,    22,    23,    24,    25,    26,
      27,    29,    28,     0,     0,    41,    12,     0,    48,    51,
      49,    50,     0,    55,    56,    57,     0,    43,     0,    47,
      52,    53,    54,    46,    42,    44,    45
};

/* YYDEFGOTO[NTERM-NUM]. */
static const yysigned_char yydefgoto[] =
{
      -1,     6,     7,    31,     8,     9,    10,    27,    23,    11,
      12,    13,    14,    36,    59,    86,    87,    88,    93,    89,
      37
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -24
static const yysigned_char yypact[] =
{
      29,   -10,   -24,    29,    29,   -24,     6,   100,   -24,   -23,
      39,   104,   107,    -8,     2,    -4,   -24,   -24,   -24,    29,
      29,    29,    29,   -18,    29,    29,    73,   -24,   -24,   -24,
      29,    41,    25,     1,   -24,   -24,   -24,    29,   -22,    14,
      14,   -24,   -24,    29,   100,   100,    29,    29,    29,    29,
      29,    29,    29,    29,    14,    29,    14,    12,   -24,   -24,
     -24,    30,    75,   -24,   -24,   100,   100,   100,   100,   100,
     100,   100,   100,     5,    16,   -24,   -24,    60,   -24,   -24,
     -24,   -24,    74,   -24,   -24,   -24,    -9,   -24,    19,   -24,
     -24,   -24,   -24,   -24,   -24,   -24,   -24
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
     -24,   -24,    -3,   -24,   -24,   -24,   -24,   -24,   -24,   -24,
      24,    34,   -24,    26,    37,   -24,    -5,   -24,   -24,   -24,
     -11
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -32
static const yysigned_char yytable[] =
{
      16,    17,    94,    15,    32,    22,    18,    26,    38,    19,
      20,    43,    34,     1,    21,    57,    39,    40,    41,    42,
      76,    44,    45,    21,    74,    33,    34,    54,    56,    78,
      79,    80,    81,    82,    62,    34,    35,    83,    84,    85,
      64,     1,    32,    65,    66,    67,    68,    69,    70,    71,
      72,     1,    73,     1,    96,    55,     2,    60,    58,    35,
       3,     4,    57,    77,     5,    24,     2,    61,     2,    25,
       3,     4,     3,     4,     5,    63,     5,    19,    20,    19,
      20,    95,    21,     0,    21,     0,    75,     0,     0,    46,
      47,    48,    49,    50,    51,     0,    52,    53,    78,    79,
      80,    81,    82,     0,    19,    20,    83,    84,    85,    21,
      28,    29,    30,   -31,   -31,   -31,     0,    90,    91,    92
};

static const yysigned_char yycheck[] =
{
       3,     4,    11,    13,    12,    28,     0,    10,    12,     4,
       5,    29,    10,    12,     9,    37,    19,    20,    21,    22,
      15,    24,    25,     9,    12,    33,    10,    30,    31,    38,
      39,    40,    41,    42,    37,    10,    34,    46,    47,    48,
      43,    12,    12,    46,    47,    48,    49,    50,    51,    52,
      53,    12,    55,    12,    35,    14,    27,    33,    32,    34,
      31,    32,    37,    74,    35,    26,    27,    33,    27,    30,
      31,    32,    31,    32,    35,    38,    35,     4,     5,     4,
       5,    86,     9,    -1,     9,    -1,    11,    -1,    -1,    16,
      17,    18,    19,    20,    21,    -1,    23,    24,    38,    39,
      40,    41,    42,    -1,     4,     5,    46,    47,    48,     9,
       6,     7,     8,     6,     7,     8,    -1,    43,    44,    45
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    12,    27,    31,    32,    35,    50,    51,    53,    54,
      55,    58,    59,    60,    61,    13,    51,    51,     0,     4,
       5,     9,    28,    57,    26,    30,    51,    56,     6,     7,
       8,    52,    12,    33,    10,    34,    62,    69,    12,    51,
      51,    51,    51,    29,    51,    51,    16,    17,    18,    19,
      20,    21,    23,    24,    51,    14,    51,    37,    62,    63,
      59,    60,    51,    63,    51,    51,    51,    51,    51,    51,
      51,    51,    51,    51,    12,    11,    15,    69,    38,    39,
      40,    41,    42,    46,    47,    48,    64,    65,    66,    68,
      43,    44,    45,    67,    11,    65,    35
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrlab1

/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
#endif

/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, &yylloc, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, &yylloc)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG


#ifndef fprintf
#define fprintf printk
#endif

# ifndef YYFPRINTF
//#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF ( "%s ", Title);				\
      yysymprint ( Token, Value, Location);	\
      YYFPRINTF ( "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF ( "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF ( " %d", *bottom);
  YYFPRINTF ( "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF ( "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF ( "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF ( "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yysymprint (yytype, yyvaluep, yylocationp)
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF ( "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT ( yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF ( "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF ( ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yytype, yyvaluep, yylocationp)
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

int yyparse (YYPARSE_PARAM);







/*----------.
| yyparse.  |
`----------*/

static
int yyparse (YYPARSE_PARAM)
{
  /* The lookahead symbol.  */

int yychar;


/* The semantic value of the lookahead symbol.  */
/*
YYSTYPE yylval;
*/

/* Number of syntax errors so far.  */

  int yynerrs;



/* Location data for the lookahead symbol.  */
YYLTYPE yylloc;

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  yyss = yyssa;
/*  
  short *yyss = yyssa;
  register short *yyssp;
*/
  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  yyvs = yyvsa;
/*
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;
*/

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  yyls = yylsa;
/*  YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp;*/
  YYLTYPE *yylerrsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
/*  YYSTYPE yyval;*/
  YYLTYPE yyloc;

  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF (("Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
/*
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
*/
	YYSTACK_RELOCATE (yyss,YYSIZE,YYSTACKSIZE);
	YYSTACK_RELOCATE (yyvs,YYSIZE,YYSTACKSIZE);
	YYSTACK_RELOCATE (yyls,YYSIZE,YYSTACKSIZE);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF (( "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF (( "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF (( "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF (( "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF (( "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
  *++yylsp = yylloc;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 105 "fs/reiser4/parser/parser.y"
    { yyval.charType = yyvsp[0].expr; /* free_expr( ws, $1 );*/ ;}
    break;

  case 3:
#line 109 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[0].expr;;}
    break;

  case 4:
#line 110 "fs/reiser4/parser/parser.y"
    { yyval.expr = const_to_expr( ws, yyvsp[0].wrd ); ;}
    break;

  case 5:
#line 111 "fs/reiser4/parser/parser.y"
    { yyval.expr = unname( ws, yyvsp[0].expr ); ;}
    break;

  case 6:
#line 112 "fs/reiser4/parser/parser.y"
    { yyval.expr = name( ws, yyvsp[0].expr ); ;}
    break;

  case 7:
#line 114 "fs/reiser4/parser/parser.y"
    { yyval.expr = concat_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 8:
#line 115 "fs/reiser4/parser/parser.y"
    { yyval.expr = list_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 9:
#line 116 "fs/reiser4/parser/parser.y"
    { yyval.expr = list_async_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 10:
#line 117 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[0].expr; level_down( ws, IF_STATEMENT, IF_STATEMENT ); ;}
    break;

  case 11:
#line 119 "fs/reiser4/parser/parser.y"
    { yyval.expr = assign( ws, yyvsp[-2].expr, yyvsp[0].expr, yyvsp[-1].charType ); ;}
    break;

  case 12:
#line 123 "fs/reiser4/parser/parser.y"
    { yyval.expr = assign_invert( ws, yyvsp[-4].expr, yyvsp[-1].expr, yyvsp[-3].charType ); ;}
    break;

  case 13:
#line 124 "fs/reiser4/parser/parser.y"
    { yyval.expr = symlink( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 14:
#line 133 "fs/reiser4/parser/parser.y"
    {yyval.charType = O_TRUNC; ;}
    break;

  case 15:
#line 134 "fs/reiser4/parser/parser.y"
    {yyval.charType = O_APPEND;;}
    break;

  case 16:
#line 138 "fs/reiser4/parser/parser.y"
    { yyval.expr = if_then_else( ws, yyvsp[-3].expr, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 17:
#line 139 "fs/reiser4/parser/parser.y"
    { yyval.expr = if_then( ws, yyvsp[-1].expr, yyvsp[0].expr) ;         ;}
    break;

  case 18:
#line 143 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[0].expr; ;}
    break;

  case 19:
#line 148 "fs/reiser4/parser/parser.y"
    { level_up( ws, IF_STATEMENT ); ;}
    break;

  case 20:
#line 152 "fs/reiser4/parser/parser.y"
    { yyval.expr = not_expression( ws, yyvsp[0].expr ); ;}
    break;

  case 21:
#line 153 "fs/reiser4/parser/parser.y"
    { yyval.expr = check_exist( ws, yyvsp[0].expr ); ;}
    break;

  case 22:
#line 154 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_EQ_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 23:
#line 155 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_NE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 24:
#line 156 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_LE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 25:
#line 157 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_GE_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 26:
#line 158 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_LT_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 27:
#line 159 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_GT_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 28:
#line 160 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_OR_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 29:
#line 161 "fs/reiser4/parser/parser.y"
    { yyval.expr = compare_AND_expression( ws, yyvsp[-2].expr, yyvsp[0].expr ); ;}
    break;

  case 30:
#line 165 "fs/reiser4/parser/parser.y"
    { goto_end( ws );;}
    break;

  case 31:
#line 169 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[0].expr;;}
    break;

  case 32:
#line 170 "fs/reiser4/parser/parser.y"
    { yyval.expr = target_name( yyvsp[-2].expr, yyvsp[0].expr );;}
    break;

  case 33:
#line 174 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[0].expr; ;}
    break;

  case 34:
#line 175 "fs/reiser4/parser/parser.y"
    { yyval.expr = range2expr( ws, yyvsp[-2].expr, yyvsp[0].expr);;}
    break;

  case 35:
#line 176 "fs/reiser4/parser/parser.y"
    { yyval.expr = pars_lookup_process( ws, yyvsp[0].expr ) ; ;}
    break;

  case 36:
#line 180 "fs/reiser4/parser/parser.y"
    { yyval.expr = pars_expr( ws, yyvsp[-1].expr, yyvsp[0].expr ) ; ;}
    break;

  case 37:
#line 181 "fs/reiser4/parser/parser.y"
    { yyval.expr = pars_expr( ws, yyvsp[-2].expr, yyvsp[0].expr ) ; ;}
    break;

  case 38:
#line 185 "fs/reiser4/parser/parser.y"
    { yyval.expr = pars_lookup_root( ws ) ; ;}
    break;

  case 39:
#line 186 "fs/reiser4/parser/parser.y"
    { yyval.expr = pars_lookup_curr( ws ) ; ;}
    break;

  case 40:
#line 190 "fs/reiser4/parser/parser.y"
    { yyval.expr = lookup_word( ws, yyvsp[0].wrd ); ;}
    break;

  case 41:
#line 191 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[-1].expr; level_down( ws, yyvsp[-2].charType, yyvsp[0].charType );;}
    break;

  case 42:
#line 195 "fs/reiser4/parser/parser.y"
    { yyval.expr = yyvsp[-1].expr; level_down( ws, yyvsp[-2].charType, yyvsp[0].charType );;}
    break;

  case 43:
#line 199 "fs/reiser4/parser/parser.y"
    { yyval.expr = new_range(ws, yyvsp[0].rng_com); ;}
    break;

  case 44:
#line 200 "fs/reiser4/parser/parser.y"
    { yyval.expr = add_range(ws, yyvsp[-1].expr, yyvsp[0].rng_com); ;}
    break;

  case 45:
#line 204 "fs/reiser4/parser/parser.y"
    { yyval.rng_com = range_expression2command(ws, yyvsp[-1].charType, yyvsp[0].wrd);;}
    break;

  case 46:
#line 205 "fs/reiser4/parser/parser.y"
    { yyval.rng_com = range_units_type(ws, COMMAND_UNITS, yyvsp[0].charType);;}
    break;

  case 47:
#line 206 "fs/reiser4/parser/parser.y"
    { yyval.rng_com =  range_units_type(ws, COMMAND_TYPE, yyvsp[0].charType);;}
    break;

  case 48:
#line 210 "fs/reiser4/parser/parser.y"
    { yyval.charType = COMMAND_OFFSET;;}
    break;

  case 49:
#line 211 "fs/reiser4/parser/parser.y"
    { yyval.charType = COMMAND_LAST;;}
    break;

  case 50:
#line 212 "fs/reiser4/parser/parser.y"
    { yyval.charType = COMMAND_FIRST;;}
    break;

  case 51:
#line 213 "fs/reiser4/parser/parser.y"
    { yyval.charType = COMMAND_LEN;;}
    break;

  case 52:
#line 216 "fs/reiser4/parser/parser.y"
    { yyval.charType = UNITS_BYTE;;}
    break;

  case 53:
#line 217 "fs/reiser4/parser/parser.y"
    { yyval.charType = UNITS_LINE;;}
    break;

  case 54:
#line 218 "fs/reiser4/parser/parser.y"
    { yyval.charType = UNITS_ITEM; ;}
    break;

  case 55:
#line 222 "fs/reiser4/parser/parser.y"
    { yyval.charType = RANGE_CUT;;}
    break;

  case 56:
#line 223 "fs/reiser4/parser/parser.y"
    { yyval.charType = RANGE_ZERRO;;}
    break;

  case 57:
#line 224 "fs/reiser4/parser/parser.y"
    { yyval.charType = RANGE_TRANSPARENT;;}
    break;

  case 58:
#line 228 "fs/reiser4/parser/parser.y"
    { yyval.charType = yyvsp[0].charType; level_up( ws, yyvsp[0].charType ); ;}
    break;


    }

/* Line 970 of yacc.c.  */
#line 1439 "fs/reiser4/parser/parser.tab.c"

  yyvsp -= yylen;
  yyssp -= yylen;
  yylsp -= yylen;

  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }

  yylerrsp = yylsp;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp, yylsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval, &yylloc);
      yychar = YYEMPTY;
      *++yylerrsp = yylloc;
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab2;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:

  /* Suppress GCC warning that yyerrlab1 is unused when no action
     invokes YYERROR.  */
#if defined (__GNUC_MINOR__) && 2093 <= (__GNUC__ * 1000 + __GNUC_MINOR__)
  __attribute__ ((__unused__))
#endif

  yylerrsp = yylsp;
  *++yylerrsp = yyloc;
  goto yyerrlab2;


/*---------------------------------------------------------------.
| yyerrlab2 -- pop states until the error token can be shifted.  |
`---------------------------------------------------------------*/
yyerrlab2:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp, yylsp);
      yyvsp--;
      yystate = *--yyssp;
      yylsp--;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF (( "Shifting error token, "));

  *++yyvsp = yylval;
  YYLLOC_DEFAULT (yyloc, yylsp, (yylerrsp - yylsp));
  *++yylsp = yyloc;

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 235 "fs/reiser4/parser/parser.y"




/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   End:
*/

