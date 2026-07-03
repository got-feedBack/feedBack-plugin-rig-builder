#!/usr/bin/make -f
# Epicall Centura — Epiphone Electar Century 15W guitar head model (DPF)

NAME = EpicallCentura

FILES_DSP = \
	CenturaPlugin.cpp

FILES_UI = \
	Centura_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
