/* StudioPlate — Rocksmith reverb rack (Studio plate reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioPlate"
#define REVERB_DESC  "Studio plate reverb"
#define REVERB_UID   d_cconst('R','P','l','1')
#define REVERB_SIZE  0.68f
#define REVERB_DAMP  -0.05f
#define REVERB_APFB  0.62f
#define REVERB_WETMAX 0.14f  // Mix tops out at 14% wet — plate was UNCAPPED (default 1.0),
                             // i.e. up to 100% wet = the "too exaggerated" tail. 0.14 keeps it a
                             // subtle, musical plate (tail ~-34..-38 dB vs the note), just a touch
                             // more present than the 0.10 hall verb. (2026-06)
#include "../../_shared/reverb_plugin.hpp"
