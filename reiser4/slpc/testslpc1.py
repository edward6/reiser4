import string
import sys
import os

FILENAME=sys.argv[1]
INFILE=open(FILENAME, "r")
lines=INFILE.readlines ();
INFILE.close ();

ents_tab  = { }

for l in lines:
    fields = string.split (l)

    if fields[0] == "NODESZ":
        continue

    nodesz = string.atoi (fields[0])
    locks  = fields[1]
    inscy  = string.atoi (fields[3])
    seacy  = string.atoi (fields[4])
    delcy  = string.atoi (fields[5])

    ent_key = ("%d-" % nodesz) + locks

    if not ents_tab.has_key (ent_key):
        ents_tab[ent_key] = (inscy, seacy, delcy, 1)
    else:
        ent = ents_tab[ent_key]

        inscy_acc = ent[0]
        seacy_acc = ent[1]
        delcy_acc = ent[2]
        count_acc = ent[3]

        ent = (inscy_acc + inscy,
               seacy_acc + seacy,
               delcy_acc + delcy,
               count_acc + 1)

        ents_tab[ent_key] = ent

for size in (64, 128, 256, 512, 1024):
    str = ""
    for lock in ("rw", "ex", "no"):
        ent_key = ("%d" % size) + "-" + lock

        ent = ents_tab[ent_key]

        count = ent[3]

        inscy = ent[0] / count
        seacy = ent[1] / count
        delcy = ent[2] / count

        str = str + ("%d %d %d " % (inscy, seacy, delcy))

    print ("%d " % size) + str
