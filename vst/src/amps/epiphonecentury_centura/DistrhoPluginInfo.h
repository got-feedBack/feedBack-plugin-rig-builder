#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Epicall Centura" — a parody-named clone of the Epiphone Electar Century
// (75th reissue) all-tube guitar head (Rocksmith gear "EpiphoneElectarCentury").
// Two 12AX7 stages + passive tone + input voicing + 2x 6V6 push-pull, modeled
// from the Century 75th-reissue schematic + panel.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "EpicallCentura"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:epicallcentura"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.epicallcentura"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Ecnt

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
