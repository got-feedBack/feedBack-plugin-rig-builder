#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Citrus AD200" — a parody-named clone of the Orange AD200B (MK3) all-tube bass
// head (the game gear "OrangeAD200B"). 12AX7 input + Orange passive subtractive
// tone stack + 4x KT88 push-pull, modeled from the AD200B panel + manual (tone-
// stack character cross-checked against the BAJA AD200B emulator layout).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "CitrusAD200"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:citrusad200"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.citrusad200"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Ca20

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
