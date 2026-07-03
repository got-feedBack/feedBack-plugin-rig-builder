#!/usr/bin/make -f

NAME = BassOverdrive

FILES_DSP = \
	BassOverdrivePlugin.cpp

FILES_UI = \
	BassOverdrive_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
