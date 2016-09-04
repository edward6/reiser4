#ifndef __FIBER_H__
#define __FIBER_H__

extern int reiser4_fiber_load(reiser4_subvol *, __u64 len,
			      reiser4_block_nr loc, int pin_jnodes);
extern void reiser4_fiber_done(struct reiser4_subvol *);

#endif /* __FIBER_H__ */

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
