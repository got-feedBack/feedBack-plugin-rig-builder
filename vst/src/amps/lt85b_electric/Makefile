#!/usr/bin/make -f
# Electric B600F — Acoustic B600H 600W solid-state bass head model (DPF plugin)

NAME = ElectricB600F

FILES_DSP = \
	ElectricPlugin.cpp

FILES_UI = \
	Electric_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
