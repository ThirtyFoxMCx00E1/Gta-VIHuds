/* VIHud: the in-game HUD in the GTA VI look (drawn right after CHud::Draw, the stock player info / radar / wanted
 * widgets are skipped while Hud=1):
 *   - round radar bottom left with the pink rim: San Andreas roads, water and parks from sa_map_data.h (the
 *     vector map generated from the game's path nodes, same data as the MP radar), turning with the camera,
 *     the game's own radar blips (CRadar::ms_RadarTrace + CRadar::RadarBlipSprites) and the player arrow
 *   - heart (health), shield (armour) and lightning (stamina) rings next to the radar
 *   - money, wanted stars, weapon name + ammo top right (the stock weapon tap area stays where it was)
 *   - item wheel: hold the weapon area -> the weapon wheel (on foot) or the radio wheel (in a car)
 *   - ped actions: the context buttons of the current state, bottom right
 * Every value is read from the game each frame (script opcodes / CPed fields), nothing is made up. */
#include "scripting.h"
#include "sa_map_data.h"

DEFOPCODE(010B, STORE_SCORE, iv);
DEFOPCODE(01C0, STORE_WANTED_LEVEL, iv);
DEFOPCODE(041A, GET_AMMO_IN_CHAR_WEAPON, iiv);

static void* (*fnFindPlayerPed)(int) = NULL;
static void* (*fnPoolPed)(int) = NULL;
static void* (*fnPoolVeh)(int) = NULL;
static void* (*fnPoolObj)(int) = NULL;
static uint8_t* g_radarTrace = NULL;
static void** g_blipSprites = NULL;            // CSprite2d RadarBlipSprites[]: { RwTexture* }
static float* g_statFloat = NULL;
static uint8_t* g_clockH = NULL; static uint8_t* g_clockM = NULL; static uint8_t* g_clockDay = NULL;
static uint8_t* g_displayHud = NULL; static uint8_t* g_cutscene = NULL;
static void* g_theCamera = NULL;
static bool g_scrOK = false, g_hudHooks = false;

#define PED_HEALTH  BYBIT(1348, 1708)
#define PED_ARMOUR  BYBIT(1356, 1716)
#define PED_VEHICLE BYBIT(1424, 1800)
#define PED_WEAPONS BYBIT(1444, 1840)
#define PED_WSTRIDE BYBIT(28, 32)
#define PED_WSLOT   BYBIT(1820, 2268)
#define ENT_MATRIX  BYBIT(20, 24)
#define ENT_PLACE   BYBIT(4, 8)
#define TRACE_SIZE  BYBIT(0x28, 0x30)
#define TRACE_SPR   BYBIT(0x24, 0x28)

template<typename T> static bool Rd(const void* base, size_t off, T& out) { return base && SafeCopy((const uint8_t*)base + off, &out, sizeof(T)); }

static bool EntityPos(void* e, float& x, float& y, float& z, float* fx = NULL, float* fy = NULL)
{
    if(!e) return false;
    void* m = NULL; Rd(e, ENT_MATRIX, m);
    float p[3];
    if(m)
    {
        if(!SafeCopy((uint8_t*)m + 48, p, sizeof(p))) return false;
        if(fx && fy) { float f[2]; if(SafeCopy((uint8_t*)m + 16, f, sizeof(f))) { *fx = f[0]; *fy = f[1]; } }
    }
    else
    {
        float pl[4]; if(!SafeCopy((uint8_t*)e + ENT_PLACE, pl, sizeof(pl))) return false;
        p[0] = pl[0]; p[1] = pl[1]; p[2] = pl[2];
        if(fx && fy) { *fx = -sinf(pl[3]); *fy = cosf(pl[3]); }
    }
    x = p[0]; y = p[1]; z = p[2];
    return x == x && y == y && fabsf(x) < 1e5f && fabsf(y) < 1e5f;
}

/* ---- one snapshot of the player, refreshed every frame the HUD is drawn ---- */
struct VIPlayer
{
    bool ok; void* ped; void* veh; int handle;
    float x, y, z, fx, fy, hp, hpMax, armour, stamina;
    int money, wanted, weapon, ammoClip, ammoTotal, slot;
    int wType[13], wAmmo[13];
};
static VIPlayer g_pl;
static int g_lastMoney = 0, g_moneyDelta = 0; static bool g_moneySeen = false; static int64_t g_moneyMs = 0;

static void ReadPlayer()
{
    VIPlayer& p = g_pl; memset(&p, 0, sizeof(p));
    if(!fnFindPlayerPed || !InGame()) return;
    p.ped = fnFindPlayerPed(-1);
    if(!p.ped) return;
    if(!EntityPos(p.ped, p.x, p.y, p.z, &p.fx, &p.fy)) return;
    Rd(p.ped, PED_HEALTH, p.hp); Rd(p.ped, PED_ARMOUR, p.armour); Rd(p.ped, PED_VEHICLE, p.veh);
    uint8_t inCar = 0; if(p.veh) { float vx, vy, vz; if(!EntityPos(p.veh, vx, vy, vz, &p.fx, &p.fy)) p.veh = NULL; else { p.x = vx; p.y = vy; p.z = vz; inCar = 1; } }
    (void)inCar;
    p.hpMax = 100.0f; if(g_statFloat) { float mh = g_statFloat[24]; if(mh > 100 && mh < 2000) p.hpMax = 100.0f + (mh - 569.0f) / 4.31f; }   // STAT_MAX_HEALTH (569 = 100 hp, 1000 = 176)
    if(p.hpMax < 100) p.hpMax = 100;
    p.stamina = g_statFloat ? std::max(0.0f, std::min(1.0f, g_statFloat[22] / 1000.0f)) : 0.5f;
    int slot = 0; Rd(p.ped, PED_WSLOT, slot); p.slot = slot >= 0 && slot < 13 ? slot : 0;
    for(int i = 0; i < 13; ++i)
    {
        int w[4] = { 0, 0, 0, 0 };
        if(SafeCopy((uint8_t*)p.ped + PED_WEAPONS + i * PED_WSTRIDE, w, sizeof(w))) { p.wType[i] = w[0]; p.wAmmo[i] = w[3]; if(i == p.slot) { p.ammoClip = w[2]; p.ammoTotal = w[3]; p.weapon = w[0]; } }
    }
    if(g_scrOK)
    {
        uint32_t h = 0; CALLSCM(GET_PLAYER_CHAR, 0, &h); p.handle = (int)h;
        uint32_t m = 0; CALLSCM(STORE_SCORE, 0, &m); p.money = (int)m;
        uint32_t wl = 0; CALLSCM(STORE_WANTED_LEVEL, 0, &wl); p.wanted = (int)wl;
    }
    if(p.money != g_lastMoney) { g_moneyDelta = p.money - g_lastMoney; if(!g_moneySeen) g_moneyDelta = 0; g_moneySeen = true; g_lastMoney = p.money; g_moneyMs = NowMs(); }
    p.ok = true;
}

static VIGameInfo ReadGameInfo()
{
    VIGameInfo g; memset(&g, 0, sizeof(g));
    if(!InGame()) return g;
    ReadPlayer();
    if(!g_pl.ok) return g;
    g.ok = true; g.money = g_pl.money; g.wanted = g_pl.wanted; g.hp = g_pl.hp; g.armour = g_pl.armour;
    if(g_clockH) g.hour = *g_clockH; if(g_clockM) g.minute = *g_clockM;
    g.day = g_clockDay ? (int)*g_clockDay - 1 : 4;
    return g;
}

/* ---- the vector map: world -> screen inside a circle (or a rectangle for the pause preview) ---- */
struct MapView { float cx, cy, R, k, qx, qy, fx, fy; bool circle; float x0, y0, x1, y1; };
static inline void MapPt(const MapView& v, float wx, float wy, float& sx, float& sy)
{
    float dx = wx - v.qx, dy = wy - v.qy;
    sx = v.cx + (dx * v.fy - dy * v.fx) * v.k;
    sy = v.cy - (dx * v.fx + dy * v.fy) * v.k;
}
static bool ClipSeg(const MapView& v, float& ax, float& ay, float& bx, float& by)
{
    if(v.circle)
    {
        float dx = bx - ax, dy = by - ay, fx = ax - v.cx, fy = ay - v.cy;
        float a = dx * dx + dy * dy, b = 2 * (fx * dx + fy * dy), c = fx * fx + fy * fy - v.R * v.R;
        if(a < 1e-6f) return c <= 0;
        float disc = b * b - 4 * a * c; if(disc < 0) return false;
        float s = sqrtf(disc), t0 = (-b - s) / (2 * a), t1 = (-b + s) / (2 * a);
        if(t1 < 0 || t0 > 1) return false;
        t0 = std::max(0.0f, t0); t1 = std::min(1.0f, t1);
        float nax = ax + dx * t0, nay = ay + dy * t0; bx = ax + dx * t1; by = ay + dy * t1; ax = nax; ay = nay;
        return true;
    }
    // rectangle (Liang-Barsky)
    float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
    float p[4] = { -dx, dx, -dy, dy }, q[4] = { ax - v.x0, v.x1 - ax, ay - v.y0, v.y1 - ay };
    for(int i = 0; i < 4; ++i)
    {
        if(fabsf(p[i]) < 1e-6f) { if(q[i] < 0) return false; continue; }
        float r = q[i] / p[i];
        if(p[i] < 0) { if(r > t1) return false; if(r > t0) t0 = r; } else { if(r < t0) return false; if(r < t1) t1 = r; }
    }
    float nax = ax + dx * t0, nay = ay + dy * t0; bx = ax + dx * t1; by = ay + dy * t1; ax = nax; ay = nay;
    return true;
}
/* convex polygon clipped against the view (Sutherland-Hodgman against a 32-gon / the rectangle), drawn as a fan */
static void MapPoly(const MapView& v, const float* wx, const float* wy, int n, Col c)
{
    float px[48], py[48], qx[48], qy[48]; int m = n;
    for(int i = 0; i < n; ++i) MapPt(v, wx[i], wy[i], px[i], py[i]);
    const int E = v.circle ? 32 : 4;
    for(int e = 0; e < E && m > 0; ++e)
    {
        float ex, ey, nx, ny, d;
        if(v.circle) { float a = 6.2831853f * e / E; nx = cosf(a); ny = sinf(a); d = v.R * 1.001f; ex = v.cx; ey = v.cy; }
        else { static const float N[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }; nx = N[e][0]; ny = N[e][1]; ex = 0; ey = 0; d = e == 0 ? -v.x0 : e == 1 ? v.x1 : e == 2 ? -v.y0 : v.y1; }
        int k = 0;
        for(int i = 0; i < m && k < 46; ++i)
        {
            int j = (i + 1) % m;
            float di = (px[i] - ex) * nx + (py[i] - ey) * ny - d, dj = (px[j] - ex) * nx + (py[j] - ey) * ny - d;
            if(di <= 0) { qx[k] = px[i]; qy[k] = py[i]; ++k; }
            if((di <= 0) != (dj <= 0)) { float t = di / (di - dj); qx[k] = px[i] + (px[j] - px[i]) * t; qy[k] = py[i] + (py[j] - py[i]) * t; ++k; }
        }
        m = k; memcpy(px, qx, sizeof(float) * m); memcpy(py, qy, sizeof(float) * m);
    }
    for(int i = 1; i + 1 < m; ++i) Quad4(px[0], py[0], px[i], py[i], px[0], py[0], px[i + 1], py[i + 1], c, c, c, c);
}
static const Col MAP_LAND = { 38, 40, 52, 235 }, MAP_WATER = { 58, 92, 150, 235 }, MAP_PARK = { 52, 92, 66, 235 }, MAP_ROAD = { 150, 152, 168, 255 };

static void DrawMapLayers(const MapView& v)
{
    float reach = (v.circle ? v.R : std::max(v.x1 - v.x0, v.y1 - v.y0) * 0.75f) / v.k + 40.0f, R2 = reach * reach;
    for(int i = 0; i < SAMAP_PARKS; ++i)
    {
        const SaMapRect& r = kSaMapPark[i];
        float nx = std::max(r.x0, std::min(v.qx, r.x1)) - v.qx, ny = std::max(r.y0, std::min(v.qy, r.y1)) - v.qy;
        if(nx * nx + ny * ny > R2) continue;
        float xs[4] = { r.x0, r.x1, r.x1, r.x0 }, ys[4] = { r.y0, r.y0, r.y1, r.y1 };
        MapPoly(v, xs, ys, 4, MAP_PARK);
    }
    {   // the sea all around the map
        const float W = 3000.0f;
        struct B { float x0, y0, x1, y1; } bs[4] = { { -W - 6000, -W - 6000, -W, W + 6000 }, { W, -W - 6000, W + 6000, W + 6000 }, { -W, -W - 6000, W, -W }, { -W, W, W, W + 6000 } };
        for(const B& b : bs)
        {
            float nx = std::max(b.x0, std::min(v.qx, b.x1)) - v.qx, ny = std::max(b.y0, std::min(v.qy, b.y1)) - v.qy;
            if(nx * nx + ny * ny > R2) continue;
            float xs[4] = { b.x0, b.x1, b.x1, b.x0 }, ys[4] = { b.y0, b.y0, b.y1, b.y1 };
            MapPoly(v, xs, ys, 4, MAP_WATER);
        }
    }
    for(int i = 0; i < SAMAP_WATER; ++i)
    {
        const SaMapWater& w = kSaMapWater[i];
        float xs[4], ys[4], x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for(int k = 0; k < w.n; ++k) { xs[k] = w.x[k] * 0.25f; ys[k] = w.y[k] * 0.25f; x0 = std::min(x0, xs[k]); x1 = std::max(x1, xs[k]); y0 = std::min(y0, ys[k]); y1 = std::max(y1, ys[k]); }
        float nx = std::max(x0, std::min(v.qx, x1)) - v.qx, ny = std::max(y0, std::min(v.qy, y1)) - v.qy;
        if(nx * nx + ny * ny > R2) continue;
        MapPoly(v, xs, ys, w.n, MAP_WATER);
    }
    // roads (grid cells around the view, nearest cells first, capped so a big view never stalls the frame)
    int c0x = (int)((v.qx - reach + 3000) / SAMAP_CELL), c1x = (int)((v.qx + reach + 3000) / SAMAP_CELL);
    int c0y = (int)((v.qy - reach + 3000) / SAMAP_CELL), c1y = (int)((v.qy + reach + 3000) / SAMAP_CELL);
    c0x = std::max(0, c0x); c0y = std::max(0, c0y); c1x = std::min(SAMAP_GRID - 1, c1x); c1y = std::min(SAMAP_GRID - 1, c1y);
    float rw = std::max(1.6f, 7.0f * v.k);
    int budget = v.circle ? 1400 : 2600;
    for(int cy = c0y; cy <= c1y && budget > 0; ++cy) for(int cx = c0x; cx <= c1x && budget > 0; ++cx)
    {
        int c = cy * SAMAP_GRID + cx;
        for(int k = kSaMapCellStart[c]; k < kSaMapCellStart[c + 1] && budget > 0; ++k)
        {
            int e = kSaMapCellEdge[k];
            const int16_t* na = &kSaMapNode[kSaMapEdge[e * 2] * 3];
            const int16_t* nb = &kSaMapNode[kSaMapEdge[e * 2 + 1] * 3];
            float ax, ay, bx, by;
            MapPt(v, na[0] * 0.25f, na[1] * 0.25f, ax, ay); MapPt(v, nb[0] * 0.25f, nb[1] * 0.25f, bx, by);
            if(!ClipSeg(v, ax, ay, bx, by)) continue;
            VLine(ax, ay, bx, by, rw, MAP_ROAD); --budget;
        }
    }
}

static Col BlipColour(uint32_t c, bool bright)
{
    static const uint32_t k[9] = { 0xB4191DFF, 0x5CB55DFF, 0x5A79C6FF, 0xE6E6E6FF, 0xF2CF3AFF, 0xA855C9FF, 0x51C8E0FF, 0, 0 };
    uint32_t v = c < 7 ? k[c] : c;
    Col r = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), 255 };
    if(!bright && c < 7) r = Mix(r, Col{ 0, 0, 0, 255 }, 0.25f);
    return r;
}
static void DrawBlips(const MapView& v, float iconPx)
{
    if(!g_radarTrace) return;
    for(int i = 0; i < 175; ++i)
    {
        const uint8_t* t = g_radarTrace + i * TRACE_SIZE;
        uint8_t spr = 0, flags = 0, flags2 = 0;
        if(!SafeCopy(t + TRACE_SPR, &spr, 1) || !SafeCopy(t + TRACE_SPR + 1, &flags, 1) || !SafeCopy(t + TRACE_SPR + 2, &flags2, 1)) return;
        if(!(flags & 0x02)) continue;                          // bInUse (bTrackingBlip)
        int type = (flags2 >> 2) & 0x0F, display = flags2 & 0x03;
        if(display == 0 || display == 1) continue;            // not shown / marker only
        uint32_t colour = 0, handle = 0; float pos[3] = { 0, 0, 0 };
        Rd(t, 0, colour); Rd(t, 4, handle); SafeCopy(t + 8, pos, sizeof(pos));
        if(type == 1 || type == 2 || type == 3)
        {
            void* e = type == 1 ? (fnPoolVeh ? fnPoolVeh((int)handle) : NULL) : type == 2 ? (fnPoolPed ? fnPoolPed((int)handle) : NULL) : (fnPoolObj ? fnPoolObj((int)handle) : NULL);
            if(!e || !EntityPos(e, pos[0], pos[1], pos[2])) continue;
        }
        else if(type != 4 && type != 5 && type != 6) continue;
        float sx, sy; MapPt(v, pos[0], pos[1], sx, sy);
        bool inside = v.circle ? ((sx - v.cx) * (sx - v.cx) + (sy - v.cy) * (sy - v.cy) <= v.R * v.R) : (sx >= v.x0 && sx <= v.x1 && sy >= v.y0 && sy <= v.y1);
        if(!inside)
        {
            if(flags & 0x04) continue;                        // short range: only inside
            if(v.circle) { float dx = sx - v.cx, dy = sy - v.cy, l = sqrtf(dx * dx + dy * dy); sx = v.cx + dx / l * (v.R - iconPx * 0.4f); sy = v.cy + dy / l * (v.R - iconPx * 0.4f); }
            else { sx = std::max(v.x0, std::min(v.x1, sx)); sy = std::max(v.y0, std::min(v.y1, sy)); }
        }
        void* tex = (spr && g_blipSprites) ? g_blipSprites[spr] : NULL;
        if(tex) DrawTex(tex, sx - iconPx * 0.5f, sy - iconPx * 0.5f, sx + iconPx * 0.5f, sy + iconPx * 0.5f, 0, 0, 1, 1, COL_WHITE);
        else
        {
            float r = iconPx * 0.26f;
            VDisc(sx, sy, r + 1.5f, Col{ 0, 0, 0, 255 }); VDisc(sx, sy, r, BlipColour(colour, (flags & 0x01) != 0));
            if(pos[2] > g_pl.z + 4) Quad4(sx - r, sy - r * 1.6f, sx + r, sy - r * 1.6f, sx, sy - r * 2.6f, sx, sy - r * 2.6f, VI_WHITE, VI_WHITE, VI_WHITE, VI_WHITE);
        }
    }
}
static void PlayerArrow(float x, float y, float s, float ang)
{
    float c = cosf(ang), sn = sinf(ang);
    auto P = [&](float px, float py, float& ox, float& oy) { ox = x + px * c - py * sn; oy = y + px * sn + py * c; };
    float ax, ay, bx, by, lx, ly, rx, ry;
    P(0, -s, ax, ay); P(0, s * 0.35f, bx, by); P(-s * 0.75f, s * 0.8f, lx, ly); P(s * 0.75f, s * 0.8f, rx, ry);
    Col o = { 0, 0, 0, 255 };
    float k = 1.35f; float ax2, ay2, lx2, ly2, rx2, ry2, bx2, by2;
    P(0, -s * k, ax2, ay2); P(-s * 0.75f * k, s * 0.8f * k, lx2, ly2); P(s * 0.75f * k, s * 0.8f * k, rx2, ry2); P(0, s * 0.35f * k, bx2, by2);
    Quad4(ax2, ay2, rx2, ry2, lx2, ly2, bx2, by2, o, o, o, o);
    Quad4(ax, ay, rx, ry, lx, ly, bx, by, VI_WHITE, VI_WHITE, VI_WHITE, VI_WHITE);
}
static void CamForward(float& fx, float& fy)
{
    fx = 0; fy = 1;
    if(g_theCamera) { float f[3]; void* m = NULL; Rd(g_theCamera, ENT_MATRIX, m); if(m && SafeCopy((uint8_t*)m + 16, f, sizeof(f))) { float l = sqrtf(f[0] * f[0] + f[1] * f[1]); if(l > 0.1f) { fx = f[0] / l; fy = f[1] / l; } } }
}

static void DrawMapPreview(float x0, float y0, float x1, float y1)
{
    ReadPlayer();
    FillRect(x0, y0, x1, y1, MAP_LAND);
    MapView v; v.circle = false; v.x0 = x0; v.y0 = y0; v.x1 = x1; v.y1 = y1;
    v.cx = (x0 + x1) * 0.5f; v.cy = (y0 + y1) * 0.5f; v.R = 0; v.fx = 0; v.fy = 1;
    v.qx = g_pl.ok ? g_pl.x : 0; v.qy = g_pl.ok ? g_pl.y : 0;
    v.k = (y1 - y0) / 1100.0f;                                // about 1.1 km from top to bottom
    DrawMapLayers(v);
    DrawBlips(v, 0.032f * L.H);
    if(g_pl.ok) PlayerArrow(v.cx, v.cy, 0.012f * L.H, atan2f(g_pl.fx, g_pl.fy));
}

/* ---- the HUD pieces ---- */
static void StatRing(float cx, float cy, float r, float frac, Col col, int icon)
{
    VDisc(cx, cy, r, Col{ 0, 0, 0, 150 });
    VRing(cx, cy, r - r * 0.1f, r * 0.16f, Col{ 255, 255, 255, 50 });
    frac = std::max(0.0f, std::min(1.0f, frac));
    if(frac > 0) VRing(cx, cy, r - r * 0.1f, r * 0.16f, col, -1.5707963f, -1.5707963f + 6.2831853f * frac);
    float s = r * 0.42f;
    if(icon == 0)
    {   // heart: two discs + a triangle
        VDisc(cx - s * 0.48f, cy - s * 0.2f, s * 0.52f, col); VDisc(cx + s * 0.48f, cy - s * 0.2f, s * 0.52f, col);
        Quad4(cx - s * 0.98f, cy - s * 0.05f, cx + s * 0.98f, cy - s * 0.05f, cx, cy + s * 0.95f, cx, cy + s * 0.95f, col, col, col, col);
    }
    else if(icon == 1)
    {   // lightning bolt
        Quad4(cx + s * 0.25f, cy - s, cx + s * 0.55f, cy - s, cx - s * 0.45f, cy + s * 0.12f, cx + s * 0.05f, cy + s * 0.12f, col, col, col, col);
        Quad4(cx - s * 0.05f, cy - s * 0.12f, cx + s * 0.45f, cy - s * 0.12f, cx - s * 0.55f, cy + s, cx - s * 0.25f, cy + s, col, col, col, col);
    }
    else
    {   // shield
        Quad4(cx - s * 0.8f, cy - s * 0.85f, cx + s * 0.8f, cy - s * 0.85f, cx - s * 0.8f, cy + s * 0.1f, cx + s * 0.8f, cy + s * 0.1f, col, col, col, col);
        Quad4(cx - s * 0.8f, cy + s * 0.1f, cx + s * 0.8f, cy + s * 0.1f, cx, cy + s, cx, cy + s, col, col, col, col);
    }
}
static void Star(float cx, float cy, float r, Col c)
{
    for(int i = 0; i < 5; ++i)
    {
        float a = -1.5707963f + i * 1.2566371f, b = a + 0.6283185f, d = a - 0.6283185f;
        Quad4(cx + cosf(a) * r, cy + sinf(a) * r, cx + cosf(b) * r * 0.42f, cy + sinf(b) * r * 0.42f, cx + cosf(d) * r * 0.42f, cy + sinf(d) * r * 0.42f, cx, cy, c, c, c, c);
    }
}
static const char* WeaponName(int t)
{
    static const char* n[47] = { "FIST", "BRASS KNUCKLES", "GOLF CLUB", "NIGHTSTICK", "KNIFE", "BASEBALL BAT", "SHOVEL", "POOL CUE", "KATANA", "CHAINSAW",
        "DILDO", "DILDO", "VIBRATOR", "VIBRATOR", "FLOWERS", "CANE", "GRENADE", "TEAR GAS", "MOLOTOV", "", "", "", "PISTOL", "SILENCED PISTOL", "DESERT EAGLE",
        "SHOTGUN", "SAWN-OFF", "COMBAT SHOTGUN", "MICRO SMG", "SMG", "AK-47", "M4", "TEC-9", "RIFLE", "SNIPER RIFLE", "RPG", "HEAT RPG", "FLAMETHROWER",
        "MINIGUN", "SATCHEL", "DETONATOR", "SPRAY CAN", "EXTINGUISHER", "CAMERA", "NIGHT VISION", "THERMAL", "PARACHUTE" };
    return t >= 0 && t < 47 ? n[t] : "";
}
static std::string MoneyText(int v)
{
    char b[32]; unsigned a = (unsigned)(v < 0 ? -v : v);
    if(a >= 1000000) snprintf(b, sizeof(b), "$%u,%03u,%03u", a / 1000000, a / 1000 % 1000, a % 1000);
    else if(a >= 1000) snprintf(b, sizeof(b), "$%u,%03u", a / 1000, a % 1000);
    else snprintf(b, sizeof(b), "$%u", a);
    return (v < 0 ? "-" : "") + std::string(b);
}

/* ---- the item wheel (weapons on foot, radio stations in a car) ---- */
static bool g_wheelOn = false; static int g_wheelSel = -1; static bool g_wheelRadio = false;
static int64_t g_holdMs = 0; static bool g_holdDown = false;
static float g_wpArea[4];
static const char* kRadio[13] = { "PLAYBACK FM", "K ROSE", "K-DST", "BOUNCE FM", "SF-UR", "RADIO LOS SANTOS", "RADIO X", "CSR 103.9", "K-JAH WEST", "MASTER SOUNDS", "WCTR", "USER TRACKS", "RADIO OFF" };
static int WheelCount() { return g_wheelRadio ? 13 : 13; }
static std::string WheelLabel(int i) { if(g_wheelRadio) return kRadio[i]; int t = g_pl.wType[i]; return (t > 0 || i == 0) ? std::string(WeaponName(t)) : std::string(); }
static void WheelUpdate(const Touch& t)
{
    float cx = L.cx, cy = L.H * 0.5f, R = 0.30f * L.H;
    if(t.down)
    {
        float dx = t.x - cx, dy = t.y - cy;
        if(dx * dx + dy * dy > (R * 0.3f) * (R * 0.3f)) { float a = atan2f(dy, dx) + 1.5707963f; if(a < 0) a += 6.2831853f; int n = WheelCount(); int s = (int)(a / 6.2831853f * n + 0.5f) % n; if(g_wheelRadio || !WheelLabel(s).empty()) g_wheelSel = s; }
    }
    else
    {   // lifted: apply
        if(g_wheelSel >= 0)
        {
            if(g_wheelRadio) { if(fnRetune && g_pAudioEngine) fnRetune(g_pAudioEngine, (signed char)(g_wheelSel == 12 ? 13 : g_wheelSel)); }
            else if(g_scrOK && g_pl.handle) CALLSCM(SET_CURRENT_CHAR_WEAPON, g_pl.handle, g_pl.wType[g_wheelSel]);
        }
        g_wheelOn = false; g_wheelSel = -1;
    }
}
static void WheelDraw()
{
    float cx = L.cx, cy = L.H * 0.5f, R = 0.30f * L.H, r0 = R * 0.42f;
    FillRect(0, 0, L.W, L.H, Col{ 0, 0, 0, 110 });
    int n = WheelCount();
    for(int i = 0; i < n; ++i)
    {
        float a0 = -1.5707963f + (i - 0.5f) * 6.2831853f / n + 0.02f, a1 = -1.5707963f + (i + 0.5f) * 6.2831853f / n - 0.02f;
        bool sel = (i == g_wheelSel), have = g_wheelRadio || !WheelLabel(i).empty();
        Col c = sel ? VI_PINK : Col{ 12, 12, 16, (uint8_t)(have ? 200 : 120) };
        for(int s = 0; s < 6; ++s)
        {
            float t0 = a0 + (a1 - a0) * s / 6, t1 = a0 + (a1 - a0) * (s + 1) / 6;
            Quad4(cx + cosf(t0) * R, cy + sinf(t0) * R, cx + cosf(t1) * R, cy + sinf(t1) * R, cx + cosf(t0) * r0, cy + sinf(t0) * r0, cx + cosf(t1) * r0, cy + sinf(t1) * r0, c, c, c, c);
        }
        float am = (a0 + a1) * 0.5f, lr = (R + r0) * 0.5f;
        std::string lb = WheelLabel(i);
        if(!lb.empty())
        {
            float fh = 0.0125f * L.H, mw = std::min(R - r0, lr * 6.2831853f / n) * 0.9f;
            std::string shortLb = Fit(g_bank, lb, fh / g_bank.capH, 0, mw);
            Txt(0, shortLb, cx + cosf(am) * lr, cy + sinf(am) * lr, fh, VI_WHITE, 1);
        }
    }
    VDisc(cx, cy, r0 * 0.92f, Col{ 0, 0, 0, 220 });
    std::string mid = g_wheelSel >= 0 ? WheelLabel(g_wheelSel) : (g_wheelRadio ? "RADIO" : "WEAPONS");
    Txt(2, Fit(VF(2), mid, 0.016f * L.H / VF(2).capH, 0, r0 * 1.75f), cx, cy - 0.012f * L.H, 0.016f * L.H, VI_WHITE, 1);
    if(!g_wheelRadio && g_wheelSel >= 0 && g_pl.wType[g_wheelSel] >= 16) { char b[24]; snprintf(b, sizeof(b), "%d", g_pl.wAmmo[g_wheelSel]); Txt(0, b, cx, cy + 0.025f * L.H, 0.016f * L.H, VI_GREY, 1); }
}

static void PedActions()
{
    struct A { const char* t; int g; } a[4]; int n = 0;
    if(g_pl.veh) { a[n++] = { "Exit vehicle", 2 }; a[n++] = { "Radio (hold weapon)", 3 }; a[n++] = { "Handbrake", 0 }; }
    else
    {
        if(g_pl.weapon >= 22) a[n++] = { "Aim / shoot", 1 }; else a[n++] = { "Attack", 1 };
        a[n++] = { "Sprint", 0 }; a[n++] = { "Enter vehicle", 2 }; a[n++] = { "Item wheel (hold weapon)", 3 };
    }
    float h = 0.016f * L.H, x = 0.975f * L.W, y = 0.62f * L.H;
    for(int i = 0; i < n; ++i, y += 0.042f * L.H)
    {
        float w = TxtW(0, a[i].t, h) + 2.6f * h + 0.02f * L.W;
        FillRect(x - w, y - 0.017f * L.H, x, y + 0.017f * L.H, Col{ 0, 0, 0, 120 });
        Hint(a[i].t, a[i].g, x - 0.006f * L.W, y, h, Col{ 240, 240, 245, 230 });
    }
}

static void VIHudDraw()
{
    if(!g_cfgHud || !g_resOK || !InGame()) return;
    if(g_kind != SK_NONE || NowMs() - g_liveMs < 300) return;
    if(g_displayHud && !*g_displayHud) return;
    if(g_cutscene && *g_cutscene) return;
    ReadPlayer();
    if(!g_pl.ok) return;
    ComputeLayout(); SetStates();
    const float W = L.W, H = L.H;
    // radar
    float R = 0.125f * H, rcx = 0.035f * W + R, rcy = g_cfgHudRadarTop ? 0.05f * H + R : H - 0.045f * H - R;
    VDisc(rcx, rcy, R, MAP_LAND, 64);
    MapView v; v.circle = true; v.cx = rcx; v.cy = rcy; v.R = R; v.qx = g_pl.x; v.qy = g_pl.y;
    CamForward(v.fx, v.fy);
    v.k = R / (g_pl.veh ? 330.0f : 200.0f);
    DrawMapLayers(v);
    DrawBlips(v, 0.034f * H);
    // the player arrow: own heading relative to the camera
    float rel = atan2f(g_pl.fx, g_pl.fy) - atan2f(v.fx, v.fy);
    PlayerArrow(rcx, rcy, 0.011f * H, rel);
    VRing(rcx, rcy, R, 0.007f * H, VI_PINK, 0, 6.2831853f, 72);
    { float nx = 0, ny = 0; MapPt(v, g_pl.x, g_pl.y + 1000, nx, ny); float dx = nx - rcx, dy = ny - rcy, l = sqrtf(dx * dx + dy * dy); if(l > 1) { nx = rcx + dx / l * R; ny = rcy + dy / l * R; VDisc(nx, ny, 0.017f * H, Col{ 0, 0, 0, 230 }); Txt(2, "N", nx, ny, 0.014f * H, VI_WHITE, 1); } }
    // health / armour / stamina rings, right of the radar
    float sr = 0.030f * H, sx = rcx + R + 0.012f * W + sr, sy = rcy + R - sr;
    StatRing(sx, sy, sr, g_pl.hp / g_pl.hpMax, g_pl.hp < g_pl.hpMax * 0.25f ? Col{ 235, 60, 60, 255 } : Col{ 120, 220, 120, 255 }, 0);
    StatRing(sx, sy - 2.3f * sr, sr, g_pl.stamina, Col{ 255, 205, 60, 255 }, 1);
    if(g_pl.armour > 0.5f) StatRing(sx, sy - 4.6f * sr, sr, g_pl.armour / 100.0f, Col{ 90, 170, 255, 255 }, 2);
    // top right: weapon + ammo, money, wanted stars
    float xr = 0.975f * W, ty = 0.06f * H;
    std::string wn = WeaponName(g_pl.weapon);
    float wbx0 = 0.80f * W;
    g_wpArea[0] = wbx0; g_wpArea[1] = 0.02f * H; g_wpArea[2] = W; g_wpArea[3] = 0.12f * H;
    Slant(wbx0, ty - 0.032f * H, xr, ty + 0.032f * H, 0.012f * H, Col{ 0, 0, 0, 120 }, Col{ 0, 0, 0, 170 });
    Txt(2, wn.empty() ? std::string("FIST") : wn, xr - 0.01f * W, ty - 0.008f * H, 0.018f * H, VI_WHITE, 2);
    if(g_pl.weapon >= 16 && g_pl.weapon != 46)
    {
        char b[32]; snprintf(b, sizeof(b), "%d  |  %d", g_pl.ammoClip, std::max(0, g_pl.ammoTotal - g_pl.ammoClip));
        Txt(0, b, xr - 0.01f * W, ty + 0.017f * H, 0.014f * H, VI_GREY, 2);
    }
    float my = ty + 0.07f * H;
    Txt(2, MoneyText(g_pl.money), xr, my, 0.026f * H, Col{ 255, 255, 255, 255 }, 2);
    if(g_moneyDelta && NowMs() - g_moneyMs < 3000)
    {
        char b[32]; snprintf(b, sizeof(b), "%s%s", g_moneyDelta > 0 ? "+" : "", MoneyText(g_moneyDelta).c_str());
        Txt(2, b, xr, my + 0.04f * H, 0.018f * H, g_moneyDelta > 0 ? Col{ 120, 230, 140, 255 } : Col{ 235, 80, 80, 255 }, 2);
    }
    if(g_pl.wanted > 0)
    {
        float r = 0.016f * H, y = my + (g_moneyDelta && NowMs() - g_moneyMs < 3000 ? 0.085f : 0.05f) * H, x = xr - r;
        bool blink = (NowMs() / 400) % 2 == 0;
        for(int i = 0; i < 6; ++i, x -= r * 2.4f) { if(i < g_pl.wanted) { Star(x, y, r + 1.5f, Col{ 0, 0, 0, 255 }); Star(x, y, r, blink ? VI_WHITE : VI_PINK); } else Star(x, y, r * 0.8f, Col{ 0, 0, 0, 90 }); }
    }
    // the clock under the radar ring (top: under the radar, bottom: above it)
    if(g_clockH && g_clockM) { char b[16]; snprintf(b, sizeof(b), "%02d:%02d", *g_clockH, *g_clockM); Txt(2, b, rcx, g_cfgHudRadarTop ? rcy + R + 0.03f * H : rcy - R - 0.03f * H, 0.017f * H, VI_WHITE, 1); }
    // input: long press on the weapon area = the item wheel
    Touch t; ReadTouch(t);
    bool inW = t.x >= g_wpArea[0] && t.x <= g_wpArea[2] && t.y >= g_wpArea[1] && t.y <= g_wpArea[3];
    if(g_wheelOn) WheelUpdate(t);
    else if(t.down && inW) { if(!g_holdDown) { g_holdDown = true; g_holdMs = NowMs(); } else if(NowMs() - g_holdMs > 450) { g_wheelOn = true; g_wheelRadio = g_pl.veh != NULL; g_wheelSel = g_wheelRadio ? -1 : g_pl.slot; } }
    else g_holdDown = false;
    if(g_wheelOn) WheelDraw(); else PedActions();
    fnRenderState(RS_ZTEST, (void*)1);
    fnRenderState(RS_ZWRITE, (void*)1);
}

/* the stock widgets are skipped while our HUD is on */
DECL_HOOKv(VI_PlayerInfoDraw, void* self) { if(g_cfgHud && g_resOK) return; VI_PlayerInfoDraw(self); }
DECL_HOOKv(VI_RadarDraw, void* self) { if(g_cfgHud && g_resOK) return; VI_RadarDraw(self); }
DECL_HOOKv(VI_DrawWanted, void) { if(g_cfgHud && g_resOK) return; VI_DrawWanted(); }

static void InstallVIHud()
{
    g_scrOK = Scripting::Init(g_hGame);
    SET_TO(fnFindPlayerPed, aml->GetSym(g_hGame, "_Z13FindPlayerPedi"));
    SET_TO(fnPoolPed, aml->GetSym(g_hGame, "_ZN6CPools6GetPedEi"));
    SET_TO(fnPoolVeh, aml->GetSym(g_hGame, "_ZN6CPools10GetVehicleEi"));
    SET_TO(fnPoolObj, aml->GetSym(g_hGame, "_ZN6CPools9GetObjectEi"));
    g_radarTrace  = (uint8_t*)aml->GetSym(g_hGame, "_ZN6CRadar13ms_RadarTraceE");
    g_blipSprites = (void**)aml->GetSym(g_hGame, "_ZN6CRadar16RadarBlipSpritesE");
    g_statFloat   = (float*)aml->GetSym(g_hGame, "_ZN6CStats14StatTypesFloatE");
    g_clockH      = (uint8_t*)aml->GetSym(g_hGame, "_ZN6CClock18ms_nGameClockHoursE");
    g_clockM      = (uint8_t*)aml->GetSym(g_hGame, "_ZN6CClock20ms_nGameClockMinutesE");
    g_clockDay    = (uint8_t*)aml->GetSym(g_hGame, "_ZN6CClock10CurrentDayE");
    g_displayHud  = (uint8_t*)aml->GetSym(g_hGame, "_ZN11CTheScripts11bDisplayHudE");
    g_cutscene    = (uint8_t*)aml->GetSym(g_hGame, "_ZN12CCutsceneMgr10ms_runningE");
    g_theCamera   = (void*)aml->GetSym(g_hGame, "TheCamera");
    LOGI("VI HUD: scripting %d, player %p, radar trace %p, blip sprites %p, stats %p, clock %p/%p, camera %p",
         (int)g_scrOK, (void*)fnFindPlayerPed, g_radarTrace, (void*)g_blipSprites, (void*)g_statFloat, g_clockH, g_clockM, g_theCamera);
    if(!g_cfgHud) { LOGI("VI HUD switched off (Hud=0): the stock HUD stays"); return; }
    if(!fnFindPlayerPed) { g_cfgHud = 0; LOGE("VI HUD off: FindPlayerPed not found"); return; }
    int n = 0;
    n += HookLoading("CWidgetPlayerInfo::Draw", "_ZN17CWidgetPlayerInfo4DrawEv", (void*)&HookOf_VI_PlayerInfoDraw, (void**)&VI_PlayerInfoDraw);
    n += HookLoading("CWidgetRadar::Draw", "_ZN12CWidgetRadar4DrawEv", (void*)&HookOf_VI_RadarDraw, (void**)&VI_RadarDraw);
    n += HookLoading("CHud::DrawWanted", "_ZN4CHud10DrawWantedEv", (void*)&HookOf_VI_DrawWanted, (void**)&VI_DrawWanted);
    g_hudHooks = n > 0;
    LOGI("VI HUD on: %d of 3 stock HUD parts replaced", n);
}
