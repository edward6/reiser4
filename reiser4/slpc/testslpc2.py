import string
import sys
import os

FILENAME=sys.argv[1]
INFILE=open(FILENAME, "r")
lines=INFILE.readlines ();
INFILE.close ();

plots_tab = { }
procs_tab = { }
ents_tab  = { }

for l in lines:
    fields = string.split (l)

    if fields[0] == "PROCS":
        continue

    procs = string.atoi (fields[0])
    totcy = string.atof (fields[2]) / 1000000000.0
    inscy = string.atoi (fields[3])
    seacy = string.atoi (fields[4])
    delcy = string.atoi (fields[5])
    appcy = string.atoi (fields[6])
    mix   = string.replace (fields[11], "/", "-")
    locks = fields[13]
    keys  = string.atoi (fields[14])

    plot_key = ("%d-" % keys) + locks + "-" + mix
    ent_key  = plot_key + ("-%d" % procs)

    plots_tab[plot_key] = plot_key
    procs_tab[procs]    = procs

    if not ents_tab.has_key (ent_key):
        ents_tab[ent_key] = (totcy, inscy, seacy, delcy, appcy, 1)
    else:
        ent = ents_tab[ent_key]

        totcy_acc = ent[0]
        inscy_acc = ent[1]
        seacy_acc = ent[2]
        delcy_acc = ent[3]
        appcy_acc = ent[4]
        count_acc = ent[5]

        ent = (totcy_acc + totcy,
               inscy_acc + inscy,
               seacy_acc + seacy,
               delcy_acc + delcy,
               appcy_acc + appcy,
               count_acc + 1)

        ents_tab[ent_key] = ent

GNUPLOT=open("testslpc2-gnuplot", "w")

GNUPLOT.write ("#!/usr/local/bin/gnuplot -persist\n")
GNUPLOT.write ("set grid\n")
GNUPLOT.write ("set xlabel \"Processors\"\n")
GNUPLOT.write ("set ylabel \"Cycles\"\n")
GNUPLOT.write ("set data style linespoints\n")
GNUPLOT.write ("set pointsize 2\n")
GNUPLOT.write ("set terminal gif\n")

for mix in ("10-80-10-0", "25-50-25-0", "33-34-33-0", "40-20-40-0"):
    for locks in ("ex", "rw"):
        dataf = mix + "-" + locks + ".out"
        plotf = mix + "-" + locks + ".gif"
        PLOTOUT=open(dataf, "w")
        pstr = ""
        for pc in procs_tab.keys ():
            pstr = ""
            str = ""
            col = 2
            for skeys in (400, 40000, 4000000, 40000000):
                ent_key = ("%d-%s-%s-%d" % (skeys, locks, mix, pc))

                ent = ents_tab[ent_key]

                count = ent[5]
                totcy = ent[0] / count
                #inscy = ent[1] / count
                #seacy = ent[2] / count
                #delcy = ent[3] / count
                #appcy = ent[4] / count

                str = str + ("%d " % totcy)

                desc = ""

                if locks == "rw":
                    desc = "Read-write"
                else:
                    desc = "Exclusive"

                if pstr != "":
                    pstr = pstr + ", "

                pstr = pstr + "'" + dataf + "' using 1:" + ("%d" % col) + " title \"" + desc + (" %d keys" % skeys) + "\""

                col = col + 1

            PLOTOUT.write (("%d " % pc) + str + "\n")

        PLOTOUT.close ()

        GNUPLOT.write ("set output '" + plotf + "'\n")
        GNUPLOT.write ("plot " + pstr + "\n")

GNUPLOT.close ()

os.system ("chmod +x testslpc2-gnuplot")
