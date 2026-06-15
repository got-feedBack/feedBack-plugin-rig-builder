#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Sharke HB5000" — a parody-named clone of the Hartke HA5000 bass head
// (the game gear "CS300B"): tube + solid-state preamp blend, compressor,
// 10-band graphic EQ, variable low/high-pass, master. Modeled from the
// HA5000 circuit diagram (Samson/Hartke, board 4005182801).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "SharkeHB5000"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:sharkehb5000"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.sharkehb5000"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Shk5

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     960
#define DISTRHO_UI_DEFAULT_HEIGHT    300
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
