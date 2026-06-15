#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Sampleg V-4B" — a parody-named clone of the Ampeg V-4B all-tube bass head
// (the game gear "CS75B"). Preamp + tone stack modeled from the 1971 V-4B
// factory schematic; power section is the 4x 7027A push-pull (~100W).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "SamplegV4B"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:samplegv4b"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.samplegv4b"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Sv4b

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     840
#define DISTRHO_UI_DEFAULT_HEIGHT    256
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
