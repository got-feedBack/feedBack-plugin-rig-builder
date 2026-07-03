#!/usr/bin/make -f
# Hipzon GA-79 RVT — Gibson GA-79 RVT stereo guitar amp model (DPF)

NAME = HipzonGA79RVT

FILES_DSP = \
	Ga79Plugin.cpp

FILES_UI = \
	Ga79_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
