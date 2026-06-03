/* StudioChamber — Rocksmith reverb rack (Studio chamber reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioChamber"
#define REVERB_DESC  "Studio chamber reverb"
#define REVERB_UID   d_cconst('R','C','h','1')
#define REVERB_SIZE  0.78f
#define REVERB_DAMP  0.12f
#define REVERB_APFB  0.55f
#include "../_shared/reverb_plugin.hpp"
