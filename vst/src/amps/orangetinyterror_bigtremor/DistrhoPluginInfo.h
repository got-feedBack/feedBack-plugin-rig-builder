#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Citrus Big Tremor" — a parody-named clone of the Orange Tiny Terror single-
// channel all-tube guitar head (the game gear "OrangeTinyTerror"). Two 12AX7
// gain stages + passive tone + 2x EL84 cathode-biased push-pull, modeled from
// the Tiny Terror schematic + panel.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "CitrusBigTremor"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:citrusbigtremor"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.citrusbigtremor"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Cbtr

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
