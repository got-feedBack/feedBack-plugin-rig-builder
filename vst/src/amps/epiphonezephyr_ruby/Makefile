#!/usr/bin/make -f
# Epicall Ruby — Epiphone Electar Zephyr Amp20 vintage guitar amp model (DPF)

NAME = EpicallRuby

FILES_DSP = \
	RubyPlugin.cpp

FILES_UI = \
	Ruby_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
