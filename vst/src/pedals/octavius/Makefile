#!/usr/bin/make -f

NAME = Octavius

FILES_DSP = \
	OctaviusPlugin.cpp

FILES_UI = \
	Octavius_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
