#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Epicall Ruby" — a parody-named clone of the Epiphone Electar Zephyr Amp20
// (~1949, Danelectro-built) vintage guitar amp (the game gear "EpiphoneZephyr").
// 12AX7 preamp + passive Bass/Treble + 2x 6L6G push-pull, modeled from the 1949
// Zephyr schematic; the game exposes Volume/Bass/Treble.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "EpicallRuby"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:epicallruby"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.epicallruby"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Erby

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     900
#define DISTRHO_UI_DEFAULT_HEIGHT    300
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
