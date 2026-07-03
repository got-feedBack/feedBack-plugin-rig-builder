/* StudioPlate - EMT 140-style plate model. Same Time/Tone/Depth/Mix surface as
 * the game, but the DSP is now a plate-specific modal/FDN tank instead of the
 * generic Freeverb rack core. */
#define REVERB_LABEL "StudioPlate"
#define REVERB_DESC  "Studio plate reverb"
#define REVERB_UID   d_cconst('R','P','l','1')
#define REVERB_MODEL_CLASS Emt140PlateCore
#define REVERB_WETMAX 0.55f
#include "../../_shared/reverb_model_plugin.hpp"
