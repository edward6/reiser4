/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * definitions of work space for yacc generated  from
 * parser.y
 */


#define MAXLEVELCO 500
#define BEGIN_FROM_ROOT 222
#define BEGIN_FROM_CURRENT 333



struct reiser4_syscall_w_space
{
	/*	char * ws_inline;     this two field used for parsing string, one (inline) stay on begin */
	char * ws_pline;     /*   of token, second (pline) walk to end to token                   */
#ifdef yyacc
	                     /* next field need for yacc                   */
	                     /* accesing to this fields from rules: ws->... */
	int ws_yystacksize; /*500*/
	int ws_yymaxdepth ; /*500*/
	int ws_yydebug;
	int ws_yynerrs;
	int ws_yyerrflag;
	int ws_yychar;
	int * ws_yyssp;
	YYSTYPE * ws_yyvsp;
	YYSTYPE ws_yyval;
	YYSTYPE ws_yylval;
	short   ws_yyss[YYSTACKSIZE];
	YYSTYPE ws_yyvs[YYSTACKSIZE];
#else
	/* declare for bison */
#endif
	int	ws_yyerrco;
	int	ws_level;              /* current level            */
	int	ws_errco;              /* number of errors         */
	                               /* working fields  */
	char        * tmpWrdEnd;
	char        * yytext;
	                               /* space for   */
	freeSpace_t * freeSpHead;
	freeSpace_t * freeSpCur;
	wrd_t       * wrdHead;
	pars_var_t     * Head_pars_var;
	streg_t     * Head_level;	
	streg_t     * cur_level;	
	expr_v4_t   * root_e;          /* root expression  for this task */

	pars_var_t     * wvn;              /* work    for this task */

	struct dentry * de;            /* work dentry for this task */
	struct nameidata nd;

//	lnode     * wln;              /* work lnode   for this task */
//	lnode     * root_ln;          /* root lnode   for this task */
//	lnode     * pwd_ln;           /* pwd  lnode   for this task */
//	struct path_walk path_walk;
};



#define printf prink

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
