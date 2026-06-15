#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Citrus Rumbleverb 50" — a parody-named clone of the Orange Rockerverb 50 MkII
// 2-channel all-tube guitar head (the game gear "OrangeRockerverb"). Shared
// 12AX7 input, Clean + Dirty channels, valve spring reverb, 2x EL34 push-pull,
// modeled from the Rockerverb 50 schematic + panel.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "CitrusRumbleverb50"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:citrusrumbleverb50"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.citrusrumbleverb50"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Crv5

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     1000
#define DISTRHO_UI_DEFAULT_HEIGHT    300
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
