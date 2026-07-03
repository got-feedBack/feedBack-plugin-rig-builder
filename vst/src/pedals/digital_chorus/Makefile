#!/usr/bin/make -f

NAME = DigitalChorus

FILES_DSP = \
	DigitalChorusPlugin.cpp

FILES_UI = \
	DigitalChorus_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
