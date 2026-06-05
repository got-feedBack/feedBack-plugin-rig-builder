#!/usr/bin/make -f
# Bender Fumble 800 — Fender Rumble Bass all-tube head model (DPF plugin)

NAME = BenderFumble800

FILES_DSP = \
	RumblePlugin.cpp

FILES_UI = \
	Rumble_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
