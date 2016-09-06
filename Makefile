#
#  Specify the target (to avoid in Makefile.target)
#
all:

#
#  Application file
#
APPLNAME = tinyip
APPL_CXXOBJS = ethernet.o ip.o arp.o icmp.o udp.o tcp.o
USE_CXX = true
APPL_CFG = $(APPLNAME).cfg

DEBUG = true
OMIT_OPTIMIZATION = true

#
#  Define the directory of MBED library
#
MBED_LIB_DIR = ../../mbed-lib

#
#  Define the directory of ASP kernel
#
SRCDIR = ../../asp-1.9.2-utf8

#
#  Include Makefiles of MBED library
#
include $(MBED_LIB_DIR)/common/Makefile.cmn
include $(MBED_LIB_DIR)/mbed-src/Makefile.src
#include $(MBED_LIB_DIR)/EthernetInterface/Makefile.eif
#include $(MBED_LIB_DIR)/mbed-rpc/Makefile.rpc
#include $(MBED_LIB_DIR)/HttpServer_snapshot/Makefile.hss
#include $(MBED_LIB_DIR)/FATFileSystem/Makefile.ffs
#include $(MBED_LIB_DIR)/i2c/Makefile.i2c
include $(MBED_LIB_DIR)/Makefile.mbd

#
#  Include the Makefile of ASP kernel
#
KERNEL_LIB = .
include $(SRCDIR)/Makefile.asp

install:
	mv asp.bin tinyip.bin
	cp tinyip.bin /media/daiki/MBED
