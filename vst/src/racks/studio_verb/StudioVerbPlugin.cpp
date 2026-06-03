/* StudioVerb — Rocksmith reverb rack (Studio hall reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioVerb"
#define REVERB_DESC  "Studio hall reverb"
#define REVERB_UID   d_cconst('R','V','b','1')
#define REVERB_SIZE  1.00f
#define REVERB_DAMP  0.00f
#define REVERB_APFB  0.50f
#include "../_shared/reverb_plugin.hpp"
