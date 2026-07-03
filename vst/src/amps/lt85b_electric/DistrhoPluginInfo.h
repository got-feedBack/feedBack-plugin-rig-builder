#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Electric B600F" — a parody-named clone of the Acoustic B600H (600 W solid-
// state bass head, the game gear "LT85B"). Gain/Volume preamp + sweepable
// Notch + 6-band EQ + Class-D power, modeled from the Acoustic B450/B600H
// schematic + panel.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "ElectricB600F"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:electricb600f"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.electricb600f"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Eb6f

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     1100
#define DISTRHO_UI_DEFAULT_HEIGHT    200
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
