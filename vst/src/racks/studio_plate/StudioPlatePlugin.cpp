/* StudioPlate — Rocksmith reverb rack (Studio plate reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioPlate"
#define REVERB_DESC  "Studio plate reverb"
#define REVERB_UID   d_cconst('R','P','l','1')
#define REVERB_SIZE  0.68f
#define REVERB_DAMP  -0.05f
#define REVERB_APFB  0.62f
#include "../_shared/reverb_plugin.hpp"
