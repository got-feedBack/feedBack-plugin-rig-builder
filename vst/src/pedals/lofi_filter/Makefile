#!/usr/bin/make -f

NAME = LoFiFilter

FILES_DSP = \
	LoFiFilterPlugin.cpp

FILES_UI = \
	LoFiFilter_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
