/* Copyright by Hans Reiser, 2003 */

extern int reiser4_fill_super (struct super_block * s, void * data, int silent);
extern int reiser4_done_super (struct super_block * s);
extern reiser4_internal reiser4_plugin * get_default_plugin(pset_member memb);
