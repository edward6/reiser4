/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Contains cluster operations for cryptcompress object plugin (see
   http://www.namesys.com/cryptcompress_design.txt for details). */



/*         Concepts of clustering. Definition of cluster size.
	   Clusters in loff_t-space, address-space. Disk clusters. 
	     
   
   In order to process (compress/encrypt) plain text we first should split it
   into chunks. Then we process each chunk independently by the following
   function (we can assume for simplicity that this function looks the same for
   compression and encryption):
   
   alg(const char *input_ptr, const int input_length, char *output_ptr, int output_length);
   
   where:
   input_ptr is a pointer to the first byte of input chunk (that contains plain text),
   input_len is a length of input chunk,
   output_ptr is a pointer to the first byte of output chunk (that contains processed text),
   output_len is a length of output chunk.
   
   If alg() represents crypto algorithm, then input_len and output_len are
   completely defined by this algorithm. If this algorithm is symmetric,
   then input_len == output_len (== usually 8-16 bytes), and we call this
   by crypto atom size.
   
   If alg() represents compression algorithm, then output_len depends both
   on input_len and on the content of input chunk so input_len (which can
   be assigned arbitrary) defines a compression quality (the more input_len
   the better compression quality). For each cryptocompress file we assigned
   special attribute - cluster size,
   
   CLUSTER SIZE IS A FILE ATTRIBUTE, WHICH MEANS MAXIMAL SIZE OF INPUT CHUNK
   THAT WE USE FOR COMPRESSION.
   
   So if we wanna compress 10K-file with cluster size 4K, we compress first
   two chunks with input_len = 4K, and third chunk with input_len = 2K.
   We use only cluster sizes that represented as (PAGE_CACHE_SIZE << shift),
   where shift (= 0, 1, 2,... ) is a parameter, which is supposed to be stored
   in disk stat-data (we call this CLUSTER SHIFT).
   
   Inode mapping of cryptocompression files contains pages filled by plain
   text. The cluster attribute defines clustering in address space. For example,
   101K-file with cluster size 16K (cluster shift = 2) has 7 clusters:
   six 4-page clusters and one cluster which contains 2 pages (for the rest).
   Each output chunk which contains compressed data are split into special
   items to provide tight packing of this data on disk (currently only ctails
   hold compressed data). This set of items we call by DISK CLUSTER. Also
   we can define clusters in loff_t-space of file offsets. So 101K-file with
   cluster size 32K (cluster shift = 3) consists from three 32K clusters and
   one 5K cluster. Like pages, each cluster in loff-t space (and, therefore each
   cluster in address space, and each disk cluster (if file was written on
   disk)) is defined by its index (e.g. offset, but the unit is cluster size
   instead of PAGE_SIZE).
   
   Obviously, all read/write/truncate operations are going by clusters.
   For example, if we wanna read cryptcompress file with cluster size 16K
   from off1 = 20K to off2 = 40K, we first need to read two clusters with
   index = 1, 2. This means that all main methods of cryptcompress object
   plugin call appropriate cluster operation. For the same index we represent
   all loff_t-space/address-space/disk clusters by one "cluster handle"
   (type reiser4_cluster_t)
*/
