/* SPDX-License-Identifier: MPL-2.0 */
/* BREAK KAP FM-1: the demo is MOUSTIQUE, the label's crane fly, dancing on the break (it replaces CHOPPAH's five
 * scenes). Left alone for a minute while stopped (three while playing), the screen shows it: white wire on black like
 * the logo, standing on six long bent legs over a floor of cubes; a foot steps on each slice played, it squats on the
 * kicks, its wings buzz; the black keys' effects move it: ROLLs (it shivers, wings buzzing), REVERSE (on its back, legs
 * kicking), FREEZE (frozen, ice blue), TAPE STOP (its legs give way), GATE (it walks, three legs then three), RANDOM
 * (legs and floor twitching), FILTER (colours going round), DUB (three ghosts trailing). The camera cuts to another
 * angle every two bars. Any key, button or knob ends it, and does nothing else.
 *
 * Drawn like the rest of the screen, in strips of 60 rows, and with nothing kept between strips: each strip projects
 * the scene again and fills what crosses it (no list of shapes: RAM is short). No libm: sines from brk_sin_turns, the
 * square root its own. (Part of app.c's unity: included there in order, after screen.c.) */

#define DEMO_IDLE_MS 60000u              /* stopped: a minute untouched */
#define DEMO_IDLE_PLAY_MS 180000u        /* playing: three */
#define DEMO_FRAME_MS 40u                /* 25 frames a second */

static struct {
    uint8_t on;
    int32_t step, slice;                 /* the engine's step and slice last seen (a new one: a foot steps) */
    uint32_t t0, drawn, rng, bar, shot, shot_bar;
    float beat, last, kick, mprev, squat;
} demo;
static uint8_t mq_kind;                  /* the character: 0 MOUSTIQUE, 1 PANTIN */
static uint16_t mq_col, mq_wingc;        /* its colours this frame */

/* ------------------------------------------------------------------------------------------------- maths --- */
typedef struct { float x, y, z; } mq_v3;
static mq_v3 mq_v(float x, float y, float z) { mq_v3 r; r.x = x; r.y = y; r.z = z; return r; }
static mq_v3 mq_vadd(mq_v3 a, mq_v3 b) { return mq_v(a.x + b.x, a.y + b.y, a.z + b.z); }
static mq_v3 mq_vsub(mq_v3 a, mq_v3 b) { return mq_v(a.x - b.x, a.y - b.y, a.z - b.z); }
static mq_v3 mq_vmul(mq_v3 a, float s) { return mq_v(a.x * s, a.y * s, a.z * s); }
static float mq_vdot(mq_v3 a, mq_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static mq_v3 mq_vlerp(mq_v3 a, mq_v3 b, float t) { return mq_vadd(a, mq_vmul(mq_vsub(b, a), t)); }
static float mq_mn(float a, float b) { return a < b ? a : b; }
static float mq_mx(float a, float b) { return a > b ? a : b; }
static float mq_sn(float rad) { return brk_sin_turns(rad * 0.15915494f); }
static float mq_cs(float rad) { return brk_sin_turns(rad * 0.15915494f + 0.25f); }
static float mq_sq(float x)                 /* a square root: a guess from the float's bits, two Newton steps */
{
    union { float f; uint32_t i; } u;
    if (x <= 0.0f)
        return 0.0f;
    u.f = x;
    u.i = 0x1FBD1DF5u + (u.i >> 1);
    u.f = 0.5f * (u.f + x / u.f);
    return 0.5f * (u.f + x / u.f);
}
static mq_v3 mq_vnorm(mq_v3 a) { float l = mq_sq(mq_vdot(a, a)); return l > 1e-6f ? mq_vmul(a, 1.0f / l) : a; }
/* turned: roll (about z), then pitch (about x), then yaw (about y) */
typedef struct { float cy, sy, cp, sp, cr, sr; } mq_rot_t;
static mq_rot_t mq_rot(float yaw, float pitch, float roll)
{
    mq_rot_t m;
    m.cy = mq_cs(yaw); m.sy = mq_sn(yaw); m.cp = mq_cs(pitch); m.sp = mq_sn(pitch); m.cr = mq_cs(roll); m.sr = mq_sn(roll);
    return m;
}
static mq_v3 mq_turn(const mq_rot_t *m, mq_v3 p)
{
    float x = p.x * m->cr - p.y * m->sr, y = p.x * m->sr + p.y * m->cr, y2 = y * m->cp - p.z * m->sp,
          z2 = y * m->sp + p.z * m->cp;
    return mq_v(x * m->cy + z2 * m->sy, y2, -x * m->sy + z2 * m->cy);
}

/* ------------------------------------------------------------------------------------------------- camera --- */
static mq_v3 mq_cam_e, mq_cam_r, mq_cam_u, mq_cam_f;
static float mq_cam_F;
static int32_t mq_sy0, mq_bh = 60;          /* the strip being filled: screen rows sy0 .. sy0+bh-1 */
static float mq_cy = 120.0f;             /* the screen row the camera looks at */

static int mq_proj(mq_v3 p, float *x, float *y, float *z)
{
    mq_v3 d = mq_vsub(p, mq_cam_e);
    float zz = mq_vdot(d, mq_cam_f);
    if (zz < 0.05f)
        return 0;
    *x = 120.0f + mq_vdot(d, mq_cam_r) * mq_cam_F / zz;
    *y = mq_cy - mq_vdot(d, mq_cam_u) * mq_cam_F / zz;
    *z = zz;
    return 1;
}

/* a convex polygon (screen points) filled into the strip */
static void mq_fill(const float *px, const float *py, uint32_t n, uint16_t c)
{
    float lo_y = py[0], hi_y = py[0];
    int32_t ya, yb, y, x, xa, xb;
    uint32_t i;
    uint16_t sc = swap16(c);
    for (i = 1; i < n; i++) {
        lo_y = mq_mn(lo_y, py[i]);
        hi_y = mq_mx(hi_y, py[i]);
    }
    if (hi_y < (float)mq_sy0 || lo_y >= (float)(mq_sy0 + mq_bh))
        return;
    ya = lo_y < (float)mq_sy0 ? mq_sy0 : (int32_t)lo_y;
    yb = hi_y > (float)(mq_sy0 + mq_bh - 1) ? mq_sy0 + mq_bh - 1 : (int32_t)hi_y;
    for (y = ya; y <= yb; y++) {
        float fy = (float)y + 0.5f, lo = 1e9f, hi = -1e9f;
        for (i = 0; i < n; i++) {
            float y0 = py[i], y1 = py[(i + 1u) % n];
            if ((fy >= y0 && fy < y1) || (fy >= y1 && fy < y0)) {
                float xx = px[i] + (px[(i + 1u) % n] - px[i]) * (fy - y0) / (y1 - y0);
                lo = mq_mn(lo, xx);
                hi = mq_mx(hi, xx);
            }
        }
        if (lo > hi)
            continue;
        xa = (int32_t)(lo + 0.5f);
        xb = (int32_t)(hi + 0.5f);
        if (xb == xa)
            xb++;                        /* (a sliver still shows: one pixel) */
        if (xa < 0)
            xa = 0;
        if (xb > 240)
            xb = 240;
        for (x = xa; x < xb; x++)
            cv_px[(uint32_t)(y - mq_sy0) * 240u + (uint32_t)x] = sc;
    }
}
static void mq_s_poly(const mq_v3 *pt, uint32_t n, uint16_t c)
{
    float px[6], py[6], z;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (!mq_proj(pt[i], &px[i], &py[i], &z))
            return;
    mq_fill(px, py, n, c);
}
/* a thick segment, from width wa to wb (world units; at least about a pixel) */
static void mq_s_seg2(mq_v3 a, mq_v3 b, float wa, float wb, uint16_t c)
{
    float ax, ay, az, bx, by, bz, dx, dy, l, px[4], py[4];
    if (!mq_proj(a, &ax, &ay, &az) || !mq_proj(b, &bx, &by, &bz))
        return;
    dx = bx - ax;
    dy = by - ay;
    l = mq_sq(dx * dx + dy * dy);
    if (l < 0.01f) {
        dx = 1.0f;
        dy = 0.0f;
        l = 1.0f;
    }
    dx /= l;
    dy /= l;
    wa = mq_mx(0.55f, wa * mq_cam_F / az * 0.5f);
    wb = mq_mx(0.55f, wb * mq_cam_F / bz * 0.5f);
    px[0] = ax - dy * wa; py[0] = ay + dx * wa;
    px[1] = bx - dy * wb; py[1] = by + dx * wb;
    px[2] = bx + dy * wb; py[2] = by - dx * wb;
    px[3] = ax + dy * wa; py[3] = ay - dx * wa;
    mq_fill(px, py, 4u, c);
}
static void mq_s_disc(mq_v3 a, float r, uint16_t c)       /* a ball: an octagon */
{
    float x, y, z, px[8], py[8], rr;
    uint32_t i;
    if (!mq_proj(a, &x, &y, &z))
        return;
    rr = mq_mx(0.8f, r * mq_cam_F / z);
    for (i = 0; i < 8u; i++) {
        px[i] = x + rr * brk_sin_turns((float)i * 0.125f + 0.25f);
        py[i] = y + rr * brk_sin_turns((float)i * 0.125f);
    }
    mq_fill(px, py, 8u, c);
}

/* ------------------------------------------------------------------------------------------------- the fly --- */
typedef struct { mq_v3 p; float t; } mq_foot_t;
typedef struct { mq_v3 pos; float yaw, roll, body_y, pitch, broll, flap, sweep; mq_foot_t feet[6]; } mq_fly_t;
static mq_fly_t mq_fly;
static float MQ_E[11];                      /* the black keys' effects, eased (bit PAD_*) */
static float mq_rnds[12];
static uint32_t mq_foot_n;
typedef struct { mq_v3 p; float yaw, roll, body_y; } mq_trail_t;
static uint32_t mq_hist_n;
static const float MQ_REST[6][2] = {{.2f, .3f}, {.3f, -.16f}, {.15f, -.3f}, {-.2f, .3f}, {-.3f, -.16f}, {-.15f, -.3f}};
static const float MQ_HIP[6][2] = {{.03f, .02f}, {.035f, 0.0f}, {.03f, -.02f}, {-.03f, .02f}, {-.035f, 0.0f}, {-.03f, -.02f}};
#define MQ_S_FLY 1.5f

static void mq_fly_pose(float t, float dt, int32_t gate_ph)
{
    float roll = mq_mx(MQ_E[PAD_ROLL8], mq_mx(MQ_E[PAD_ROLL16], MQ_E[PAD_ROLL32])), spread = 1.0f + .5f * MQ_E[PAD_TAPESTOP] + .2f * demo.squat,
          buzz;
    uint32_t j;
    mq_fly.pos = mq_v(0.0f, 0.0f, 0.0f);
    mq_fly.yaw = .25f * mq_sn(t * .4f);
    mq_fly.roll = 3.14159f * MQ_E[PAD_REVERSE];
    mq_fly.body_y = (.2f - .09f * demo.squat - .17f * MQ_E[PAD_TAPESTOP]) + .015f * mq_sn(demo.beat * 6.2832f) +
                 (MQ_E[PAD_REVERSE] > .5f ? -.25f : 0.0f);
    mq_fly.pitch = -.05f + .1f * demo.kick;
    mq_fly.broll = .04f * mq_sn(t * 55.0f) * roll;
    buzz = mq_mx(roll, .6f * demo.kick);
    mq_fly.flap = buzz > .05f ? mq_sn(t * (roll > .1f ? 140.0f : 90.0f)) * .9f * buzz : 0.0f;
    mq_fly.sweep = .12f + .25f * buzz;
    for (j = 0; j < 6u; j++) {
        mq_foot_t *fo = &mq_fly.feet[j];
        float lift;
        fo->t += dt * 6.0f;
        lift = fo->t < 1.0f ? mq_sn(3.14159f * fo->t) : 0.0f;
        if (MQ_E[PAD_GATE] > .1f)
            lift = mq_mx(lift, MQ_E[PAD_GATE] * (float)(((uint32_t)gate_ph ^ (j & 1u)) & 1u));
        fo->p = mq_v(MQ_REST[j][0] * spread, .06f * lift + MQ_E[PAD_RANDOM] * mq_rnds[j] * .08f,
                  MQ_REST[j][1] * spread + .02f * lift);
        fo->p.x += (mq_rnds[j + 6u] - .5f) * .12f * MQ_E[PAD_RANDOM];
        if (MQ_E[PAD_REVERSE] > .5f)
            fo->p = mq_v(MQ_REST[j][0] * .7f, mq_fly.body_y + .25f + .08f * mq_sn(t * 9.0f + (float)j), MQ_REST[j][1] * .7f);
    }
}

/* the fly's shapes into the strip; wire: only its legs and a thin body (the ghosts) */
static void mq_fly_draw(const mq_fly_t *F, uint16_t col, uint16_t wingc, int wire)
{
    mq_rot_t R = mq_rot(F->yaw, 0.0f, F->roll), B = mq_rot(0.0f, F->pitch, F->broll);
    float lw = .0085f * MQ_S_FLY;
    uint32_t j;
    int s;
#define MQ_W(p) mq_vadd(F->pos, mq_vmul(mq_turn(&R, (p)), MQ_S_FLY))
#define MQ_BD(p) MQ_W(mq_vadd(mq_v(0.0f, F->body_y, 0.0f), mq_turn(&B, (p))))
    if (!wire)                           /* the wings: long blades out to the sides, swept back, flapping */
        for (s = -1; s <= 1; s += 2) {
            static const float WP[6][2] = {{0.0f, -.008f}, {.12f, -.016f}, {.3f, -.03f}, {.4f, -.012f}, {.36f, .002f},
                                           {.15f, .008f}};
            mq_rot_t Wr = mq_rot((float)s * F->sweep, 0.0f, (float)s * (.08f + F->flap)), Tl = mq_rot(0.0f, .5f, 0.0f);
            mq_v3 pts[6], root = mq_v((float)s * .02f, .022f, .005f);
            uint32_t i;
            for (i = 0; i < 6u; i++) {
                mq_v3 w = mq_turn(&Tl, mq_v((float)s * WP[i][0], 0.0f, WP[i][1]));
                pts[i] = MQ_BD(mq_vadd(root, mq_turn(&Wr, w)));
            }
            mq_s_poly(pts, 6u, wingc);
        }
    for (j = 0; j < 6u; j++) {           /* the legs: hip, a sharp knee high above, a long shin down, the foot hooked out */
        const mq_foot_t *fo = &F->feet[j];
        float sd = j < 3u ? 1.0f : -1.0f;
        mq_v3 hip = mq_v(MQ_HIP[j][0], F->body_y, MQ_HIP[j][1]), knee = mq_vlerp(hip, fo->p, .3f), ank = fo->p;
        knee.y = mq_mx(hip.y, fo->p.y) + .09f * (1.0f - .6f * MQ_E[PAD_TAPESTOP]) * (MQ_E[PAD_REVERSE] > .5f ? -1.0f : 1.0f);
        knee.x += sd * .02f;
        ank.x -= sd * .06f;
        ank.y += .012f;
        mq_s_seg2(MQ_W(hip), MQ_W(knee), lw, lw, col);
        mq_s_seg2(MQ_W(knee), MQ_W(ank), lw, lw, col);
        mq_s_seg2(MQ_W(ank), MQ_W(fo->p), lw, lw, col);
    }
    {                                    /* the body: the long abdomen, the thorax, the head, the proboscis */
        static const float AR[5] = {.006f, .03f, .026f, .014f, .002f};
        mq_v3 a = MQ_BD(mq_v(0.0f, -.005f, -.03f)), b = MQ_BD(mq_v(0.0f, -.05f, -.44f));
        float k = wire ? .4f : 1.0f;
        for (j = 0; j < 4u; j++)
            mq_s_seg2(mq_vlerp(a, b, (float)j * .25f), mq_vlerp(a, b, (float)(j + 1u) * .25f), 2.0f * AR[j] * MQ_S_FLY * k,
                   2.0f * AR[j + 1u] * MQ_S_FLY * k, col);
        mq_s_disc(MQ_BD(mq_v(0.0f, 0.0f, 0.0f)), .03f * MQ_S_FLY, col);
        mq_s_disc(MQ_BD(mq_v(0.0f, .01f, .055f)), .017f * MQ_S_FLY, col);
        mq_s_seg2(MQ_BD(mq_v(0.0f, .015f, .07f)), MQ_BD(mq_v(0.0f, .07f, .38f)), lw * .9f, lw * .9f, col);
    }
#undef MQ_W
#undef MQ_BD
}

/* ------------------------------------------------------------------------------------------------- the floor --- */
#define MQ_NC 7
#define MQ_CS .17f
/* the demo's own memory in the pool: the screen reader's text and the break manager's SysEx use it while the demo is
 * off (main.c: sp_buf, app_sysex), the fly's trail and the floor while it is on (RAM is short) */
static union {
    struct { float sx[48], sy[48], sz[48]; } stars;
    struct { mq_trail_t hist[16]; float fl_h[MQ_NC * MQ_NC]; } s;   /* (580 B: the stars' 576, give or take; the pool's
                                                                     * headroom wants 8 KB) */
} demo_sky PLAT_POOL;
#define mq_hist (demo_sky.s.hist)
#define mq_fl_h (demo_sky.s.fl_h)
static float mq_fl_v[MQ_NC * MQ_NC];     /* (the floor's speeds: RAM) */

static void mq_floor_tick(float dt, float t, int32_t gate_ph)
{
    uint32_t i;
    float ring = t * 6.0f - (float)(int32_t)(t * 6.0f / 5.0f) * 5.0f;
    for (i = 0; i < MQ_NC * MQ_NC; i++) {
        float dx = (float)(i % MQ_NC) - 3.0f, dz = (float)(i / MQ_NC) - 3.0f, d = mq_sq(dx * dx + dz * dz), tg, k;
        k = 1.0f - (d - ring < 0.0f ? ring - d : d - ring) * .7f;
        tg = .05f * demo.kick * mq_mx(0.0f, k) - MQ_E[PAD_TAPESTOP] * .04f * (float)(d < 2.0f) +
             MQ_E[PAD_RANDOM] * .07f * mq_rnds[i % 12u] + MQ_E[PAD_GATE] * .05f * (float)(((i % MQ_NC + i / MQ_NC) ^ (uint32_t)gate_ph) & 1u) +
             mq_mx(MQ_E[PAD_ROLL8], mq_mx(MQ_E[PAD_ROLL16], MQ_E[PAD_ROLL32])) * .03f * mq_sn(t * 40.0f + d);
        mq_fl_v[i] += ((tg - mq_fl_h[i]) * 90.0f - mq_fl_v[i] * 12.0f) * dt;
        mq_fl_h[i] += mq_fl_v[i] * dt;
    }
}
static uint16_t mq_shade(uint32_t r, uint32_t g, uint32_t b, float k)
{
    k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    return RGB((uint32_t)((float)r * k), (uint32_t)((float)g * k), (uint32_t)((float)b * k));
}
static void mq_floor_draw(void)
{
    uint8_t order[MQ_NC * MQ_NC];
    float d[MQ_NC * MQ_NC];
    uint32_t n, j;
    float g = MQ_CS * .44f;
    int ice = MQ_E[PAD_FREEZE] > .5f;
    for (n = 0; n < MQ_NC * MQ_NC; n++) {   /* far to near from the camera */
        mq_v3 c = mq_vsub(mq_v(((float)(n % MQ_NC) - 3.0f) * MQ_CS, 0.0f, ((float)(n / MQ_NC) - 3.0f) * MQ_CS), mq_cam_e);
        d[n] = mq_vdot(c, c);
        order[n] = (uint8_t)n;
        for (j = n; j > 0u && d[order[j - 1u]] < d[order[j]]; j--) {
            uint8_t tmp = order[j];
            order[j] = order[j - 1u];
            order[j - 1u] = tmp;
        }
    }
    for (n = 0; n < MQ_NC * MQ_NC; n++) {
        uint32_t i = order[n];
        float x = ((float)(i % MQ_NC) - 3.0f) * MQ_CS, z = ((float)(i / MQ_NC) - 3.0f) * MQ_CS, h = mq_fl_h[i] + .015f;
        float k = mq_mn(1.0f, mq_mx(.08f, 1.1f - mq_sq(x * x + z * z) * 1.4f)) * (.35f + 5.0f * mq_mx(0.0f, mq_fl_h[i]));
        uint16_t top = ice ? mq_shade(120, 190, 230, k) : mq_shade(245, 184, 46, k * .55f),
                 side = ice ? mq_shade(60, 100, 130, k) : mq_shade(120, 80, 20, k * .5f);
        float fz = mq_cam_e.z > z ? z + g : z - g, fx = mq_cam_e.x > x ? x + g : x - g;
        mq_v3 q[4];
        q[0] = mq_v(x - g, h, fz); q[1] = mq_v(x + g, h, fz); q[2] = mq_v(x + g, -.2f, fz); q[3] = mq_v(x - g, -.2f, fz);
        mq_s_poly(q, 4u, side);
        q[0] = mq_v(fx, h, z - g); q[1] = mq_v(fx, h, z + g); q[2] = mq_v(fx, -.2f, z + g); q[3] = mq_v(fx, -.2f, z - g);
        mq_s_poly(q, 4u, side);
        q[0] = mq_v(x - g, h, z - g); q[1] = mq_v(x + g, h, z - g); q[2] = mq_v(x + g, h, z + g); q[3] = mq_v(x - g, h, z + g);
        mq_s_poly(q, 4u, top);
    }
}

/* ------------------------------------------------------------------------------------------------- the demo --- */
static void demo_start(uint32_t now)
{
    uint32_t i;
    for (i = 0; i < sizeof demo; i++)
        ((uint8_t *)&demo)[i] = 0u;
    demo.on = 1u;
    demo.t0 = now;
    demo.rng = now | 1u;
    demo.step = demo.slice = -1;
    demo.shot_bar = 0xFFFFFFFFu;
    mq_hist_n = 0u;
    for (i = 0; i < MQ_NC * MQ_NC; i++)
        mq_fl_h[i] = mq_fl_v[i] = 0.0f;
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

/* SCÈNE: MOUSTIQUE on the whole screen while everything still plays (HOME held: main.c); the demo's drawing, without
 * the demo (no key ends it) */
static uint8_t mq_scene;
static void scene_toggle(uint32_t now);
static uint8_t mq_switched;
static void scene_next(uint32_t now)     /* HOME held: SCÈNE with the character picked; again, the other one; again,
                                          * back, that one kept (the saver and the pads' pictures show it) */
{
    if (mq_scene && !mq_switched) {
        mq_kind ^= 1u;
        mq_switched = 1u;
        return;
    }
    mq_switched = 0u;
    scene_toggle(now);
}
static void scene_toggle(uint32_t now)
{
    if (mq_scene) {
        mq_scene = 0u;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.redraw = RG_ALL;
        return;
    }
    demo_start(now);
    demo.on = 0u;
    mq_scene = 1u;
}

static void demo_end(void)
{
    demo.on = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}


/* ------------------------------------------------------------------------------------------------- PANTIN --- */
/* the puppet: flat yellow, floppy (each joint a spring going after its target), its head a screen showing the break's
 * waveform with two eyes; it dances on the beat, throws an arm on each slice, squats on the kick; FREEZE: hands up,
 * round eyes; TAPE STOP: it collapses; REVERSE: a handstand; GATE: it marches; RANDOM: it flails; the ROLLs: it shakes */
typedef struct { float v, s; } mq_spr_t; /* a spring: value, speed */
static struct {
    mq_spr_t a[2], fw[2], e[2], lean, hip, turn;   /* arms: raised (0 down .. 3.1 up), forward, the elbow; the body */
    uint32_t punch;                      /* the arm thrown last */
    float pt[2];                         /* each arm's throw, 1 .. 0 */
} pp;
static void mq_spring(mq_spr_t *p, float tg, float dt, float k)
{
    p->s += ((tg - p->v) * k - p->s * 2.0f * mq_sq(k) * .45f) * dt;
    p->v += p->s * dt;
}
static void pp_tick(float t, float dt, int32_t gate_ph, int hit)
{
    float roll = mq_mx(MQ_E[PAD_ROLL8], mq_mx(MQ_E[PAD_ROLL16], MQ_E[PAD_ROLL32])), sw = mq_sn(demo.beat * 3.14159f);
    uint32_t i;
    if (hit) {                           /* a slice: the other arm thrown */
        pp.punch ^= 1u;
        pp.pt[pp.punch] = 1.0f;
    }
    for (i = 0; i < 2u; i++) {
        float side = i ? 1.0f : -1.0f, a = .35f + .35f * sw * side + 1.6f * pp.pt[i], fw = .2f + .9f * pp.pt[i], e = .9f - .7f * pp.pt[i];
        pp.pt[i] = mq_mx(0.0f, pp.pt[i] - dt * 5.0f);
        if (MQ_E[PAD_FREEZE] > .5f || MQ_E[PAD_REVERSE] > .5f)
            a = 2.9f, fw = 0.0f, e = .1f;   /* hands up (and on its hands, upside down) */
        if (MQ_E[PAD_TAPESTOP] > .5f)
            a = .05f, fw = .3f, e = .1f;    /* arms hanging */
        if (MQ_E[PAD_RANDOM] > .3f)
            a = mq_rnds[i * 3u] * 3.0f, fw = mq_rnds[i * 3u + 1u] * 1.5f - .4f, e = mq_rnds[i * 3u + 2u] * 2.0f;
        a += roll * .25f * mq_sn(t * 70.0f + (float)i);
        mq_spring(&pp.a[i], a, dt, 140.0f);
        mq_spring(&pp.fw[i], fw, dt, 140.0f);
        mq_spring(&pp.e[i], e, dt, 160.0f);
    }
    mq_spring(&pp.hip, .26f - .06f * demo.squat - .17f * MQ_E[PAD_TAPESTOP] + .015f * mq_sn(demo.beat * 6.2832f), dt, 220.0f);
    mq_spring(&pp.lean, .12f * sw + .5f * MQ_E[PAD_TAPESTOP] + roll * .08f * mq_sn(t * 60.0f) +
                        (MQ_E[PAD_RANDOM] > .3f ? mq_rnds[7] - .5f : 0.0f), dt, 120.0f);
    mq_spring(&pp.turn, .35f * mq_sn(t * .5f) + (float)gate_ph * .2f * MQ_E[PAD_GATE], dt, 60.0f);
}
/* the puppet's shapes into the strip, at pos; one: a single colour (a ghost, an echo) */
static void pp_draw(mq_v3 pos, uint16_t one, int ghost)
{
    mq_rot_t R = mq_rot(pp.turn.v, 0.0f, MQ_E[PAD_REVERSE] * 3.14159f), L = mq_rot(0.0f, 0.0f, pp.lean.v);
    uint16_t body = ghost ? one : mq_col, dark = ghost ? one : RGB(18, 18, 22), ink = ghost ? one : RGB(245, 184, 46);
    float rev = MQ_E[PAD_REVERSE] > .5f ? .62f : 0.0f, w = .036f;
    mq_v3 hip = mq_v(0.0f, pp.hip.v, 0.0f), neck = mq_vadd(hip, mq_turn(&L, mq_v(0.0f, .17f, 0.0f)));
    uint32_t i;
    int32_t k;
#define PW(p) mq_vadd(mq_vadd(pos, mq_v(0.0f, rev, 0.0f)), mq_turn(&R, (p)))   /* (REVERSE: turned, then lifted) */
    if (MQ_E[PAD_FILTER] <= .3f && !ghost && MQ_E[PAD_FREEZE] <= .5f)
        body = ink;
    for (i = 0; i < 2u; i++) {           /* the legs: hip, knee forward, foot on the floor (GATE: marching) */
        float side = i ? 1.0f : -1.0f, lift = MQ_E[PAD_GATE] * (float)((uint32_t)(demo.beat * 2.0f + (float)i) & 1u) * .08f;
        mq_v3 h = mq_vadd(hip, mq_v(side * .045f, 0.0f, 0.0f)), f = mq_v(side * (.07f + .05f * MQ_E[PAD_TAPESTOP]), lift, .02f), m, kn;
        float dx = f.x - h.x, dy = f.y - h.y, d = mq_sq(dx * dx + dy * dy), up;
        m = mq_vlerp(h, f, .5f);
        up = d < .24f ? mq_sq(.0144f - d * d * .25f) : 0.0f;
        kn = mq_vadd(m, mq_v(0.0f, 0.0f, up * 1.6f));
        mq_s_seg2(PW(h), PW(kn), w, w, body);
        mq_s_seg2(PW(kn), PW(f), w, w * .9f, body);
        mq_s_seg2(PW(f), PW(mq_vadd(f, mq_v(0.0f, 0.0f, .05f))), w * 1.2f, w * 1.2f, dark);
    }
    mq_s_seg2(PW(hip), PW(neck), .09f, .1f, body);   /* the body */
    for (i = 0; i < 2u; i++) {           /* the arms: shoulder, elbow, the hand a white ball */
        float side = i ? 1.0f : -1.0f, a = pp.a[i].v, fw = pp.fw[i].v, e = pp.e[i].v;
        mq_v3 sh = mq_vadd(neck, mq_v(side * .06f, -.015f, 0.0f));
        mq_v3 u = mq_vnorm(mq_v(side * mq_sn(a), -mq_cs(a), fw)), el = mq_vadd(sh, mq_vmul(u, .1f));
        mq_v3 u2 = mq_vnorm(mq_vadd(u, mq_v(0.0f, e * .8f, e * .5f))), hd = mq_vadd(el, mq_vmul(u2, .1f));
        mq_s_seg2(PW(sh), PW(el), w, w, body);
        mq_s_seg2(PW(el), PW(hd), w, w * .9f, body);
        mq_s_disc(PW(hd), .025f, ghost ? one : RGB(244, 244, 240));
    }
    {                                    /* the head: a screen, the break's waveform on it, two eyes */
        mq_v3 c = mq_vadd(neck, mq_turn(&L, mq_v(0.0f, .075f, 0.0f))), q[4];
        float hw = .075f, hh = .055f;
        q[0] = PW(mq_vadd(c, mq_v(-hw - .012f, -hh - .012f, .01f))); q[1] = PW(mq_vadd(c, mq_v(hw + .012f, -hh - .012f, .01f)));
        q[2] = PW(mq_vadd(c, mq_v(hw + .012f, hh + .012f, .01f)));   q[3] = PW(mq_vadd(c, mq_v(-hw - .012f, hh + .012f, .01f)));
        mq_s_poly(q, 4u, body);
        q[0] = PW(mq_vadd(c, mq_v(-hw, -hh, .02f))); q[1] = PW(mq_vadd(c, mq_v(hw, -hh, .02f)));
        q[2] = PW(mq_vadd(c, mq_v(hw, hh, .02f)));   q[3] = PW(mq_vadd(c, mq_v(-hw, hh, .02f)));
        mq_s_poly(q, 4u, dark);
        if (!ghost) {
            float big = MQ_E[PAD_FREEZE] > .5f ? 1.7f : 1.0f, sq2 = MQ_E[PAD_TAPESTOP] > .5f ? .3f : 1.0f;
            for (k = 0; k < 12; k++) {   /* the waveform, across the screen's lower half */
                float x = -hw + .008f + (float)k * (2.0f * hw - .016f) / 11.0f, hgt = (float)wave_px[(uint32_t)k * 20u] * .0011f + .003f;
                mq_s_seg2(PW(mq_vadd(c, mq_v(x, -.025f - hgt, .025f))), PW(mq_vadd(c, mq_v(x, -.025f + hgt, .025f))), .006f,
                          .006f, ink);
            }
            for (k = -1; k <= 1; k += 2) {   /* the eyes */
                mq_v3 ey = mq_vadd(c, mq_v((float)k * .03f, .018f, .026f));
                mq_s_disc(PW(ey), .014f * big * sq2 + .002f, RGB(244, 244, 240));
                mq_s_disc(PW(mq_vadd(ey, mq_v(.004f * mq_sn(demo.beat * 3.14159f), 0.0f, .002f))), .006f * big * sq2 + .001f, dark);
            }
        }
    }
#undef PW
}

static float mq_gate = 1.0f, mq_tape = 1.0f, mq_flash, mq_t; /* the engine's gate level and tape speed; a roll's hit */
static uint32_t mq_rolln, mq_seed;       /* the roll's repeats seen; the glitch's dice (a new one each step) */

/* the fly's step: the engine read, the effects eased, the pose; full: the demo or SCÈNE (the floor, the trail, the
 * camera's cuts), else a pad's picture (the fly alone, a slow turn) */
static void mq_tick(uint32_t now, int full)
{
    static const float SH[6][4] = {{0.0f, .45f, 1.9f, 1.24f}, {.7f, .3f, 1.6f, 1.12f}, {-.6f, .7f, 1.7f, 1.18f},
                                   {2.6f, .4f, 1.8f, 1.18f}, {0.0f, 2.0f, .35f, 1.07f}, {-1.4f, .18f, 1.35f, 1.07f}};
                                   /* (each angle: round, up, back, and the focal length over 240) */
    brk_status_t st;
    float t, dt, m, a;
    uint32_t k;
    int32_t gate_ph;
    int frozen, hit;
    brk_status(&st);
    t = (float)((now - demo.t0) % 1000000u) * .001f;
    dt = demo.last > 0.0f && t > demo.last ? mq_mn(.08f, t - demo.last) : .04f;
    demo.last = t;
    demo.beat = st.playing && st.bar16 >= 0 ? (float)(st.bar % 64u) * 4.0f + ((float)st.bar16 + st.phase) * .25f
                                           : t * brk_get_tempo() * (1.0f / 60.0f);
    gate_ph = (int32_t)(demo.beat * 2.0f) & 1;
    for (k = 0; k < 11u; k++) {          /* the effects held, eased in and out */
        float tg = (st.pads >> k) & 1u ? 1.0f : 0.0f;
        MQ_E[k] += (tg - MQ_E[k]) * mq_mn(1.0f, dt * (tg > MQ_E[k] ? 14.0f : 6.0f));
    }
    dt *= 1.0f - .6f * MQ_E[PAD_HALF];   /* HALF SPEED: it moves in slow motion */
    frozen = MQ_E[PAD_FREEZE] > .5f;
    m = meter_brk;                       /* the kick: the break's level jumping (and the kick layer's) */
    if (m - demo.mprev > .12f || meter_kick > .3f)
        demo.kick = 1.0f, demo.squat = frozen ? demo.squat : 1.0f;
    demo.mprev = m;
    demo.kick *= .82f;
    hit = st.playing && st.slice >= 0 && (st.slice != demo.slice || st.step != demo.step) && !frozen;
    if (hit) {
        mq_fly.feet[mq_foot_n % 6u].t = 0.0f;  /* a slice: a foot steps */
        mq_foot_n++;
    }
    if (st.step != demo.step) {          /* a new step: fresh dice */
        demo.step = st.step;
        for (k = 0; k < 12u; k++)
            mq_rnds[k] = brk_randf(&demo.rng);
    }
    demo.slice = st.playing ? st.slice : -1;
    mq_t = t;
    mq_gate = st.gate;
    mq_tape = st.tape;
    if (st.roll_n != mq_rolln) {         /* a roll's repeat: a flash */
        mq_rolln = st.roll_n;
        mq_flash = st.roll_n ? 1.0f : 0.0f;
    }
    mq_flash *= .55f;
    mq_seed = (uint32_t)st.step * 0x9E3779B9u ^ (uint32_t)(st.slice + 7) * 0x85EBCA6Bu;
    if (!frozen) {
        demo.squat = mq_mx(0.0f, demo.squat - dt * 5.0f);
        mq_fly_pose(t, dt, gate_ph);
        if (full) {
            mq_floor_tick(dt, t, gate_ph);
            mq_hist[mq_hist_n % 16u].p = mq_fly.pos;
            mq_hist[mq_hist_n % 16u].yaw = mq_fly.yaw;
            mq_hist[mq_hist_n % 16u].roll = mq_fly.roll;
            mq_hist[mq_hist_n % 16u].body_y = mq_fly.body_y;
            mq_hist_n++;
        }
    }
    if (mq_kind)                         /* (PANTIN even frozen: FREEZE puts its hands up) */
        pp_tick(t, frozen ? dt * .5f : dt, gate_ph, hit);
    /* the camera: a slow turn; full, another angle every two bars */
    if (full && st.playing && st.bar / 2u != demo.shot_bar) {
        demo.shot_bar = st.bar / 2u;
        demo.shot = (demo.shot + 1u + (brk_rand(&demo.rng) & 1u)) % 6u;
    }
    if (!full || !st.playing)
        demo.shot = 0u;
    {
        const float *s = SH[demo.shot];
        a = s[0] + (full ? .18f * mq_sn(t * .21f) : .5f * mq_sn(t * .35f));
        mq_cam_e = mq_v(mq_sn(a) * s[2], s[1] - (full ? 0.0f : .2f), mq_cs(a) * s[2]);
        mq_cam_f = mq_vnorm(mq_vsub(mq_v(0.0f, mq_kind ? .3f : .16f, -.04f), mq_cam_e));
        mq_cam_r = mq_vnorm(mq_v(-mq_cam_f.z, 0.0f, mq_cam_f.x));
        mq_cam_u = mq_v(mq_cam_r.y * mq_cam_f.z - mq_cam_r.z * mq_cam_f.y, mq_cam_r.z * mq_cam_f.x - mq_cam_r.x * mq_cam_f.z,
                  mq_cam_r.x * mq_cam_f.y - mq_cam_r.y * mq_cam_f.x);
        mq_cam_F = 240.0f * s[3] * (1.0f + .1f * demo.kick);
        if (MQ_E[PAD_REVERSE] > .5f) {   /* REVERSE: the world upside down (the fly on its back: right way up) */
            mq_cam_u = mq_vmul(mq_cam_u, -1.0f);
            mq_cam_r = mq_vmul(mq_cam_r, -1.0f);
        }
    }
    /* white like the logo; FREEZE ice blue; FILTER the hues going round */
    if (MQ_E[PAD_FILTER] > .3f) {
        float h = t * .5f;
        mq_col = RGB((uint32_t)(140.0f + 110.0f * brk_sin_turns(h)), (uint32_t)(140.0f + 110.0f * brk_sin_turns(h + .33f)),
                     (uint32_t)(140.0f + 110.0f * brk_sin_turns(h + .67f)));
    } else
        mq_col = frozen ? RGB(190, 230, 255) : RGB(244, 244, 240);
    mq_wingc = frozen ? RGB(160, 196, 216) : MQ_E[PAD_FILTER] > .3f ? mq_col : RGB(208, 208, 204);
}

/* the strip mq_sy0 .. +mq_bh: the floor (full), DUB's ghosts (full: its trail; else beside it), the fly */
static void mq_band(int full)
{
    uint32_t i;
    if (full)
        mq_floor_draw();
    if (mq_kind) {                       /* PANTIN: its echoes (ROLL), its ghosts (DUB), itself */
        float r = mq_mx(MQ_E[PAD_ROLL8], mq_mx(MQ_E[PAD_ROLL16], MQ_E[PAD_ROLL32]));
        for (i = 3u; MQ_E[PAD_DUB] > .05f && i-- > 0u;)
            pp_draw(mq_v((float)(i + 1u) * .1f, 0.0f, -(float)(i + 1u) * .22f), mq_shade(120, 230, 160, (.6f - (float)i * .15f) * mq_mn(1.0f, MQ_E[PAD_DUB] * 2.0f)), 1);
        if (r > .2f) {
            pp_draw(mq_v(-.18f * r, 0.0f, -.05f), mq_shade(200, 200, 196, .5f), 1);
            pp_draw(mq_v(.18f * r, 0.0f, -.05f), mq_shade(200, 200, 196, .5f), 1);
        }
        pp_draw(mq_v(0.0f, 0.0f, 0.0f), 0u, 0);
    } else {
    if (MQ_E[PAD_DUB] > .05f && (!full || mq_hist_n > 16u))   /* DUB: three ghosts trailing behind, green wire */
        for (i = 3u; i-- > 0u;) {
            mq_fly_t mq_ghost = mq_fly;
            mq_v3 off = mq_v((float)(i + 1u) * .07f, 0.0f, -(float)(i + 1u) * .2f);
            if (full) {
                uint32_t h = (mq_hist_n - 1u - (i + 1u) * 5u) % 16u;
                mq_ghost.pos = mq_hist[h].p;
                mq_ghost.yaw = mq_hist[h].yaw;
                mq_ghost.roll = mq_hist[h].roll;
                mq_ghost.body_y = mq_hist[h].body_y;
            }
            mq_ghost.pos = mq_vadd(mq_ghost.pos, off);
            mq_fly_draw(&mq_ghost, mq_shade(150, 230, 170, (.6f - (float)i * .15f) * mq_mn(1.0f, MQ_E[PAD_DUB] * 2.0f)), 0u, 1);
        }
    {                                    /* the ROLLs: stuttered copies either side, more the faster the roll */
        float r = mq_mx(MQ_E[PAD_ROLL8], mq_mx(MQ_E[PAD_ROLL16], MQ_E[PAD_ROLL32]));
        uint32_t n = MQ_E[PAD_ROLL32] > .5f ? 3u : MQ_E[PAD_ROLL16] > .5f ? 2u : 1u;
        if (r > .2f)
            for (i = n; i > 0u; i--) {
                mq_fly_t e = mq_fly;
                float k = (float)i * .16f * r;
                e.pos = mq_vadd(mq_fly.pos, mq_v(-k, 0.0f, 0.0f));
                mq_fly_draw(&e, mq_shade(200, 200, 196, .7f - (float)i * .18f), 0u, 1);
                e.pos = mq_vadd(mq_fly.pos, mq_v(k, 0.0f, 0.0f));
                mq_fly_draw(&e, mq_shade(200, 200, 196, .7f - (float)i * .18f), 0u, 1);
            }
    }
    mq_fly_draw(&mq_fly, mq_col, mq_wingc, 0);
    }
    if (MQ_E[PAD_DUB] > .05f) {          /* DUB: rings going out from it on the beat, green */
        float cx, cy, cz, ph = demo.beat - (float)(int32_t)demo.beat;
        if (mq_proj(mq_vadd(mq_fly.pos, mq_v(0.0f, mq_fly.body_y * MQ_S_FLY, 0.0f)), &cx, &cy, &cz))
            for (i = 0; i < 2u; i++) {
                float rr = (ph + (float)i * .5f - (float)(int32_t)(ph + (float)i * .5f)) * 120.0f + 8.0f;
                uint16_t c = swap16(mq_shade(120, 230, 160, MQ_E[PAD_DUB] * (1.0f - rr / 128.0f)));
                uint32_t a;
                for (a = 0; a < 48u; a++) {
                    int32_t x = (int32_t)(cx + rr * brk_sin_turns((float)a / 48.0f + .25f)),
                            y = (int32_t)(cy + rr * .45f * brk_sin_turns((float)a / 48.0f));
                    if (x >= 0 && x < 239 && y >= mq_sy0 && y < mq_sy0 + mq_bh)
                        cv_px[(uint32_t)(y - mq_sy0) * 240u + (uint32_t)x] = cv_px[(uint32_t)(y - mq_sy0) * 240u + (uint32_t)x + 1u] = c;
                }
            }
    }
    if (MQ_E[PAD_FREEZE] > .5f)          /* FREEZE: frost, the same sparkles all the while */
        for (i = 0; i < 70u; i++) {
            uint32_t h = (i + 1u) * 0x9E3779B1u, x = (h >> 8) % 240u, y = (h >> 20) % 240u;
            int32_t yy = mq_bh == 60 ? (int32_t)y : FX_Y + (int32_t)(y % 96u);
            if (yy >= mq_sy0 && yy < mq_sy0 + mq_bh)
                cv_px[(uint32_t)(yy - mq_sy0) * 240u + x] = swap16(h & 1u ? RGB(255, 255, 255) : RGB(150, 210, 255));
        }
}

/* the effects on the picture itself, row by row, once the strip is drawn: RANDOM glitches (bands thrown sideways),
 * TAPE STOP melts (the rows wobbling, the light going with the tape), GATE cuts the light with the gate, HALF SPEED and
 * FILTER draw scanlines, a ROLL's hit flashes the picture negative */
static void mq_rev(uint16_t *p, int32_t a, int32_t b)   /* p[a..b-1] back to front */
{
    for (b--; a < b; a++, b--) {
        uint16_t t = p[a];
        p[a] = p[b];
        p[b] = t;
    }
}
static void mq_post(void)
{
    int32_t r;
    float k = 1.0f - MQ_E[PAD_GATE] * (1.0f - mq_gate);
    k *= 1.0f - MQ_E[PAD_TAPESTOP] * .6f * (1.0f - mq_tape);
    for (r = 0; r < mq_bh; r++) {
        int32_t y = mq_sy0 + r, sh = 0, x;
        uint16_t *px = &cv_px[(uint32_t)r * 240u];
        float kr = k;
        if (MQ_E[PAD_RANDOM] > .2f) {
            uint32_t h = ((uint32_t)(y / 6) + 1u) * 0x27D4EB2Du ^ mq_seed;
            h ^= h >> 15;
            if ((h & 3u) == 0u)
                sh = (int32_t)((h >> 8) % 61u) - 30;
        }
        if (MQ_E[PAD_TAPESTOP] > .1f)
            sh += (int32_t)(MQ_E[PAD_TAPESTOP] * (1.0f - mq_tape) * 10.0f * brk_sin_turns((float)y * .02f + mq_t * .8f));
        if ((MQ_E[PAD_HALF] > .5f || MQ_E[PAD_FILTER] > .5f) && (y & 1))
            kr *= .45f;
        if (sh) {                        /* (the row turned round by sh, in place: three reversals) */
            sh = (sh + 240) % 240;
            mq_rev(px, 0, 240);
            mq_rev(px, 0, sh);
            mq_rev(px, sh, 240);
        }
        if (kr < .97f || mq_flash > .35f)
            for (x = 0; x < 240; x++) {
                uint32_t c = swap16(px[x]), rr = c >> 11, g = (c >> 5) & 63u, b = c & 31u;
                if (mq_flash > .35f) {
                    rr = 31u - rr;
                    g = 63u - g;
                    b = 31u - b;
                }
                rr = (uint32_t)((float)rr * kr);
                g = (uint32_t)((float)g * kr);
                b = (uint32_t)((float)b * kr);
                px[x] = swap16(rr << 11 | g << 5 | b);
            }
    }
}

static void demo_frame(uint32_t now)
{
    if (now - demo.drawn < DEMO_FRAME_MS)
        return;
    demo.drawn = now;
    mq_tick(now, 1);
    mq_cy = 120.0f;
    mq_bh = 60;
    for (mq_sy0 = 0; mq_sy0 < 240; mq_sy0 += 60) {   /* the whole screen, in four strips */
        cv_begin(240, 60, C_BLACK);
        mq_band(1);
        mq_post();
        cv_blit(0, (uint32_t)mq_sy0);
    }
}

/* a black key held: its picture is MOUSTIQUE doing the effect, in the strip where the slice blocks are (two bands of
 * FX_BAND rows), the pad's name over it (main.c, about 30 times a second) */
static void draw_fx(int pad, const brk_status_t *st, uint32_t now)
{
    const brk_padset_t *p = &set.pads;
    char t[24];
    int32_t top;
    (void)st;
    t[0] = 0;
    text_cat(t, PAD_TITLE[pad]);
    if (pad <= PAD_ROLL32) {
        text_cat(t, " ");
        text_cat(t, BRK_ROLL_DIV_NAME[p->roll_div[pad] % BRK_ROLL_DIVS]);
    }
    mq_tick(now, 0);
    mq_cam_F *= .56f;                    /* (smaller: 96 rows) */
    mq_cy = (float)(FX_Y + 54);
    mq_bh = FX_BAND;
    for (top = 0; top < 2 * FX_BAND; top += FX_BAND) {
        cv_begin(240, FX_BAND, C_BLACK);
        mq_sy0 = FX_Y + top;
        mq_band(0);
        mq_post();
        cv_oy = -top;
        cv_text(6, 0, &FONT_B, t, C_HI);
        cv_oy = 0;
        cv_blit(0, FX_Y + (uint32_t)top);
    }
}

/* the demo's lights: the buttons' comet (the start screen's PATH), the white key of the step, the black keys held */
static void demo_leds(uint32_t now)
{
    uint32_t head = (now - demo.t0) / 70u % NB, b = 1u << PATH[head], k = 0u, i,
             mid = (1u << PATH[(head + NB - 1u) % NB] | 1u << PATH[(head + NB - 2u) % NB]) & ~(1u << B_PLAY);
    if (b & 1u << B_PLAY)
        b = LED_PLAY_GREEN;              /* (PLAY: its green LED) */
    if (demo.step >= 0)
        k |= 1u << WK[(uint32_t)demo.step % 16u];
    for (i = 0; i < 11u; i++)
        if (MQ_E[i] > .5f)
            k |= 1u << BK[i];
    plat_lights(b, k, (((1u << NB) - 1u) & ~(1u << B_PLAY)) | LED_PLAY_GREEN, 0u, 1);
    plat_lights_mid(mid, 0u);
}
