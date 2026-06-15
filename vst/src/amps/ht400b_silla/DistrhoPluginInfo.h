#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Silla Boogie 400" — a parody-named clone of the Mesa/Boogie Bass 400+
// all-tube head (the game gear "HT400B"). 12AX7 preamp + tone stack + the
// Mesa 6-band graphic EQ + 12x 6L6 push-pull, modeled from the Bass 400+ panel
// and documented circuit.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "SillaBoogieBass400"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:sillaboogiebass400"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.sillaboogiebass400"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Sb40

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
