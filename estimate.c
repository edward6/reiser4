
/* maximal cost in leaf nodes of deleting an item (left and right are wandered, current disappears but not immediately)*/
#define ESTIMATE_ITEM_DELETE 2

/* maximal cost in leaf nodes of inserting an item (left, right, new, and current are wandered) */
#define ESTIMATE_ITEM_INSERT 4

/* maximal cost in leaf nodes of updating an item (current is wandered)*/
#define ESTIMATE_ITEM_UPDATE 1

estimate_rename()
{

/* we ignore internal nodes because we have some percent of the device
   space in reserve, and no set of changes to internal nodes can
   exceed that reserve and leave us with internal nodes whose children
   can fit onto this disk drive because we know what worst case fan
   out is. */

/* if we ever get a rename that does more than insert one item and
   delete one item and update a parent directory stat data, we'll need
   to recode this. */
	return ESTIMATE_ITEM_DELETE + ESTIMATE_ITEM_INSERT + ESTIMATE_ITEM_UPDATE;

}
