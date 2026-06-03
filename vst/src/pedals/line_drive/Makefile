#!/usr/bin/make -f

NAME = LineDrive

FILES_DSP = \
	LineDrivePlugin.cpp

FILES_UI = \
	LineDrive_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
