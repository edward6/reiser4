/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Key assignment policy implementation */

#include "debug.h"
#include "key.h"
#include "kassign.h"
#include "vfs_ops.h"
#include "inode.h"
#include "super.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block, etc  */

#define OID_CHARS (sizeof(__u64) - 1)
#define OFFSET_CHARS (sizeof(__u64))

static const __u64 longname_mark = 0x0100000000000000ull;

int
is_longname_key(const reiser4_key *key)
{
	assert("nikita-2863", key != NULL);
	assert("nikita-2864", get_key_type(key) == KEY_FILE_NAME_MINOR);

	return (get_key_objectid(key) & longname_mark) ? 1 : 0;
}

int
is_longname(const char *name, int len)
{
	return len > OID_CHARS + OFFSET_CHARS;
}

/* code ascii string into __u64.
  
   Put characters of @name into result (@str) one after another starting
   from @start_idx-th highest (arithmetically) byte. This produces
   endian-safe encoding. memcpy(2) will not do.
   
*/
static __u64
pack_string(const char *name /* string to encode */ ,
	    int start_idx	/* highest byte in result from
				 * which to start encoding */ )
{
	unsigned i;
	__u64 str;

	str = 0;
	for (i = 0; (i < sizeof str - start_idx) && name[i]; ++i) {
		str <<= 8;
		str |= (unsigned char) name[i];
	}
	str <<= (sizeof str - i - start_idx) << 3;
	return str;
}

#if !REISER4_DEBUG_OUTPUT
static
#endif
char *
unpack_string(__u64 value, char *buf)
{
	do {
		*buf = value >> (64 - 8);
		if (*buf)
			++ buf;
		value <<= 8;
	} while(value != 0);
	*buf = 0;
	return buf;
}

char *
extract_name_from_key(const reiser4_key *key, char *buf)
{
	char *cont;

	assert("nikita-2868", !is_longname_key(key));

	cont = unpack_string(get_key_objectid(key) & ~longname_mark, buf);
	(void)unpack_string(get_key_offset(key), cont);
	return buf;
}

/* build key to be used by ->readdir() method.
  
   See reiser4_readdir() for more detailed comment. */
int
build_readdir_key(struct file *dir /* directory being read */ ,
		  reiser4_key * result /* where to store key */ )
{
	reiser4_file_fsdata *fdata;
	struct inode *inode;

	assert("nikita-1361", dir != NULL);
	assert("nikita-1362", result != NULL);
	assert("nikita-1363", dir->f_dentry != NULL);
	inode = dir->f_dentry->d_inode;
	assert("nikita-1373", inode != NULL);

	fdata = reiser4_get_file_fsdata(dir);
	if (IS_ERR(fdata))
		return PTR_ERR(fdata);
	assert("nikita-1364", fdata != NULL);
	return extract_key_from_de_id(get_inode_oid(inode), &fdata->dir.readdir.position.dir_entry_key, result);

}

/* build key for directory entry. */
int
build_entry_key(const struct inode *dir	/* directory where entry is
					 * (or will be) in.*/ ,
		const struct qstr *qname	/* name of file referenced
					 * by this entry */ ,
		reiser4_key * result	/* resulting key of directory
					 * entry */ )
{
	oid_t objectid;
	__u64 offset;
	const char *name;
	int len;

	assert("nikita-1139", dir != NULL);
	assert("nikita-1140", qname != NULL);
	assert("nikita-1141", qname->name != NULL);
	assert("nikita-1142", result != NULL);

	name = qname->name;
	len  = qname->len;

	assert("nikita-2867", strlen(name) == len);

	key_init(result);
	/* locality of directory entry's key is objectid of parent
	   directory */
	set_key_locality(result, get_inode_oid(dir));
	/* minor packing locality is constant */
	set_key_type(result, KEY_FILE_NAME_MINOR);
	/* dot is special case---we always want it to be first entry in
	   a directory. Actually, we just want to have smallest
	   directory entry.
	*/
	if (len == 1 && name[0] == '.')
		return 0;

	/* This is our brand new proposed key allocation algorithm for
	   directory entries:
	  
	   If name is shorter than 7 + 8 = 15 characters, put first 7
	   characters into objectid field and remaining characters (if
	   any) into offset field. Dream long dreamt came true: file
	   name as a key!
	  
	   If file name is longer than 15 characters, put first 7
	   characters into objectid and hash of remaining characters
	   into offset field.
	  
	   To distinguish above cases, in latter set up unused high bit
	   in objectid field.
	  
	*/

	/* objectid of key is composed of seven first characters of
	   file's name. This imposes global ordering on directory
	   entries.
	*/
	objectid = pack_string(name, 1);
	if (!is_longname(name, len)) {
		if (len > OID_CHARS)
			offset = pack_string(name + OID_CHARS, 0);
		else
			offset = 0ull;
	} else {
		/* note in a key fact that offset contains hash. */
		objectid |= longname_mark;
		/* offset is the hash of the file name. */
		offset = inode_hash_plugin(dir)->hash(name + OID_CHARS, 
						      len - OID_CHARS);
	}

	/* objectid is 60 bits */
	assert("nikita-1405", !(objectid & ~KEY_OBJECTID_MASK));
	set_key_objectid(result, objectid);
	set_key_offset(result, offset);
	return 0;
}

/* build key for directory entry.
  
   This is for directories where we want repeatable and restartable readdir()
   even in case 32bit user level struct dirent (readdir(3)).
*/
int
build_readdir_stable_entry_key(const struct inode *dir	/* directory where
							 * entry is (or
							 * will be) in. */ ,
			       const struct qstr *name	/* name of file
							 * referenced by
							 * this entry */ ,
			       reiser4_key * result	/* resulting key of
							 * directory entry */ )
{
	oid_t objectid;

	assert("nikita-2283", dir != NULL);
	assert("nikita-2284", name != NULL);
	assert("nikita-2285", name->name != NULL);
	assert("nikita-2286", result != NULL);

	key_init(result);
	/* locality of directory entry's key is objectid of parent
	   directory */
	set_key_locality(result, get_inode_oid(dir));
	/* minor packing locality is constant */
	set_key_type(result, KEY_FILE_NAME_MINOR);
	/* dot is special case---we always want it to be first entry in
	   a directory. Actually, we just want to have smallest
	   directory entry.
	*/
	if ((name->len == 1) && (name->name[0] == '.'))
		return 0;

	/* objectid of key is 31 lowest bits of hash. */
	objectid = inode_hash_plugin(dir)->hash(name->name, (int) name->len) & 0x7fffffff;

	assert("nikita-2303", !(objectid & ~KEY_OBJECTID_MASK));
	set_key_objectid(result, objectid);

	/* offset is always 0. */
	set_key_offset(result, (__u64) 0);
	return 0;
}

/* true, if @key is the key of "." */
int
is_dot_key(const reiser4_key * key /* key to check */ )
{
	assert("nikita-1717", key != NULL);
	assert("nikita-1718", get_key_type(key) == KEY_FILE_NAME_MINOR);
	return (get_key_objectid(key) == 0ull) && (get_key_offset(key) == 0ull);
}

/* build key for stat-data.
  
   return key of stat-data of this object. This should became sd plugin
   method in the future. For now, let it be here.
  
*/
reiser4_key *
build_sd_key(const struct inode * target /* inode of an object */ ,
	     reiser4_key * result	/* resulting key of @target
					   stat-data */ )
{
	assert("nikita-261", result != NULL);

	key_init(result);
	set_key_locality(result, reiser4_inode_by_inode(target)->locality_id);
	set_key_objectid(result, get_inode_oid(target));
	set_key_type(result, KEY_SD_MINOR);
	set_key_offset(result, (__u64) 0);
	return result;
}

/* encode part of key into &obj_key_id
  
   This encodes into @id part of @key sufficient to restore @key later,
   given that latter is key of object (key of stat-data).
  
   See &obj_key_id
*/
int
build_obj_key_id(const reiser4_key * key /* key to encode */ ,
		 obj_key_id * id /* id where key is encoded in */ )
{
	assert("nikita-1151", key != NULL);
	assert("nikita-1152", id != NULL);

	xmemcpy(id, key, sizeof *id);
	return 0;
}

/* encode reference to @obj in @id.
  
   This is like build_obj_key_id() above, but takes inode as parameter. */
int
build_inode_key_id(const struct inode *obj /* object to build key of */ ,
		   obj_key_id * id /* result */ )
{
	reiser4_key sdkey;

	assert("nikita-1166", obj != NULL);
	assert("nikita-1167", id != NULL);

	build_sd_key(obj, &sdkey);
	build_obj_key_id(&sdkey, id);
	return 0;
}

/* decode @id back into @key
  
   Restore key of object stat-data from @id. This is dual to
   build_obj_key_id() above.
*/
int
extract_key_from_id(const obj_key_id * id	/* object key id to extract key
						 * from */ ,
		    reiser4_key * key /* result */ )
{
	assert("nikita-1153", id != NULL);
	assert("nikita-1154", key != NULL);

	key_init(key);
	xmemcpy(key, id, sizeof *id);
	return 0;
}

/* extract objectid of directory from key of directory entry within said
   directory.
   */
oid_t extract_dir_id_from_key(const reiser4_key * de_key	/* key of
								 * directory
								 * entry */ )
{
	assert("nikita-1314", de_key != NULL);
	return get_key_locality(de_key);
}

/* encode into @id key of directory entry.
  
   Encode into @id information sufficient to later distinguish directory
   entries within the same directory. This is not whole key, because all
   directory entries within directory item share locality which is equal
   to objectid of their directory.
  
*/
int
build_de_id(const struct inode *dir /* inode of directory */ ,
	    const struct qstr *name	/* name to be given to @obj by
					 * directory entry being
					 * constructed */ ,
	    de_id * id /* short key of directory entry */ )
{
	reiser4_key key;

	assert("nikita-1290", dir != NULL);
	assert("nikita-1292", id != NULL);

	/* NOTE-NIKITA this is suboptimal. */
	inode_dir_plugin(dir)->entry_key(dir, name, &key);
	return build_de_id_by_key(&key, id);
}

/* encode into @id key of directory entry.
  
   Encode into @id information sufficient to later distinguish directory
   entries within the same directory. This is not whole key, because all
   directory entries within directory item share locality which is equal
   to objectid of their directory.
  
*/
int
build_de_id_by_key(const reiser4_key * entry_key	/* full key of directory
							 * entry */ ,
		   de_id * id /* short key of directory entry */ )
{
	xmemcpy(id, ((__u64 *) entry_key) + 1, sizeof *id);
	return 0;
}

/* restore from @id key of directory entry.
  
   Function dual to build_de_id(): given @id and locality, build full
   key of directory entry within directory item.
  
*/
int
extract_key_from_de_id(const oid_t locality	/* locality of directory
						 * entry */ ,
		       const de_id * id /* directory entry id */ ,
		       reiser4_key * key /* result */ )
{
	/* no need to initialise key here: all fields are overwritten */
	xmemcpy(((__u64 *) key) + 1, id, sizeof *id);
	set_key_locality(key, locality);
	set_key_type(key, KEY_FILE_NAME_MINOR);
	return 0;
}

/* compare two &obj_key_id */
cmp_t key_id_cmp(const obj_key_id * i1 /* first object key id to compare */ ,
		 const obj_key_id * i2 /* second object key id to compare */ )
{
	reiser4_key k1;
	reiser4_key k2;

	extract_key_from_id(i1, &k1);
	extract_key_from_id(i2, &k2);
	return keycmp(&k1, &k2);
}

/* compare &obj_key_id with full key */
cmp_t key_id_key_cmp(const obj_key_id * id /* object key id to compare */ ,
		     const reiser4_key * key /* key to compare */ )
{
	reiser4_key k1;

	extract_key_from_id(id, &k1);
	return keycmp(&k1, key);
}

/* compare two &de_id's */
cmp_t de_id_cmp(const de_id * id1 /* first &de_id to compare */ ,
		const de_id * id2 /* second &de_id to compare */ )
{
	/* NOTE-NIKITA ugly implementation */
	reiser4_key k1;
	reiser4_key k2;

	extract_key_from_de_id((oid_t) 0, id1, &k1);
	extract_key_from_de_id((oid_t) 0, id2, &k2);
	return keycmp(&k1, &k2);
}

/* compare &de_id with key */
cmp_t de_id_key_cmp(const de_id * id /* directory entry id to compare */ ,
		    const reiser4_key * key /* key to compare */ )
{
	reiser4_key k1;

	extract_key_from_de_id(get_key_locality(key), id, &k1);
	return keycmp(&k1, key);
}

/* true if key of root directory sd */
int
is_root_dir_key(const struct super_block *super /* super block to check */ ,
		const reiser4_key * key /* key to check */ )
{
	assert("nikita-1819", super != NULL);
	assert("nikita-1820", key != NULL);
	/* call disk plugin's root_dir_key method if it exists */
	if (get_super_private(super)->df_plug && get_super_private(super)->df_plug->root_dir_key)
		return keyeq(key, get_super_private(super)->df_plug->root_dir_key(super));
	return 0;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
