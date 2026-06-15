#ifndef DC30_PARAMS_H
#define DC30_PARAMS_H

/*
 * UNPARALLEL DC30 = Matchless DC30 — a hand-wired AC30-class boutique combo,
 * TWO independent channels into a shared 4xEL84 CLASS-A power amp (~30W) with
 * NO global negative feedback => very chimey, jangly, blooms & compresses at
 * high volume. Parody brand: the face must never read "Matchless".
 *
 * Local reference (hand-traced, 4 pages, Matchless DC30):
 *   CHANNEL 1 "Brilliant" — two 12AX7 stages: input 68k/1M; stage1 220k plate,
 *     25uF cathode bypass; coupling 560pF+180pF -> 500kA Volume; stage2 100k
 *     plate, 1k5 cathode + 25uF; then a VOX TOP-BOOST tone stack with TREBLE
 *     (220k, 56pF treble cap) and BASS (1M, .022) ONLY -- no mid. Bright/glassy.
 *   CHANNEL 2 "EF86" — one EF86 pentode (higher gain, fatter/darker): 330k+2M2
 *     plate, 2k2 cathode+25uF; a 6-position TONE rotary (caps 360p/56p/.0012/
 *     .0022/.0047/.01 with 1M5) modelled as a CONTINUOUS Tone sweeping
 *     dark(fat)->bright; then 180pF -> 1MA Volume. Thick midrange, more gain.
 *   SHARED — CUT (250kA, post/PI treble cut -> higher = darker), MASTER (1MA).
 *
 * the game gear: Amp_BT30. RS exposes only Gain/Bass/Mid/Treble, so RS Gain ->
 * "Ch1 Volume" (drives the EL84 breakup, Channel pinned to Ch1 Brilliant), and
 * Bass/Treble -> the Ch1 top-boost stack. The DSP channel-select morphs between
 * the Ch1 top-boost voice and the Ch2 EF86 voice.
 */
enum Dc30ParamId
{
    kCh1Volume = 0, // CH1 VOLUME — Brilliant input gain (drives the EL84 breakup) [RS Gain]
    kBass,          // BASS   — Ch1 top-boost bass        [RS Bass]
    kTreble,        // TREBLE — Ch1 top-boost treble      [RS Treble]
    kCh2Volume,     // CH2 VOLUME — EF86 channel gain
    kTone,          // TONE   — Ch2 EF86 6-position rotary (dark->bright)
    kCut,           // CUT    — shared post/PI treble cut (higher = darker)
    kMaster,        // MASTER — shared output master (Class-A bloom/compression)
    kChannel,       // channel: Ch1 Brilliant(0) / Ch2 EF86(1)
    kParamCount
};

static const char* const kDc30Names[kParamCount] = {
    "Ch1 Volume", "Bass", "Treble", "Ch2 Volume", "Tone", "Cut", "Master", "Channel",
};

static const char* const kDc30Symbols[kParamCount] = {
    "ch1volume", "bass", "treble", "ch2volume", "tone", "cut", "master", "channel",
};

static const float kDc30Min[kParamCount] = { 0,0,0,0,0,0,0,0 };
static const float kDc30Max[kParamCount] = { 1,1,1,1,1,1,1,1 };
// Defaults: Ch1 Brilliant selected, Volume up for that chimey breakup, top-boost
// roughly flat, EF86 channel at musical defaults, Cut mild, Master open.
static const float kDc30Def[kParamCount] = {
    0.60f, 0.50f, 0.60f, 0.50f, 0.50f, 0.40f, 0.70f, 0.0f,
};

#endif // DC30_PARAMS_H
