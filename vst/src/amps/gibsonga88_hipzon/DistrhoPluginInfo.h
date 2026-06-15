#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Hipzon GA-88" — a parody-named clone of the Gibson GA-88S stereo guitar amp
// (the game gear "GibsonGA88"). 12AX7 preamp + passive Bass/Treble + 4x 6BQ5
// (EL84) push-pull, modeled from the GA-88S schematic (mono). RS: Volume/Bass/Treble.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "HipzonGA88"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:hipzonga88"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.hipzonga88"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Hg88

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
