/* Parser for the reiser4() system call */

/* Takes a string and parses it into a set of commands which are
   executed.  */

/*

We need to define multiple aspects of the object when creating it.
Additionally, we need to assign default values to those aspects of the
definition not defined.  The problem arises when we have a multi-part
definition.  We should avoid assigning one part, then assigning
default values for all other parts, then overwriting those default
values, some of which actually cannot be overwritten (e.g. pluginid).

This means we need to name the object, and then perform multiple
assignments in its creation.  Pooh suggested that we should use prefix
notation in the kernel, and put the syntactic sugar in a user space
library.  I really prefer A<-B visually, but for now if A exists use
'A/..copy/B' else use '..new/A/B'.

Some features of the syntax:

'..' precedes the names of pseudo files by convention.  That is, to
invoke a particular method of an object, you can say
'filenameA/..method1' and method1 of the object named filenameA will
be invoked.  The use of '..' avoids clashes between method names and
filenames.  More extreme measures could be taken using something more
obscure than '..' as a prefix, but I remember that Clearcase and WAFL
never really had much in the way of problems with namespace collisions
affecting users seriously, so I don't think one should excessively
worry about these things.  Maybe something more mnemonic than '..' 
could be invented.

'A;B' indicates that 'B' is not to be executed until 'A' completes.
'A,B' indicates that 'A' and 'B' are independent of each other and
unordered.  'A/B' indicates that the plugin for 'A' is to be passed
'B', and asked to handle it in its way, whatever that way is.  '*A'
indicates that one is supposed to substitute in the contents of A into
the command string.  'C/..inherit/"**A 'some text' **B"' indicates
that C when read shall return the contents of A followed by 'some text'
as a delimiter followed by the contents of B.

'A<-B' is the same as A/..copy/B, and copies the contents of B to A.

So, let us discuss the following example:

Assume 357 is the user id of Michael Jackson.

ascii_command = '/home/teletubbies/..new/(..name/glove_location, ..object_t/audit/encrypted, (..perm_t/acl));glove_location/..acl/..new(uid/357, access/denied)); ..audit/..new/mailto/"teletubbies@pbs.org"; glove_location_file/..copy/"we stole it quite some number of years ago, and put it in the very first loot pile (in the hole near the purple flower).';

reiser4(&ascii_command, ascii_command_length, stack_binary_parameters_referenced_in_ascii_command, stack_length);

struct {
char * loot_inventory_buffer = &loot_inventory_buffer;
int buffer_length = sizeof(loot_inventory_buffer);
int * bytes_written = &bytes_written;
}  __attribute__ ((__packed__)) stack_binary_parameters_referenced_in_ascii_command;

ascii_command = "..memory_address_space/range/%p/%d/%p,/home/teletubbies/complete_loot_inventory)"

reiser4(&ascii_command, ascii_command_length, stack_binary_parameters_referenced_in_ascii_command, sizeof(stack_binary_parameters_referenced_in_ascii_command));


*/

/*
  We need some examples of creating an object with default and
  non-default plugin ids.
  
  This is oddly non-trivial, I suppose the following could be used but I
  don't like it:

  creat("/filenameA/..plugin/plugin_instance/plugin_params", flags);

  The reason I don't like it is that it somehow suggests to the naive
  user that we are creating something within filenameA, when actually we
  are creating filenameA and setting something within filenameA at the
  time of creation.

  I had originally intended:

  creat("/filenameA", flags);
  reiser4("/filenameA/..plugin<=plugin_instance/plugin_params");

  but this would unnecessarily create a temporary file with the default
  plugin before creating it with the desired one.

  Comments?
*/
