obj-m := XWinMSRk.o
KVERSION = $(shell uname -r)
DESTDIR = $(HOME)

all:	XWinMSR
	make -C /lib/modules/$(KVERSION)/build M=${PWD} modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=${PWD} clean
	rm XWinMSR
XWinMSR:	XWinMSR.o
	gcc -o XWinMSR XWinMSR.c
XWinMSR.o:	XWinMSR.c
	gcc -c XWinMSR.c -o XWinMSR.o
