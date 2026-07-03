#!/usr/bin/make -f

NAME = StudioChorus

FILES_DSP = \
	StudioChorusPlugin.cpp

FILES_UI = \
	StudioChorus_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
