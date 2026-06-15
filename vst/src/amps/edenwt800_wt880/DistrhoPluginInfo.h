#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Aiden GT-880" — a parody-named clone of the Eden WT-880 "World Tour"
// Valve-Tech hybrid bass head (the game gear "EdenWt800"). Eden Valve-Tech
// preamp + 3-band semi-parametric EQ + bi-amp crossover, modeled from the Eden
// WT-800C factory schematic.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "AidenGT880"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:aidengt880"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.aidengt880"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Ag88

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     1100
#define DISTRHO_UI_DEFAULT_HEIGHT    300
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
