#!/usr/bin/make -f
# Citrus AD200 — Orange AD200B all-tube bass head model (DPF plugin)

NAME = CitrusAD200

FILES_DSP = \
	CitrusPlugin.cpp

FILES_UI = \
	Citrus_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
