/* StudioChamber — Rocksmith reverb rack (Studio chamber reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioChamber"
#define REVERB_DESC  "Studio chamber reverb"
#define REVERB_UID   d_cconst('R','C','h','1')
#define REVERB_SIZE  0.78f
#define REVERB_DAMP  0.12f
#define REVERB_APFB  0.55f
#define REVERB_WETMAX 0.14f  // Mix tops out at 14% wet — chamber was UNCAPPED (default 1.0),
                             // i.e. up to 100% wet = the "too exaggerated" tail. 0.14 keeps it a
                             // subtle, musical chamber (tail ~-34..-38 dB vs the note), just a touch
                             // more present than the 0.10 hall verb. (2026-06)
#include "../../_shared/reverb_plugin.hpp"
