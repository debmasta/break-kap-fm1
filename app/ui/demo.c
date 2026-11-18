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
static int32_t mq_sy0;                      /* the strip being filled: rows sy0 .. sy0+59 */

static int mq_proj(mq_v3 p, float *x, float *y, float *z)
{
    mq_v3 d = mq_vsub(p, mq_cam_e);
    float zz = mq_vdot(d, mq_cam_f);
    if (zz < 0.05f)
        return 0;
    *x = 120.0f + mq_vdot(d, mq_cam_r) * mq_cam_F / zz;
    *y = 120.0f - mq_vdot(d, mq_cam_u) * mq_cam_F / zz;
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
    if (hi_y < (float)mq_sy0 || lo_y >= (float)(mq_sy0 + 60))
        return;
    ya = lo_y < (float)mq_sy0 ? mq_sy0 : (int32_t)lo_y;
    yb = hi_y > (float)(mq_sy0 + 59) ? mq_sy0 + 59 : (int32_t)hi_y;
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

static void demo_end(void)
{
    demo.on = 0u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = RG_ALL;
}

static void demo_frame(uint32_t now)
{
    static const float SH[6][4] = {{0.0f, .45f, 1.9f, 1.24f}, {.7f, .3f, 1.6f, 1.12f}, {-.6f, .7f, 1.7f, 1.18f},
                                   {2.6f, .4f, 1.8f, 1.18f}, {0.0f, 2.0f, .35f, 1.07f}, {-1.4f, .18f, 1.35f, 1.07f}};
                                   /* (each angle: round, up, back, and the focal length over 240) */
    brk_status_t st;
    float t, dt, m, a;
    uint32_t i, k;
    int32_t gate_ph;
    uint16_t col, wingc;
    int frozen;
    if (now - demo.drawn < DEMO_FRAME_MS)
        return;
    demo.drawn = now;
    brk_status(&st);
    t = (float)(now - demo.t0) * .001f;
    dt = demo.last > 0.0f ? mq_mn(.08f, t - demo.last) : .04f;
    demo.last = t;
    demo.beat = st.playing && st.bar16 >= 0 ? (float)(st.bar % 64u) * 4.0f + ((float)st.bar16 + st.phase) * .25f
                                           : t * brk_get_tempo() * (1.0f / 60.0f);
    gate_ph = (int32_t)(demo.beat * 2.0f) & 1;
    for (k = 0; k < 11u; k++) {          /* the effects held, eased in and out */
        float tg = (st.pads >> k) & 1u ? 1.0f : 0.0f;
        MQ_E[k] += (tg - MQ_E[k]) * mq_mn(1.0f, dt * (tg > MQ_E[k] ? 14.0f : 6.0f));
    }
    frozen = MQ_E[PAD_FREEZE] > .5f;
    m = meter_brk;                       /* the kick: the break's level jumping (and the kick layer's) */
    if (m - demo.mprev > .12f || meter_kick > .3f)
        demo.kick = 1.0f, demo.squat = frozen ? demo.squat : 1.0f;
    demo.mprev = m;
    demo.kick *= .82f;
    if (st.step != demo.step) {          /* a new step: fresh dice */
        demo.step = st.step;
        for (k = 0; k < 12u; k++)
            mq_rnds[k] = brk_randf(&demo.rng);
    }
    if (st.playing && st.slice >= 0 && (st.slice != demo.slice || st.step != demo.step) && !frozen) {
        mq_fly.feet[mq_foot_n % 6u].t = 0.0f;  /* a slice: a foot steps */
        mq_foot_n++;
    }
    demo.slice = st.playing ? st.slice : -1;
    if (!frozen) {
        demo.squat = mq_mx(0.0f, demo.squat - dt * 5.0f);
        mq_fly_pose(t, dt, gate_ph);
        mq_floor_tick(dt, t, gate_ph);
        mq_hist[mq_hist_n % 16u].p = mq_fly.pos;
        mq_hist[mq_hist_n % 16u].yaw = mq_fly.yaw;
        mq_hist[mq_hist_n % 16u].roll = mq_fly.roll;
        mq_hist[mq_hist_n % 16u].body_y = mq_fly.body_y;
        mq_hist_n++;
    }
    /* the camera: a slow turn, another angle every two bars */
    if (st.playing && st.bar / 2u != demo.shot_bar) {
        demo.shot_bar = st.bar / 2u;
        demo.shot = (demo.shot + 1u + (brk_rand(&demo.rng) & 1u)) % 6u;
    }
    if (!st.playing)
        demo.shot = 0u;
    {
        const float *s = SH[demo.shot];
        a = s[0] + .18f * mq_sn(t * .21f);
        mq_cam_e = mq_v(mq_sn(a) * s[2], s[1], mq_cs(a) * s[2]);
        mq_cam_f = mq_vnorm(mq_vsub(mq_v(0.0f, .16f, -.04f), mq_cam_e));
        mq_cam_r = mq_vnorm(mq_v(-mq_cam_f.z, 0.0f, mq_cam_f.x));
        mq_cam_u = mq_v(mq_cam_r.y * mq_cam_f.z - mq_cam_r.z * mq_cam_f.y, mq_cam_r.z * mq_cam_f.x - mq_cam_r.x * mq_cam_f.z,
                  mq_cam_r.x * mq_cam_f.y - mq_cam_r.y * mq_cam_f.x);
        mq_cam_F = 240.0f * s[3] * (1.0f + .1f * demo.kick);
    }
    /* white like the logo; FREEZE ice blue; FILTER the hues going round */
    if (MQ_E[PAD_FILTER] > .3f) {
        float h = t * .5f;
        col = RGB((uint32_t)(140.0f + 110.0f * brk_sin_turns(h)), (uint32_t)(140.0f + 110.0f * brk_sin_turns(h + .33f)),
                  (uint32_t)(140.0f + 110.0f * brk_sin_turns(h + .67f)));
    } else
        col = frozen ? RGB(190, 230, 255) : RGB(244, 244, 240);
    wingc = frozen ? RGB(160, 196, 216) : MQ_E[PAD_FILTER] > .3f ? col : RGB(208, 208, 204);
    for (mq_sy0 = 0; mq_sy0 < 240; mq_sy0 += 60) {   /* the whole screen, in four strips */
        cv_begin(240, 60, C_BLACK);
        mq_floor_draw();
        if (MQ_E[PAD_DUB] > .05f && mq_hist_n > 16u)   /* DUB: three ghosts trailing behind, green wire */
            for (i = 3u; i-- > 0u;) {
                uint32_t h = (mq_hist_n - 1u - (i + 1u) * 5u) % 16u;
                mq_fly_t mq_ghost = mq_fly;
                mq_ghost.pos = mq_vadd(mq_hist[h].p, mq_v((float)(i + 1u) * .07f, 0.0f, -(float)(i + 1u) * .2f));
                mq_ghost.yaw = mq_hist[h].yaw;
                mq_ghost.roll = mq_hist[h].roll;
                mq_ghost.body_y = mq_hist[h].body_y;
                mq_fly_draw(&mq_ghost, mq_shade(150, 230, 170, (.6f - (float)i * .15f) * mq_mn(1.0f, MQ_E[PAD_DUB] * 2.0f)), 0u, 1);
            }
        mq_fly_draw(&mq_fly, col, wingc, 0);
        cv_blit(0, (uint32_t)mq_sy0);
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
