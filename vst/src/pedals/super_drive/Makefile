#!/usr/bin/make -f

NAME = SuperDrive

FILES_DSP = \
	SuperDrivePlugin.cpp

FILES_UI = \
	SuperDrive_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
