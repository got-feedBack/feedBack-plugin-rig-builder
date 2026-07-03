#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Tracer V8" — a parody-named clone of the Trace Elliot V-Type V8 (400 W all-
// valve bass head, the game gear "CS240B"). All-ECC83 preamp (Gain I/II +
// Bright/Pull) + Trace tone stack (Deep/Shift) + opto compressor + 8x KT88
// push-pull, modeled from the Trace Elliot V-Type V8 schematic (cd0119/cd0120).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "TracerV8"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:tracerv8"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.tracerv8"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Tcv8

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     960
#define DISTRHO_UI_DEFAULT_HEIGHT    280
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
