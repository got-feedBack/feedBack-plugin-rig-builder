#!/usr/bin/make -f
# Hipzon GA-88 — Gibson GA-88S stereo guitar amp model (DPF)

NAME = HipzonGA88

FILES_DSP = \
	Ga88Plugin.cpp

FILES_UI = \
	Ga88_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
