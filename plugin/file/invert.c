/* Suppose you want to conveniently read and write a large variety of
   small files conveniently within a single emacs buffer, without
   having a separate buffer for each 8 byte or so file.  Inverts are
   the way to do that.

  An invert is an inverted assignment.  Instead of taking an
  assignment command and writing a bunch of files, it takes a bunch of
  files and writes an assignment command that if executed would create
  those files.  But which files?  Well, that must be specified in the
  body of the invert using a special syntax, and that specification is
  called the invert of the assignment.

  When written to, an invert performs the assignment command that is written
  to it, and modifies its own body to contain the invert of that
  assignment.

  In other words, writing to an invert file what you have read from it
  is the identity operation.

  Malformed assignments cause write errors.  Partial writes are not
  supported in v4.0, but will be.

  Example:

    If an invert contains:

    /filenameA/<>+"(some text stored in the invert)+/filenameB/<>



====================== 
Each element in this definition should be an invert, and all files
should be called recursively - too.  This is bad. If one of the
included files in not a regular or invert file, then we can't read
main file.

I think to make it is possible easier: 

internal structure of invert file should be like symlink file. But
read and write method should be explitely indicated in i/o operation.. 

By default we read and write (if probably) as symlink and if we
specify ..invert at reading time that too we can specify it at write time.

example:
/my_invert_file/..invert<- ( (/filenameA<-"(The contents of filenameA))+"(some text stored in the invert)+(/filenameB<-"(The contents of filenameB) ) )
will create  /my_invert_file as invert, and will creat /filenameA and /filenameB with specified body.

read of /my_invert_file/..invert will be 
/filenameA<-"(The contents of filenameA)+"(some text stored in the invert)+/filenameB<-"(The contents of filenameB)

but read of /my_invert_file/ will be 
The contents of filenameAsome text stored in the invertThe contents of filenameB

we also can creat this file as  
/my_invert_file/<-/filenameA+"(some text stored in the invert)+/filenameB
will create  /my_invert_file , and use existing files /filenameA and /filenameB.

and when we will read it will be as previously invert file.


This is correct?
 vv
======================= 

  Then a read will return:

    /filenameA<-"(The contents of filenameA)+"(some text stored in the invert)+/filenameB<-"(The contents of filenameB)

    and a write of the line above to the invert will set the contents of
    the invert and filenameA and filenameB to their original values.

  Note that the contents of an invert have no influence on the effect
  of a write unless the write is a partial write (and a write of a
  shorter file without using truncate first is a partial write).

  truncate() has no effect on filenameA and filenameB, it merely
  resets the value of the invert.

  Writes to subfiles via the invert are implemented by preceding them
  with truncates.

  Parse failures cause write failures.

  Questions to ponder: should the invert be acted on prior to file
  close when writing to an open filedescriptor? 

 Example:

 If an invert contains:

   "(This text and a pair of quotes are all that is here.)

Then a read will return:

   "(This text and a pair of quotes are all that is here.)

*/

/*
  OPEN method places struct file in memory associated with invert body
  and returns file descriptor to the user for the future access to the invert.
  During opening we parse invert body and get a list of the 'entryes'
  (subfile names) need to be opened.
  Then we create a list of structures invert_entry and place pointer on it in
  reiserfs-specific part of invert inode. Every subfile is described by this
  structure invert_entry that has a pointer on struct znode and a pointer
  on struct inode @pt_i (if we find that this subfile uses unformated node, we load
  inode, otherwise we load znode that contains corresponding direct item).

  Since READ and WRITE methods for inverts were formulated in assignment
  language, they don't contain arguments 'size' and 'offset' that make sence
  only in ordinary read/write methods. 

  READ method is a combination of two methods:
  *ordinary read method (with offset=0, lenght=i_size) for subfiles with @pt_i != 0   
  *lightweight read method for subfiles with @pt_i =0
  in the last method we don't use page cahe, just copy data from
  @coord -> node -> data to the user.

  the same we have for WRITE method 
  
*/


ssize_t reiser4_subfile_read (struct * invert_entry, flow * f) {
	
}

ssize_t reiser4_subfile_write (struct * invert_entry, flow * f) {

}

ssize_t reiser4_invert_read (struct * file, flow * f) {
	
}

ssize_t reiser4_invert_write (struct * file, flow * f) {
	
}
