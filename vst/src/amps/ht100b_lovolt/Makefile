#!/usr/bin/make -f
# Lovolt 100 — Custom Hiwatt 100 (DR103) all-tube head model (DPF plugin)

NAME = Lovolt100

FILES_DSP = \
	LovoltPlugin.cpp

FILES_UI = \
	Lovolt_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
