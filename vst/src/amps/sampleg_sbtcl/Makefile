#!/usr/bin/make -f
# Sampleg SBT-CL — Ampeg SVT-CL all-tube bass head model (DPF plugin)

NAME = SamplegSBTCL

FILES_DSP = \
	SvtPlugin.cpp

FILES_UI = \
	Svt_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
