/* This code encrypts crypto items before flushing them to disk (as
   opposed to encrypting them after each write, which is more
   performance expensive).  

Unresolved issues:

  how do we flag an item as being a crypto item?  Or do we make crypto items distinct item types?  


*/

#if YOU_CAN_COMPILE_PSEUDO_CODE

void *
encrypt_slum_crypto_items(reiser4_key * current_slum_key)
{
	scan slum for items that are marked encrypt before flush and encrypt them;
}

#endif
