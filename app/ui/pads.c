/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH: the black-key pads: their own settings (K1-K4 while held) and their animations. (Part of app.c's unity:
 * included there in order.) */

/* -------------------------------------------------- the pads' own settings --- */
/* what K1-K4 tune while a pad is held, by pad (brk.h PAD_*); 0: that knob does nothing for it */
static const char *const PAD_KNOB[BRK_NPADS][4] = {
    {"RATE", "DECAY", "PITCH", "LEVEL"}, {"RATE", "DECAY", "PITCH", "LEVEL"}, {"RATE", "DECAY", "PITCH", "LEVEL"},
    {"SPEED", 0, 0, 0}, {"LENGTH", "PITCH", 0, 0}, {"SPEED", 0, 0, 0}, {"STOP", "SPIN UP", 0, 0},
    {"RATE", "LENGTH", 0, 0}, {"AMOUNT", "PITCH", 0, 0}, {"TARGET", "RESO", "SPEED", 0},
    {"TIME", "FEEDBACK", "TONE", "MIX"}};
static const char *const PAD_TITLE[BRK_NPADS] = {"ROLL", "ROLL", "ROLL", "REVERSE", "FREEZE", "HALF SPEED",
                                                  "TAPE STOP", "GATE", "RANDOM", "FILTER", "DUB"};
static const char *const FRZ_LEN_NAME[4] = {"SLICE", "1/2", "1/4", "1/8"};

/* -1..1 as the DJ filter's frequency: "LP 340HZ" / "HP 1.2K" */
static void dj_text(float dj, char *t)
{
    float hz = dj > 0.0f ? 20.0f * brk_exp2f(dj * 8.2f) : 18000.0f * brk_exp2f(dj * 7.8f);
    t[0] = 0;
    text_cat(t, dj > 0.0f ? "HP " : "LP ");
    if (hz >= 1000.0f) {
        tenths_text((int32_t)(hz / 100.0f), t + 3);
        text_cat(t, "K");
    } else {
        int_text((int32_t)hz, t + 3);
        text_cat(t, "HZ");
    }
}

/* the roll's decay: "DOWN 20%" each repeat quieter, "UP 20%" a swell, "OFF" */
static void roll_decay_text(float dc, char *t)
{
    char v[8];
    t[0] = 0;
    if (dc > -0.01f && dc < 0.01f) {
        text_cat(t, "OFF");
        return;
    }
    text_cat(t, dc > 0.0f ? "DOWN " : "UP ");
    pct_text(dc > 0.0f ? dc : -dc, v);
    text_cat(t, v);
}

static void pad_knob_value(int pad, int k, char *t)
{
    const brk_padset_t *p = &set.pads;
    const brk_fx_t *f = &set.fx;
    t[0] = 0;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
        else if (k == 1) roll_decay_text(p->roll_decay, t);
        else if (k == 2) signed_tenths(p->roll_pitch, "ST", t);
        else pct_text((float)(p->roll_level ? p->roll_level : 100u) * 0.01f, t);
        break;
    case PAD_REVERSE: times_text(p->rev_speed, t); break;
    case PAD_FREEZE:
        if (k == 0) text_cat(t, FRZ_LEN_NAME[p->frz_len & 3u]);
        else signed_tenths(p->frz_pitch, "ST", t);
        break;
    case PAD_HALF: times_text(p->half_speed, t); break;
    case PAD_TAPESTOP:
        if (k == 0)
            signed_tenths(p->stop_beats, " BT", t);
        else
            signed_tenths(p->spin_steps, " ST", t);
        if (t[0] == '+') {               /* (no sign: always positive) */
            uint32_t j = 0;
            do
                t[j] = t[j + 1];
            while (t[j++]);
        }
        break;
    case PAD_GATE:
        if (k == 0) text_cat(t, BRK_GATE_DIV_NAME[p->gate_div % BRK_GATE_DIVS]);
        else pct_text(p->gate_len, t);
        break;
    case PAD_RANDOM:
        if (k == 0) pct_text(p->rnd_amount, t);
        else { int_text((int32_t)p->rnd_pitch, t); text_cat(t, "ST"); }
        break;
    case PAD_FILTER:
        if (k == 0) dj_text(f->flt_target, t);
        else if (k == 1) pct_text(f->flt_res, t);
        else pct_text(f->flt_speed, t);
        break;
    case PAD_DUB:                        /* with a DELAY in the chain, the pad throws into it: its settings */
        if (delay_slot() >= 0) fx_value((uint32_t)delay_slot(), k, t);
        else if (k == 0) text_cat(t, BRK_DUB_DIV_NAME[f->dub_div % BRK_DUB_DIVS]);
        else pct_text(k == 1 ? f->dub_fb : k == 2 ? f->dub_tone : f->dub_mix, t);
        break;
    default: break;
    }
}

static void pad_knob_turn(int pad, int k, int32_t d)
{
    brk_padset_t *p = &set.pads;
    brk_fx_t *f = &set.fx;
    char t[16];
    if (pad < 0 || pad >= BRK_NPADS || k < 0 || k > 3 || !PAD_KNOB[pad][k])
        return;
    switch (pad <= PAD_ROLL32 ? 0 : pad) {
    case 0:
        if (k == 0) p->roll_div[pad] = (uint8_t)clamp((int32_t)p->roll_div[pad] + d, 0, BRK_ROLL_DIVS - 1);
        else if (k == 1) p->roll_decay = knob_f(p->roll_decay, d, 0.02f, -0.9f, 0.9f);
        else if (k == 2) {               /* 0.1 a detent, 0.5 turned fast (a run of detents < 40 ms apart) */
            static uint32_t last, run;
            uint32_t now = plat_ms();
            float v;
            run = now - last < 40u ? run + 1u : 0u;
            last = now;
            v = knob_f(p->roll_pitch, d, run >= 3u || d > 2 || d < -2 ? 0.5f : 0.1f, -12.0f, 12.0f);
            p->roll_pitch = (float)(int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f)) * 0.1f;   /* (on the tenth) */
        }
        else p->roll_level = (uint8_t)clamp((int32_t)(p->roll_level ? p->roll_level : 100u) + d * 5, 5, 100);
        break;
    case PAD_REVERSE: p->rev_speed = knob_f(p->rev_speed, d, 0.05f, 0.5f, 2.0f); break;
    case PAD_FREEZE:
        if (k == 0) p->frz_len = (uint8_t)clamp((int32_t)p->frz_len + d, 0, 3);
        else p->frz_pitch = knob_f(p->frz_pitch, d, 1.0f, -12.0f, 12.0f);
        break;
    case PAD_HALF: p->half_speed = knob_f(p->half_speed, d, 0.05f, 0.25f, 0.75f); break;
    case PAD_TAPESTOP:
        if (k == 0) p->stop_beats = knob_f(p->stop_beats, d, 0.25f, 0.25f, 4.0f);
        else p->spin_steps = knob_f(p->spin_steps, d, 0.25f, 0.25f, 8.0f);
        break;
    case PAD_GATE:
        if (k == 0) p->gate_div = (uint8_t)clamp((int32_t)p->gate_div + d, 0, BRK_GATE_DIVS - 1);
        else p->gate_len = knob_f(p->gate_len, d, 0.05f, 0.1f, 0.9f);
        break;
    case PAD_RANDOM:
        if (k == 0) p->rnd_amount = knob_f(p->rnd_amount, d, 0.05f, 0.0f, 1.0f);
        else p->rnd_pitch = knob_f(p->rnd_pitch, d, 1.0f, 0.0f, 12.0f);
        break;
    case PAD_FILTER:
        if (k == 0) f->flt_target = knob_f(f->flt_target, d, 0.04f, -1.0f, 1.0f);
        else if (k == 1) f->flt_res = knob_f(f->flt_res, d, 0.02f, 0.0f, 0.95f);
        else f->flt_speed = knob_f(f->flt_speed, d, 0.02f, 0.0f, 1.0f);
        break;
    case PAD_DUB:
        if (delay_slot() >= 0) {         /* the DELAY slot's own settings */
            fx_knob_turn((uint32_t)delay_slot(), k, d);
            return;
        }
        if (k == 0) f->dub_div = (uint8_t)clamp((int32_t)f->dub_div + d, 0, BRK_DUB_DIVS - 1);
        else if (k == 1) f->dub_fb = knob_f(f->dub_fb, d, 0.02f, 0.0f, 0.95f);
        else if (k == 2) f->dub_tone = knob_f(f->dub_tone, d, 0.02f, 0.0f, 1.0f);
        else f->dub_mix = knob_f(f->dub_mix, d, 0.02f, 0.0f, 1.0f);
        break;
    default: break;
    }
    pad_knob_value(pad, k, t);
    app_say(PAD_KNOB[pad][k], t);
    changed();
}

/* ---------------------------------------------------------- pad pictures --- */
#define FX_Y 90                          /* the held pad's strip: 240 x 96 where the slice blocks are (its
                                          * picture: MOUSTIQUE doing the effect, demo.c draw_fx) */
#define FX_MID 58                        /* (keys.c: its waveform's centre line, in the strip) */
#define FX_BAND 48                       /* drawn in two bands of 48 rows (the canvas holds 60) */
