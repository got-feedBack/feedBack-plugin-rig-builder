#!/usr/bin/make -f
# QTron Envelope Filter — DPF plugin

NAME = AutoSweep

FILES_DSP = \
	QTronPlugin.cpp

FILES_UI = \
	QTronUI.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

# Only build VST3 (that's what the Slopsmith engine scans). AU added too in
# case it's useful as a fallback format.
TARGETS += vst3
TARGETS += au

all: $(TARGETS)
