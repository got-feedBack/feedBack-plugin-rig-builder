/* StudioChamber - Lexicon PCM70 Rich Chamber-style model. Same Time/Tone/Depth/Mix
 * surface as the game, mapped to the PCM70 chamber controls: reverb time,
 * high-frequency cutoff, size/diffusion/definition and wet/dry mix. */
#define REVERB_LABEL "StudioChamber"
#define REVERB_DESC  "Studio chamber reverb"
#define REVERB_UID   d_cconst('R','C','h','1')
#define REVERB_MODEL_CLASS Pcm70RichChamberCore
#define REVERB_WETMAX 1.0f
#define REVERB_VERSION_PATCH 1
#include "../../_shared/reverb_model_plugin.hpp"
