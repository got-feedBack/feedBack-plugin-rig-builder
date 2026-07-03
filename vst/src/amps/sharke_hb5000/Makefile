#!/usr/bin/make -f
# Sharke HB5000 — Hartke HA5000 bass head model (DPF plugin)

NAME = SharkeHB5000

FILES_DSP = \
	SharkePlugin.cpp

FILES_UI = \
	Hartke_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
