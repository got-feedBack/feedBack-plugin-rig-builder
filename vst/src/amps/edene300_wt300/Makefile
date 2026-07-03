#!/usr/bin/make -f
# Aiden GT-300 — Eden WT-300 Valve-Tech hybrid bass preamp model (DPF plugin)

NAME = AidenGT300

FILES_DSP = \
	EdenPlugin.cpp

FILES_UI = \
	Eden_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
