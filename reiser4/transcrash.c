/* Syntax: definition by Mr. Demidov goes here */

/* takes TRANSCRASH_COMMAND_LIMIT number of commands, and executes
   them in a manner to ensure that either they all are committed to
   disk, or none of them are */

/* Note: if you complain that limiting how long it takes to perform
   the commands, rather than limiting the number of commands, would be
   better, all we can say is send us the code, and we will happily
   review it.  This code we choose to implement here is simple. */
