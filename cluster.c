/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Contains cluster operations for cryptcompress object plugin (see
   http://www.namesys.com/cryptcompress_design.txt for details). */

/*         Concepts of clustering. Definition of cluster size.
	   Data clusters, page clusters, disk clusters. 
	   
   
   In order to compress plain text we first should split it into chunks.
   Then we process each chunk independently by the following function:
   
   void alg(char *input_ptr, int input_length, char *output_ptr, int *output_length);
   
   where:
   input_ptr is a pointer to the first byte of input chunk (that contains plain text),
   input_len is a length of input chunk,
   output_ptr is a pointer to the first byte of output chunk (that contains processed text),
   *output_len is a length of output chunk.
   
   the length of output chunk depends both on input_len and on the content of
   input chunk so input_len (which can be assigned arbitrary) defines a
   compression quality (the more input_len the better compression quality).
   For each cryptcompress file we assign special attribute - cluster size:
   
   CLUSTER SIZE IS A FILE ATTRIBUTE, WHICH MEANS MAXIMAL SIZE OF INPUT CHUNK
   THAT WE USE FOR COMPRESSION.
   
   So if we wanna compress 10K-file with cluster size 4K, we split this file
   into three chunks (first and second - 4K, third - 2K). Those chunks are
   clusters in the space of file offsets (DATA CLUSTERS).  
   
   We use only cluster sizes that represented as (PAGE_CACHE_SIZE << shift),
   where shift (= 0, 1, 2,... ) is a parameter, which is supposed to be stored
   in disk stat-data (we call this CLUSTER SHIFT).
   
   Inode mapping of cryptcompress files contains pages filled by plain text.
   Cluster size also defines clustering in address space. For example,
   101K-file with cluster size 16K (cluster shift = 2), which can be mapped
   into 26 pages, has 7 PAGE CLUSTERS: first six clusters contains 4 pages
   and one cluster contains 2 pages (for the file tail).
   
   We split each output (compressed) chunk into special items to provide
   tight packing of data on disk (currently only ctails hold compressed data).
   This set of items we call by DISK CLUSTER.

   Each cluster is defined like pages by its index (e.g. offset, but the unit
   is cluster size instead of PAGE_SIZE). Key offset of the first unit of the
   first item of each disk cluster is multiply of cluster index to provide
   consistency in key space.
   
   Obviously, all read/write/truncate operations should be going by clusters.
   For example, if we wanna read 40K of cryptcompress file with cluster size 16K
   from offset = 20K, we first need to read two clusters (of indexes 1, 2). This
   means that all main methods of cryptcompress object plugin call appropriate
   cluster operation.
   
   For the same index we use one structure (type reiser4_cluster_t) to represent
   all data/page/disk clusters.
*/
