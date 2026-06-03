#!/usr/bin/make -f

NAME = GermaniumDrive

FILES_DSP = \
	GermaniumDrivePlugin.cpp

FILES_UI = \
	GermaniumDrive_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
