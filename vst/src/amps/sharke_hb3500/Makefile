#!/usr/bin/make -f
# Sharke HB3500 — Hartke HA3500 bass head model (DPF plugin)

NAME = SharkeHB3500

FILES_DSP = \
	SharkePlugin.cpp

FILES_UI = \
	Hartke_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
