#!/usr/bin/env python

import re
import os
import subprocess
import argparse
import random
import time


description = "Python script to generate random rinaperf clients"
epilog = "2017 Vincenzo Maffione <v.maffione@gmail.com>"

argparser = argparse.ArgumentParser(description = description,
                                    epilog = epilog)
argparser.add_argument('-T', type = int, default = 2,
                       help = "Average time between two client spawns")
argparser.add_argument('-D', type = int, default = 2,
                       help = "Average duration of a client")
argparser.add_argument('-M', type = int, default = 40,
                       help = "Max number of active clients at any time")
argparser.add_argument('--filter-name', type = str, default = 'rinaperf',
                       help = "String to use for filtering registered names")
args = argparser.parse_args()

if args.T < 1 or args.T > 100:
    print("Invalid parameter T: it must be 1 <= T <= 100")
    quit()

if args.D < 1 or args.D > 100:
    print("Invalid parameter D: it must be 1 <= D <= 100")
    quit()

# List all the ipcps to extract all the DIF names
try:
    o = subprocess.check_output(['rlite-ctl'])
    o = str(o)
    difs = re.findall(r"dif_name = '([^']+)'", o)
except:
    print("Failed to get list of IPCPs")
    quit()

if len(difs) == 0:
    print("No DIFs found")
    quit

# Look for registered applications that contain 'rinaperf' in their name
destinations = []
for dif in difs:
    try:
        o = subprocess.check_output(['rlite-ctl', 'dif-rib-show', dif])
        o = str(o)
        applications = re.findall(r"Application:\s+([^,]+)", o)
        for a in applications:
            if args.filter_name in a:
                destinations.append((dif, a))
    except Exception as e:
        print("Failed to show RIB for DIF %s" % dif)
        quit()

if len(destinations) == 0:
    print("No destination application found")
    quit()
print(destinations)

# Generate clients
random.seed()
clients = []
try:
    while 1:
        # Collect non-terminated clients
        nclients = []
        for p in clients:
            if p.poll() == None:
                nclients.append(p)
            else:
                print("process terminated")
        clients = nclients

        # Sleep for a random time (T on average)
        time.sleep(random.randint(0, 2*args.T))

        # Limit the number of active clients
        if len(clients) >= args.M:
            continue

        # Generate a new client
        dur = random.randint(1, 2*args.D-1)
        sel = random.randint(0, len(destinations)-1)
        interval = random.randint(300, 2000) # us
        pktlen = random.randint(2, 1000)
        sizespec = '' if pktlen > 800 else "-s %d" % pktlen
        dif, appl = destinations[sel]
        cmd = 'rinaperf -t perf %s -i %d -d %s -z %s -D %d' % (sizespec, interval, dif, appl, dur)
        try:
            p = subprocess.Popen(cmd.split())
            clients.append(p)
        except Exception as e:
            print('Failed to run rinaperf client')
            print(e)
except KeyboardInterrupt:
    print("Bye")
