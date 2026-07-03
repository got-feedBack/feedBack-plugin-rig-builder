#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Aiden GT-550" — a parody-named clone of the Eden WT-550 "Traveler 550"
// Valve-Tech hybrid bass preamp (the game gear "EdenWT550"). Same Valve-Tech
// Twin-Triode preamp as the WT-300 in the 550 W head; modeled from the Eden
// WT-550 preamp schematic (WT550PreEQ).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "AidenGT550"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:aidengt550"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.aidengt550"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Ag55

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     1000
#define DISTRHO_UI_DEFAULT_HEIGHT    300
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
