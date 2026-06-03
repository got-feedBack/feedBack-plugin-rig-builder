#!/usr/bin/make -f

NAME = BassFlanger

FILES_DSP = \
	BassFlangerPlugin.cpp

FILES_UI = \
	BassFlanger_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
