#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "PeeBee T-Minus" — a parody-named clone of the Peavey T-Max two-channel bass
// head (Rocksmith gear "BT600B"). Dual preamp (12AX7 tube + solid-state),
// shelving + 7-band graphic EQ, and a biamp Balance/X-Over, modeled from the
// Peavey T-Max preamp schematic (Peavey Electronics drawing 70985).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "PeeBeeTMinus"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:peebeetminus"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.peebeetminus"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Pbtm

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     1100
#define DISTRHO_UI_DEFAULT_HEIGHT    280
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
