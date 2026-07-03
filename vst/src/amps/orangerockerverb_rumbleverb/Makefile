#!/usr/bin/make -f
# Citrus Rumbleverb 50 — Orange Rockerverb 50 MkII 2-channel guitar head (DPF)

NAME = CitrusRumbleverb50

FILES_DSP = \
	RumbleverbPlugin.cpp

FILES_UI = \
	Rumbleverb_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
