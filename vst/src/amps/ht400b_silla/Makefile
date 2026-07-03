#!/usr/bin/make -f
# Silla Boogie 400 — Mesa/Boogie Bass 400+ all-tube head model (DPF plugin)

NAME = SillaBoogieBass400

FILES_DSP = \
	SillaPlugin.cpp

FILES_UI = \
	Silla_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
