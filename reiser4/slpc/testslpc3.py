import string
import sys

FILENAME=sys.argv[1]
INFILE=open(FILENAME, "r")
lines=INFILE.readlines ();
INFILE.close ();

RBINSOUT=open("rb.ins.out", "w")
SLINSOUT=open("sl.ins.out", "w")

RBSEAOUT=open("rb.sea.out", "w")
SLSEAOUT=open("sl.sea.out", "w")

RBDELOUT=open("rb.del.out", "w")
SLDELOUT=open("sl.del.out", "w")

RBOVEROUT=open("rb.over.out", "w")
SLOVEROUT=open("sl.over.out", "w")

sl_cnt  = 0
sl_ins  = 0.0
sl_sea  = 0.0
sl_del  = 0.0
sl_over = 0.0

rb_cnt  = 0
rb_ins  = 0.0
rb_sea  = 0.0
rb_del  = 0.0
rb_over = 0.0

ak = 0

def output():
    global rb_ins, rb_cnt, rb_sea, rb_del, rb_over
    global sl_ins, sl_cnt, sl_sea, sl_del, sl_over

    RBINSOUT.write ("%d\t" % ak + "%d\t" % (rb_ins / rb_cnt) + "\n")
    RBSEAOUT.write ("%d\t" % ak + "%d\t" % (rb_sea / rb_cnt) + "\n")
    RBDELOUT.write ("%d\t" % ak + "%d\t" % (rb_del / rb_cnt) + "\n")
    RBOVEROUT.write ("%d\t" % ak + "%d\t" % (rb_over) + "\n")

    SLINSOUT.write ("%d\t" % ak + "%d\t" % (sl_ins / sl_cnt) + "\n")
    SLSEAOUT.write ("%d\t" % ak + "%d\t" % (sl_sea / sl_cnt) + "\n")
    SLDELOUT.write ("%d\t" % ak + "%d\t" % (sl_del / sl_cnt) + "\n")
    SLOVEROUT.write ("%d\t" % ak + "%d\t" % (sl_over) + "\n")

    sl_cnt  = 0
    sl_ins  = 0.0
    sl_sea  = 0.0
    sl_del  = 0.0
    sl_over = 0.0

    rb_cnt  = 0
    rb_ins  = 0.0
    rb_sea  = 0.0
    rb_del  = 0.0
    rb_over = 0.0

for l in lines:
  fields = string.split (l)

  if fields[0] == "TREE":
      continue

  avgkeys      = string.atoi (fields[1])
  insert_time  = fields[2]
  search_time  = fields[4]
  delete_time  = fields[6]
  overhead     = fields[9]

  if ak != 0 and ak != avgkeys:
      output ()

  ak = avgkeys

  if fields[0] == "SL":
      sl_ins  = sl_ins + string.atof (insert_time)
      sl_sea  = sl_sea + string.atof (search_time)
      sl_del  = sl_del + string.atof (delete_time)
      sl_over = string.atof (overhead)
      sl_cnt  = sl_cnt + 1
  elif fields[0] == "RB":
      rb_ins  = rb_ins + string.atof (insert_time)
      rb_sea  = rb_sea + string.atof (search_time)
      rb_del  = rb_del + string.atof (delete_time)
      rb_over = string.atof (overhead)
      rb_cnt  = rb_cnt + 1
  else:
      print "error: unrecognized line of input: " + l
      exit(1)

output ()

RBINSOUT.close ();
SLINSOUT.close ();

RBSEAOUT.close ();
SLSEAOUT.close ();

RBDELOUT.close ();
SLDELOUT.close ();

RBOVEROUT.close ();
SLOVEROUT.close ();
