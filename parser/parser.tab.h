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




#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 15 "fs/reiser4/parser/parser.y"
typedef union YYSTYPE {
	long charType;
	expr_v4_t * expr;
	wrd_t * wrd;
//	val_range_t * rng;
	rng_command_t * rng_com;
} YYSTYPE;
/* Line 1227 of yacc.c.  */
#line 140 "fs/reiser4/parser/parser.tab.h"
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




