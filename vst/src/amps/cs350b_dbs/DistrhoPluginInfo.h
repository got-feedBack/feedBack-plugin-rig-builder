#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Marsten DBS 7400" — a parody-named clone of the Marshall DBS 7400 (Dynamic
// Bass System) solid-state bass head (the game gear "CS350B" / "CLH-350B"):
// op-amp gain, Bright/Deep, Lo/Hi Primary EQ, Depth-only compressor (fixed
// threshold + indicator LED), the real 9-band graphic EQ, volume. Modeled 1:1
// from the 7400 service schematic (boards 7400-60-0A/0B).
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "MarstenDBS7400"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:marstendbs7400"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.marstendbs7400"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Dbs4

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
