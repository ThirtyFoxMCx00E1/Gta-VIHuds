/* Host preview of VIHud: compiles the real mod sources (main.cpp + vitheme.h + vihud.h + pcextra.h) on the PC
 * with a small software rasterizer standing in for MobileMenu::DrawQuad / DrawSprite, then calls the very same
 * draw functions the game calls. The game side (player, money, blips, settings) is a fake memory image that the
 * mod reads through its normal code paths (CPed fields, CRadar::ms_RadarTrace, script opcodes).
 *
 * build (from source/):
 *   g++ -O2 -std=c++17 -D_WIN64 -DVIHUD -DPCMENU_TRANSPARENT -Ijni -Itools/stub -I../../wagu/imgui \
 *       tools/vi_preview.cpp -o /tmp/vi_preview
 * run:  /tmp/vi_preview <VIHud asset dir> <out dir>      -> main.ppm pause.ppm map.ppm settings.ppm dialog.ppm hud.ppm wheel.ppm loading.ppm
 */
#include "../jni/main.cpp"
#include "../jni/scripting.cpp"
Logger::Logger() {} 
void Logger::SetTag(const char*) {}
void Logger::Info(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
void Logger::Error(const char* f, ...) { va_list a; va_start(a, f); printf("E: "); vprintf(f, a); va_end(a); printf("\n"); }
static Logger s_logger; Logger* logger = &s_logger;
#include <map>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>

SLInterfaceID SL_IID_ENGINE, SL_IID_PLAY, SL_IID_VOLUME, SL_IID_BUFFERQUEUE, SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
SLresult slCreateEngine(SLObjectItf*, SLuint32, const void*, SLuint32, const SLInterfaceID*, const SLboolean*) { return 1; }

/* ---- software canvas ---- */
static int CW = 1600, CH = 720;
static std::vector<float> g_px;
struct FakeRaster { uint8_t pad[RWRAS_WIDTH]; int w, h; };
struct FakeTex { FakeRaster* raster; int w, h; std::vector<float> rgba; };

static void Blend(int x, int y, float r, float g, float b, float a)
{
    if(x < 0 || y < 0 || x >= CW || y >= CH || a <= 0) return;
    float* p = &g_px[(y * CW + x) * 3];
    p[0] = p[0] * (1 - a) + r * a; p[1] = p[1] * (1 - a) + g * a; p[2] = p[2] * (1 - a) + b * a;
}
static void Sample(const FakeTex* t, float u, float v, float* o)
{
    u = std::min(1.0f, std::max(0.0f, u)) * t->w - 0.5f; v = std::min(1.0f, std::max(0.0f, v)) * t->h - 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v); float fx = u - x0, fy = v - y0;
    auto at = [&](int x, int y, int c) { x = std::min(t->w - 1, std::max(0, x)); y = std::min(t->h - 1, std::max(0, y)); return t->rgba[(y * t->w + x) * 4 + c]; };
    for(int c = 0; c < 4; ++c) o[c] = (at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx) * (1 - fy) + (at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx) * fy;
}
static void Tri(const V2* p, const Col* c, const V2* uv, const FakeTex* t, int a, int b, int d)
{
    float x0 = p[a].x, y0 = p[a].y, x1 = p[b].x, y1 = p[b].y, x2 = p[d].x, y2 = p[d].y;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if(fabsf(area) < 1e-4f) return;
    int minx = std::max(0, (int)floorf(std::min({ x0, x1, x2 }))), maxx = std::min(CW - 1, (int)ceilf(std::max({ x0, x1, x2 })));
    int miny = std::max(0, (int)floorf(std::min({ y0, y1, y2 }))), maxy = std::min(CH - 1, (int)ceilf(std::max({ y0, y1, y2 })));
    for(int y = miny; y <= maxy; ++y) for(int x = minx; x <= maxx; ++x)
    {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
        float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
        float w2 = 1 - w0 - w1;
        if(w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
        float r = (c[a].r * w0 + c[b].r * w1 + c[d].r * w2) / 255.0f, g = (c[a].g * w0 + c[b].g * w1 + c[d].g * w2) / 255.0f;
        float bl = (c[a].b * w0 + c[b].b * w1 + c[d].b * w2) / 255.0f, al = (c[a].a * w0 + c[b].a * w1 + c[d].a * w2) / 255.0f;
        if(t && uv)
        {
            float s[4]; Sample(t, uv[a].x * w0 + uv[b].x * w1 + uv[d].x * w2, uv[a].y * w0 + uv[b].y * w1 + uv[d].y * w2, s);
            r *= s[0]; g *= s[1]; bl *= s[2]; al *= s[3];
        }
        Blend(x, y, r, g, bl, al);
    }
}
static void SwQuad(V2* v, Col* c) { Tri(v, c, NULL, NULL, 0, 1, 2); Tri(v, c, NULL, NULL, 1, 3, 2); }
static void SwSprite(void* tex, V2* v, V2* uv, Col* c) { FakeTex* t = (FakeTex*)tex; if(!t) return; Tri(v, c, uv, t, 0, 1, 2); Tri(v, c, uv, t, 1, 3, 2); }
static void SwState(int, void*) {}
struct FakeImg { int w, h; unsigned char* d; };
static void* SwPngRead(const char* path) { int w, h, n; unsigned char* d = stbi_load(path, &w, &h, &n, 4); if(!d) return NULL; return new FakeImg{ w, h, d }; }
static void* SwFindFmt(void* img, int, int* w, int* h, int* d, int* f) { FakeImg* i = (FakeImg*)img; *w = i->w; *h = i->h; *d = 32; *f = 0; return img; }
static void* SwRasterCreate(int w, int h, int, int) { FakeRaster* r = new FakeRaster(); r->w = w; r->h = h; return r; }
static std::map<void*, FakeImg*> g_rasterImg;
static void* SwRasterFromImg(void* r, void* img) { g_rasterImg[r] = (FakeImg*)img; return r; }
static void* SwTexCreate(void* r)
{
    FakeTex* t = new FakeTex(); t->raster = (FakeRaster*)r; FakeImg* i = g_rasterImg[r]; t->w = i->w; t->h = i->h; t->rgba.resize((size_t)i->w * i->h * 4);
    for(size_t k = 0; k < t->rgba.size(); ++k) t->rgba[k] = i->d[k] / 255.0f;
    return t;
}
static void SwImgDestroy(void* img) { FakeImg* i = (FakeImg*)img; stbi_image_free(i->d); i->d = NULL; }   // (the texture keeps its own copy)
static int g_touchDown = 0, g_touchX = 0, g_touchY = 0;
static int SwPtrButton(int, int) { return g_touchDown ? 3 : 0; }
static int SwPtrCoords(int, int* x, int* y, float*) { *x = g_touchX; *y = g_touchY; return 0; }

static void Save(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb"); fprintf(f, "P6 %d %d 255\n", CW, CH);
    std::vector<unsigned char> b(g_px.size()); for(size_t i = 0; i < b.size(); ++i) b[i] = (unsigned char)std::min(255.0f, std::max(0.0f, g_px[i] * 255.0f + 0.5f));
    fwrite(b.data(), 1, b.size(), f); fclose(f); printf("wrote %s\n", path.c_str());
}
static void Clear() { g_px.assign((size_t)CW * CH * 3, 0.0f); }

/* ---- the fake game: a CPed, a camera, radar traces and the script opcodes the HUD uses ---- */
static uint8_t g_ped[4096], g_pedMat[64], g_cam[256], g_camMat[64], g_trace[175 * TRACE_SIZE], g_pedB[256], g_pedBMat[64];
static int g_fakeMoney = 48250, g_fakeWanted = 2;
static void* FakeFindPlayer(int) { return g_ped; }
static void* FakePoolPed(int h) { return h == 77 ? g_pedB : NULL; }
static void FakeProcess(GAME_SCRIPT_THREAD* s)
{
    uint16_t op; memcpy(&op, (void*)s->dwScriptIP, 2);
    switch(op)
    {
        case 0x01F5: s->dwLocalVar[0] = 1; break;                       // GET_PLAYER_CHAR -> handle 1
        case 0x010B: s->dwLocalVar[0] = (uint32_t)g_fakeMoney; break;   // STORE_SCORE
        case 0x01C0: s->dwLocalVar[0] = (uint32_t)g_fakeWanted; break;  // STORE_WANTED_LEVEL
        default: break;
    }
}
static void PutF(uint8_t* b, size_t off, float v) { memcpy(b + off, &v, 4); }
static void SetMatrix(uint8_t* m, float x, float y, float z, float heading)
{
    memset(m, 0, 64);
    PutF(m, 0, cosf(heading)); PutF(m, 4, sinf(heading));                   // right
    PutF(m, 16, -sinf(heading)); PutF(m, 20, cosf(heading));                // forward
    PutF(m, 48, x); PutF(m, 52, y); PutF(m, 56, z);
}
static void AddBlip(int i, int type, uint32_t colour, uint32_t handle, float x, float y, uint8_t spr, int display)
{
    uint8_t* t = g_trace + i * TRACE_SIZE;
    memcpy(t, &colour, 4); memcpy(t + 4, &handle, 4); PutF(t, 8, x); PutF(t, 12, y); PutF(t, 16, 13);
    t[TRACE_SPR] = spr; t[TRACE_SPR + 1] = 0x02 | 0x01; t[TRACE_SPR + 2] = (uint8_t)((type << 2) | display);
}
static void SetupGame()
{
    // Grove Street, looking north-east
    float px = 2495.0f, py = -1668.0f;
    SetMatrix(g_pedMat, px, py, 13, -0.6f);
    void* mp = g_pedMat; memcpy(g_ped + ENT_MATRIX, &mp, sizeof(mp));
    PutF(g_ped, PED_HEALTH, 74); PutF(g_ped, PED_ARMOUR, 40);
    int slot = 3; memcpy(g_ped + PED_WSLOT, &slot, 4);
    int w0[4] = { 0, 0, 0, 1 }, w2[4] = { 24, 0, 7, 63 }, w3[4] = { 25, 0, 1, 18 }, w5[4] = { 31, 0, 50, 350 }, w1[4] = { 5, 0, 0, 1 };
    memcpy(g_ped + PED_WEAPONS + 0 * PED_WSTRIDE, w0, 16); memcpy(g_ped + PED_WEAPONS + 1 * PED_WSTRIDE, w1, 16);
    memcpy(g_ped + PED_WEAPONS + 2 * PED_WSTRIDE, w2, 16); memcpy(g_ped + PED_WEAPONS + 3 * PED_WSTRIDE, w3, 16); memcpy(g_ped + PED_WEAPONS + 5 * PED_WSTRIDE, w5, 16);
    SetMatrix(g_camMat, px, py - 6, 15, -0.45f); void* cm = g_camMat; memcpy(g_cam + ENT_MATRIX, &cm, sizeof(cm));
    SetMatrix(g_pedBMat, px + 120, py + 60, 13, 0); void* bm = g_pedBMat; memcpy(g_pedB + ENT_MATRIX, &bm, sizeof(bm));
    memset(g_trace, 0, sizeof(g_trace));
    AddBlip(0, 4, 4, 0, 2470, -1540, 0, 3);      // coord blip, yellow (mission)
    AddBlip(1, 2, 1, 77, 0, 0, 0, 3);            // a ped blip (green), position from the ped pool
    AddBlip(2, 5, 5, 0, 2620, -1860, 0, 3);      // contact point, purple
    AddBlip(3, 4, 0xE84A9EFF, 0, 1800, -1300, 0, 3);   // far away (shown on the rim)
    static float stats[400]; stats[22] = 640; stats[24] = 800;
    g_statFloat = stats;
    static uint8_t hh = 21, mm = 37, day = 6; g_clockH = &hh; g_clockM = &mm; g_clockDay = &day;
    fnFindPlayerPed = FakeFindPlayer; fnPoolPed = FakePoolPed;
    g_radarTrace = g_trace; g_theCamera = g_cam;
    ProcessOneCommand = FakeProcess; gst = new GAME_SCRIPT_THREAD; memset(gst, 0, sizeof(*gst)); g_scrOK = true;
}

/* fake MobileSettings (for the settings page values) */
static uint8_t g_settings[1024 * SET_STRIDE]; static void* g_settingsPtr = g_settings;
static void SetSetting(int id, int v, int mn, int mx, int type) { uint8_t* b = g_settings + id * SET_STRIDE; memcpy(b + SET_VALUE, &v, 4); memcpy(b + SET_MIN, &mn, 4); memcpy(b + SET_MAX, &mx, 4); memcpy(b + SET_TYPE, &type, 4); }

static void* MakeTex(const char* path) { return LoadPNG(path); }

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "../assets/VIHud/", out = argc > 2 ? argv[2] : "/tmp/vi";
    if(dir.back() != '/') dir += '/';
    static int rs[64]; rs[OFF_RS_WIDTH / 4] = CW; rs[OFF_RS_HEIGHT / 4] = CH; g_pRsGlobal = rs;
    fnDrawQuad = SwQuad; fnDrawSprite = SwSprite; fnRenderState = SwState;
    fnPngRead = (PngReadFn)SwPngRead; fnFindRasterFmt = (FindRasterFmtFn)SwFindFmt; fnRasterCreate = SwRasterCreate; fnRasterFromImg = SwRasterFromImg; fnTexCreate = SwTexCreate; fnImgDestroy = SwImgDestroy;
    fnPtrButton = SwPtrButton; fnPtrCoords = SwPtrCoords;
    g_dirPC = dir; g_cfgSounds = false; g_cfgName = "CARL JOHNSON";
    LoadResources();
    if(!g_resOK) { printf("fonts missing in %s\n", dir.c_str()); return 1; }
    SetupGame();
    // the "paused game" frame behind the pause menu: the HUD preview frame (a picture stands in for the 3D world)
    void* world = MakeTex("pc/lds/mobilesc4.png");
    auto Entries = [](bool pause)
    {
        g_entries.clear();
        if(pause)
        {
            g_entries.push_back({ "MAP", K_MAP, "FEH_MAP", NULL });
            g_entries.push_back({ "RESUME", K_STOCK, "FEP_RES", NULL });
            g_entries.push_back({ "ONLINE", K_MP, "MP", NULL });
            g_entries.push_back({ "PROGRESS", K_STOCK, "FEH_STA", NULL });
            g_entries.push_back({ "BRIEF", K_STOCK, "FEH_BRI", NULL });
            g_entries.push_back({ "SETTINGS", K_STOCK, "FEP_OPT", NULL });
            g_entries.push_back({ "QUIT GAME", K_STOCK, "FEP_QUI", NULL });
        }
        else
        {
            g_entries.push_back({ "STORY", K_STOCK, "FEP_STG", NULL });
            g_entries.push_back({ "ONLINE", K_MP, "MP", NULL });
            g_entries.push_back({ "SETTINGS", K_STOCK, "FEP_OPT", NULL });
            g_entries.push_back({ "EXIT", K_STOCK, "FEP_QUI", NULL });
        }
    };
    ComputeLayout();
    // main menu
    Clear(); Entries(false); g_isPause = false; g_sel = 0; g_kind = SK_ROOT; DrawMainView(); Save(out + "/main.ppm");
    Clear(); g_sel = 1; DrawMainView(); Save(out + "/main_online.ppm");
    // pause menu over the game frame
    g_capTex = world; g_capValid = true; g_cfgTrBlur = false;
    Clear(); Entries(true); g_isPause = true; g_sel = 1; DrawMainView(); Save(out + "/pause.ppm");
    Clear(); g_sel = 0; DrawMainView(); Save(out + "/pause_map.ppm");
    // settings (Display page)
    g_gotSettings = (uintptr_t)&g_settingsPtr;
    SetSetting(1, 2, 0, 3, 0); SetSetting(2, 70, 0, 100, 1); SetSetting(3, 1, 0, 1, 0); SetSetting(4, 45, 0, 100, 1); SetSetting(5, 0, 0, 1, 0); SetSetting(6, 3, 0, 4, 0);
    g_optRows.clear(); for(const char* c : { "Display", "Audio", "Controls", "Language", "Game", "Mods" }) g_optRows.push_back({ c, c, NULL, 0 });
    g_viCats.clear(); for(auto& r : g_optRows) g_viCats.push_back(r.label);
    g_kind = SK_PAGE; g_style = PS_SETTINGS; g_pageTitle = "Display"; g_pageRows.clear();
    struct R { int kind, id; const char* l; } rows[] = { { 1, 1, "Resolution" }, { 2, 2, "Draw Distance" }, { 1, 3, "Shadows" }, { 2, 4, "Brightness" }, { 1, 5, "Subtitles" }, { 1, 6, "Frame Limiter" } };
    for(auto& r : rows) { PRow p; p.kind = r.kind; p.item = NULL; p.setId = r.id; p.label = r.l; p.act = NULL; p.data = 0; p.lastVal = -999; g_pageRows.push_back(p); }
    ComputeLayout(); g_scroll = 0; g_sel = 3; g_capValid = false;
    Clear(); DrawSettingsPage(); Save(out + "/settings.ppm");
    // dialog
    g_capValid = true; g_style = PS_DIALOG; g_dlgMsg = "ARE YOU SURE YOU WANT TO QUIT? ALL UNSAVED PROGRESS WILL BE LOST."; g_pageRows.clear();
    for(const char* l : { "YES", "NO" }) { PRow p; p.kind = 0; p.item = NULL; p.setId = -1; p.label = l; p.act = NULL; p.data = 0; p.lastVal = 0; g_pageRows.push_back(p); }
    ComputeLayout(); g_sel = 1;
    Clear(); DrawDialogPage(); Save(out + "/dialog.ppm");
    // in-game HUD over the game frame
    g_kind = SK_NONE; g_liveMs = -1000000; g_capValid = false; g_style = PS_SETTINGS;
    Clear(); ComputeLayout(); DrawTex(world, 0, 0, CW, CH, 0, 0, 1, 1, COL_WHITE); VIHudDraw(); Save(out + "/hud.ppm");
    // the item wheel: hold the weapon box
    g_touchDown = 1; g_touchX = (int)(CW * 0.9f); g_touchY = (int)(CH * 0.06f);
    VIHudDraw(); g_holdMs -= 1000; VIHudDraw();
    g_touchX = (int)(CW * 0.5f + CH * 0.2f); g_touchY = (int)(CH * 0.5f - CH * 0.1f);
    Clear(); DrawTex(world, 0, 0, CW, CH, 0, 0, 1, 1, COL_WHITE); VIHudDraw(); VIHudDraw();
    Clear(); DrawTex(world, 0, 0, CW, CH, 0, 0, 1, 1, COL_WHITE); VIHudDraw(); Save(out + "/wheel.ppm");
    g_touchDown = 0; g_wheelOn = false;
    // loading screen
    g_lsSet[0] = MakeTex("pc/lds/mobilesc1.png"); g_lsSetN = 1; g_lsStartMs = NowMs() - 5000; g_lsLastMs.store(NowMs()); g_lsSwitchMs = NowMs();
    static float pct = 62; g_pLsPercent = &pct;
    Clear(); DrawPcScreen(1); Save(out + "/loading.ppm");
    return 0;
}
