#!/usr/bin/make -f

NAME = BitCruncher

FILES_DSP = \
	BitCruncherPlugin.cpp

FILES_UI = \
	BitCruncher_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
