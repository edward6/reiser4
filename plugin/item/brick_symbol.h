/* "Brick symbol" contains internal and extermal brick IDs */

#if !defined( __FS_REISER4_BRICK_SYMBOL_H__ )
#define __FS_REISER4_BRICK_SYMBOL_H__

#include "../../forward.h"
#include "../../dformat.h"
#include "../../kassign.h"
#include "../../key.h"

extern int store_brick_symbol(const reiser4_key *key, void *data, int len);
extern int load_brick_symbol(const reiser4_key *key, void *data,
			     int len, int exact);
extern int kill_brick_symbol(const reiser4_key *key);
extern int brick_symbol_add(reiser4_subvol *subv);
extern int brick_symbol_del(reiser4_subvol *subv);
extern int brick_identify(reiser4_subvol *subv);

/* __FS_REISER4_BRICK_SYMBOL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
