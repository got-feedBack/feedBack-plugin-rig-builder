#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// "Bender Fumble 800" — a parody-named clone of the Fender Rumble 800 (modern
// Class-D bass head, the game gear "CS300B"). Solid-state Gain + Bright/Contour/
// Vintage voicing + footswitch-style Overdrive (Drive/Level) + 4-band EQ +
// Class-D power, modeled from the Rumble 800 front panel.
#define DISTRHO_PLUGIN_BRAND   "RigBuilder"
#define DISTRHO_PLUGIN_NAME    "BenderFumble800"
#define DISTRHO_PLUGIN_URI     "urn:rigbuilder:benderfumble800"
#define DISTRHO_PLUGIN_CLAP_ID "rigbuilder.benderfumble800"

#define DISTRHO_PLUGIN_BRAND_ID  Rgbd
#define DISTRHO_PLUGIN_UNIQUE_ID Bf80

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     920
#define DISTRHO_UI_DEFAULT_HEIGHT    200
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
