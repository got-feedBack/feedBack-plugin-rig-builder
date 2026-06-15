/* StudioVerb — the game reverb rack (Studio hall reverb). Shared Freeverb-style core; this file
 * only sets the voicing + identity. */
#define REVERB_LABEL "StudioVerb"
#define REVERB_DESC  "Studio hall reverb"
#define REVERB_UID   d_cconst('R','V','b','1')
#define REVERB_SIZE   1.10f   // long combs → smooth hall tail (0.50 was short/metallic = "phasey")
#define REVERB_DAMP   0.15f   // a touch of damping so the tail isn't ringy/bright
#define REVERB_APFB   0.50f   // more diffusion → denser, less fluttery
#define REVERB_WETMAX 0.10f   // Mix knob tops out at 10% wet so the verb stays subtle
#include "../../_shared/reverb_plugin.hpp"
