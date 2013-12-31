#!/usr/bin/python

# USAGE: pci_bind.py <interface serial index>

interface_pci = ["03:00.0", "03:00.1", \
                 "0f:00.0", "0f:00.1", "0f:00.2", "0f:00.3",
                 "1c:00.0", "1c:00.1", "1c:00.2", "1c:00.3"]

if __name__ == '__main__':
    import sys
    import os

    curdir = os.getcwd()
    os.chdir('/home/yonch/src/DPDK/tools/')

    if len(sys.argv) >= 2:
        bindto = "igb_uio"
        for c in sys.argv[1]:
            if c == '+':
                bindto = 'igb_uio'
            elif c == '-':
                bindto = 'igb'
            else:
                cmd = "./pci_unbind.py -b " + bindto + " " + interface_pci[int(c,16)]
                print "executing: ", cmd
                os.system(cmd)

    os.system('./pci_unbind.py --status')

    os.chdir(curdir)

