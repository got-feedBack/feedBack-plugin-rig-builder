/* StudioVerb - Lexicon 224-style digital hall. Same Time/Tone/Depth/Mix surface
 * as the game, but the DSP now uses input diffusion, an eight-line modulated
 * FDN, bandwidth limiting and slow wander instead of the generic Freeverb core. */
#define REVERB_LABEL "StudioVerb"
#define REVERB_DESC  "Studio hall reverb"
#define REVERB_UID   d_cconst('R','V','b','1')
#define REVERB_MODEL_CLASS Lexicon224VerbCore
#define REVERB_WETMAX 0.55f
#include "../../_shared/reverb_model_plugin.hpp"
