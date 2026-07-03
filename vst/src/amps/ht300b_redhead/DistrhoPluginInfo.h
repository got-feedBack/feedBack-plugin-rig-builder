#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "SBR Super Redhead" — a parody-named clone of the SWR Super Redhead (all-tube
// preamp / 350 W bass head, the game gear "HT300B"). 12AX7 preamp + Aural
// Enhancer + semi-parametric tone + SS power, modeled from the SWR Red Head
// preamp schematic.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "SbrRedhead"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:sbrredhead"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.sbrredhead"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Sbrh

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     980
#define DISTRHO_UI_DEFAULT_HEIGHT    200
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
