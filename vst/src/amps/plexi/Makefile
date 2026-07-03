#!/usr/bin/make -f

NAME = Plexi

FILES_DSP = \
	PlexiPlugin.cpp

FILES_UI = \
	Plexi_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
