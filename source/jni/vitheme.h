/* VIHud: the GTA VI look of the menus (main menu, pause menu, settings, dialogs, mods list).
 * Included by main.cpp in place of the PC drawing functions (same names, same menu model and input):
 *   - main menu: avatar + name top left, L1 [tabs] R1 top right (one tab per stock entry), the art panel with
 *     the logo, title and text of the selected tab, hint text and the Settings / action buttons at the bottom
 *   - pause menu: the paused game behind (captured frame, dimmed), the dark left column with the VI logo and the
 *     vertical list (selected = orange-pink tertiary button), name / day / time / money / portrait top right,
 *     a live map preview while MAP is selected
 *   - settings: black top strip with the pink line, SETTINGS header + categories at the left, rows in the middle
 *     ("<  value  >", selected row orange), DESCRIPTION panel at the right
 * Every texture is optional: without it the same layout is drawn with plain shapes. */

static bool RasterSize(void* ras, int& w, int& h); static void* TexRaster(void* tex);   // pcextra.h
static const Col VI_PINK   = { 255,  74, 158, 255 };
static const Col VI_ORANGE = { 255, 148,  62, 255 };
static const Col VI_PURPLE = { 122,  52, 170, 255 };
static const Col VI_WHITE  = { 255, 255, 255, 255 };
static const Col VI_GREY   = { 178, 178, 188, 255 };
static const Col VI_DARK   = {  10,  10,  14, 225 };

static FontAtlas g_vif;                        // Montserrat ExtraBold: tabs, buttons, titles (pc/fonts/vi.png/.txt)
static void* g_viLogo = NULL;                  // vi/logo.png       the "Grand Theft Auto VI" logo
static void* g_viLogoPause = NULL;             // vi/logo_pause.png the outlined VI
static void* g_viPattern = NULL;               // vi/pattern.png    the main menu background
static void* g_viSetBg = NULL;                 // vi/settings_bg.png
static void* g_viPortrait = NULL;              // vi/portrait.png   (optional) the player picture on the pause menu
static void* g_viArt[6] = { 0 };               // vi/art1..6.png    the main menu art, one per tab
static int   g_viArtN = 0;
static float g_viTab[16][4]; static int g_viTabN = 0;        // last drawn hit boxes (x0, y0, x1, y1)
static float g_viBtn[2][4];                                     // 0 = action (X), 1 = settings
static std::vector<std::string> g_viCats;                       // the Options tiles, shown at the left of the settings pages
enum { VI_HIT_ACTION = 100, VI_HIT_SETTINGS = 101 };

static Col Alpha(Col c, float a) { c.a = (uint8_t)std::max(0.0f, std::min(255.0f, c.a * a)); return c; }
static Col Mix(Col a, Col b, float t) { return Col{ (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t) }; }

/* ---- shapes (MobileMenu::DrawQuad takes 4 points in strip order: top left, top right, bottom left, bottom right) ---- */
static void Quad4(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Col c0, Col c1, Col c2, Col c3)
{
    V2 v[4] = { { x0, y0 }, { x1, y1 }, { x2, y2 }, { x3, y3 } };
    Col cc[4] = { c0, c1, c2, c3 };
    fnDrawQuad(v, cc);
}
static void GradH(float x0, float y0, float x1, float y1, Col l, Col r) { Quad4(x0, y0, x1, y0, x0, y1, x1, y1, l, r, l, r); }
static void GradV(float x0, float y0, float x1, float y1, Col t, Col b) { Quad4(x0, y0, x1, y0, x0, y1, x1, y1, t, t, b, b); }
static void Slant(float x0, float y0, float x1, float y1, float sk, Col l, Col r) { Quad4(x0 + sk, y0, x1 + sk, y0, x0, y1, x1, y1, l, r, l, r); }
static void VLine(float ax, float ay, float bx, float by, float w, Col c)
{
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy); if(l < 0.01f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    Quad4(ax + nx, ay + ny, bx + nx, by + ny, ax - nx, ay - ny, bx - nx, by - ny, c, c, c, c);
}
static void VDisc(float cx, float cy, float r, Col c, int n = 0)
{
    if(n <= 0) n = std::max(12, std::min(64, (int)(r * 0.8f)));
    for(int i = 0; i < n; i += 2)
    {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n, a2 = 6.2831853f * (i + 2) / n;
        Quad4(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, cx, cy, cx + cosf(a2) * r, cy + sinf(a2) * r, c, c, c, c);
    }
}
static void VRing(float cx, float cy, float r, float w, Col c, float a0 = 0, float a1 = 6.2831853f, int n = 0)
{
    if(n <= 0) n = std::max(16, std::min(96, (int)(r * 0.9f)));
    float ri = r - w * 0.5f, ro = r + w * 0.5f;
    for(int i = 0; i < n; ++i)
    {
        float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
        Quad4(cx + cosf(t0) * ro, cy + sinf(t0) * ro, cx + cosf(t1) * ro, cy + sinf(t1) * ro, cx + cosf(t0) * ri, cy + sinf(t0) * ri, cx + cosf(t1) * ri, cy + sinf(t1) * ri, c, c, c, c);
    }
}
static void Pill(float x0, float y0, float x1, float y1, Col l, Col r)            // rounded ends, left-to-right gradient
{
    float rad = (y1 - y0) * 0.5f;
    GradH(x0 + rad, y0, x1 - rad, y1, l, r);
    VDisc(x0 + rad, y0 + rad, rad, l); VDisc(x1 - rad, y0 + rad, rad, r);
}
static void PillOutline(float x0, float y0, float x1, float y1, float w, Col c)
{
    float rad = (y1 - y0) * 0.5f;
    FillRect(x0 + rad, y0, x1 - rad, y0 + w, c); FillRect(x0 + rad, y1 - w, x1 - rad, y1, c);
    VRing(x0 + rad, y0 + rad, rad - w * 0.5f, w, c, 1.5707963f, 4.712389f);
    VRing(x1 - rad, y0 + rad, rad - w * 0.5f, w, c, -1.5707963f, 1.5707963f);
}

/* ---- text: h = cap height in pixels, cy = the vertical middle of the capitals ---- */
static const FontAtlas& VF(int f) { return f == 2 ? (g_vif.tex ? g_vif : g_beck) : (f == 1 ? g_beck : g_bank); }
static float TxtW(int f, const std::string& s, float h) { const FontAtlas& a = VF(f); return a.Width(s, h / (float)a.capH, 0); }
static float Txt(int f, const std::string& s, float x, float cy, float h, Col c, int align = 0)   // align 0 left, 1 centre, 2 right
{
    const FontAtlas& a = VF(f); float sc = h / (float)a.capH, w = a.Width(s, sc, 0);
    if(align == 1) x -= w * 0.5f; else if(align == 2) x -= w;
    DrawText(a, s, x, cy + h * 0.5f, sc, 0, c);
    return w;
}
static int Wrap(int f, const std::string& s, float h, float maxW, std::vector<std::string>& out)
{
    out.clear(); std::string line, word;
    for(size_t i = 0; i <= s.size(); ++i)
    {
        char ch = i < s.size() ? s[i] : ' ';
        if(ch == ' ' || ch == '\n')
        {
            if(!word.empty())
            {
                std::string t = line.empty() ? word : line + " " + word;
                if(!line.empty() && TxtW(f, t, h) > maxW) { out.push_back(line); line = word; } else line = t;
                word.clear();
            }
            if(ch == '\n' && !line.empty()) { out.push_back(line); line.clear(); }
        }
        else word += ch;
    }
    if(!line.empty()) out.push_back(line);
    return (int)out.size();
}

/* ---- pad glyphs, drawn (no textures): X, O, triangle, square, L1 / R1 ---- */
static void PadGlyph(int g, float cx, float cy, float r, Col c)
{
    float w = std::max(1.5f, r * 0.16f);
    VRing(cx, cy, r, w, c);
    float k = r * 0.42f;
    if(g == 0) { VLine(cx - k, cy - k, cx + k, cy + k, w, c); VLine(cx - k, cy + k, cx + k, cy - k, w, c); }          // X
    else if(g == 1) VRing(cx, cy, r * 0.45f, w, c);                                                                      // O
    else if(g == 2) { float t = r * 0.5f; VLine(cx, cy - t, cx + t * 0.9f, cy + t * 0.6f, w, c); VLine(cx + t * 0.9f, cy + t * 0.6f, cx - t * 0.9f, cy + t * 0.6f, w, c); VLine(cx - t * 0.9f, cy + t * 0.6f, cx, cy - t, w, c); }
    else { float t = r * 0.38f; FillRect(cx - t, cy - t, cx + t, cy - t + w, c); FillRect(cx - t, cy + t - w, cx + t, cy + t, c); FillRect(cx - t, cy - t, cx - t + w, cy + t, c); FillRect(cx + t - w, cy - t, cx + t, cy + t, c); }
}
static float Bumper(const char* t, float x, float cy, float h, Col c, int align)        // "L1" / "R1" in an outlined pill
{
    float th = h * 0.5f, w = TxtW(2, t, th) + h * 0.9f;
    if(align == 2) x -= w;
    PillOutline(x, cy - h * 0.5f, x + w, cy + h * 0.5f, std::max(1.5f, h * 0.07f), c);
    Txt(2, t, x + w * 0.5f, cy, th, c, 1);
    return w;
}
/* "Label (X)" hints, right aligned at xr; returns the left edge */
static float Hint(const std::string& label, int glyph, float xr, float cy, float h, Col c)
{
    float r = h * 0.62f;
    PadGlyph(glyph, xr - r, cy, r, c);
    float w = Txt(0, label, xr - 2 * r - h * 0.55f, cy, h, c, 2);
    return xr - 2 * r - h * 0.55f - w;
}
static void HintsBackSelect(float y, bool back = true, bool select = true)
{
    float h = 0.019f * L.H, x = 0.965f * L.W;
    if(select) x = Hint(Tr("SELECT") == "SELECT" ? "Select" : Tr("SELECT"), 0, x, y, h, VI_WHITE) - 0.03f * L.W;
    if(back) Hint(Tr("BACK") == "BACK" ? "Back" : Tr("BACK"), 1, x, y, h, VI_WHITE);
}

/* ---- textures: drawn to cover a box (cropped, never stretched) ---- */
static void DrawCover(void* tex, float x0, float y0, float x1, float y1, Col c, float skewTop = 0)
{
    if(!tex) return;
    int tw = 0, th = 0; RasterSize(TexRaster(tex), tw, th);
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if(tw > 0 && th > 0)
    {
        float ta = (float)tw / th, ba = (x1 - x0) / std::max(1.0f, y1 - y0);
        if(ta > ba) { float k = ba / ta; u0 = (1 - k) * 0.5f; u1 = u0 + k; } else { float k = ta / ba; v0 = (1 - k) * 0.5f; v1 = v0 + k; }
    }
    V2 v[4] = { { x0, y0 }, { x1, y0 + skewTop }, { x0, y1 }, { x1, y1 } };
    V2 uv[4] = { { u0, v0 }, { u1, v0 }, { u0, v1 }, { u1, v1 } };
    Col cc[4] = { c, c, c, c };
    fnDrawSprite(tex, v, uv, cc);
}
static float DrawFit(void* tex, float x, float y, float h, Col c, int align = 0)    // whole picture, height h; returns the width
{
    if(!tex) return 0;
    int tw = 0, th = 0; RasterSize(TexRaster(tex), tw, th);
    float w = (tw > 0 && th > 0) ? h * tw / th : h;
    if(align == 1) x -= w * 0.5f; else if(align == 2) x -= w;
    DrawTex(tex, x, y, x + w, y + h, 0, 0, 1, 1, c);
    return w;
}
static void LogoFallback(float x, float y, float h, Col c)          // "VI" in big letters when vi/logo.png is missing
{
    Txt(2, "VI", x + h * 0.5f, y + h * 0.5f, h * 0.62f, c, 1);
}

/* ---- what each stock entry shows on the main menu ---- */
struct VIInfo { std::string tab, title, desc, action, hint; };
static VIInfo InfoOf(const Entry& e)
{
    VIInfo i; std::string k = e.key ? e.key : "";
    if(e.kind == K_MP) { i.tab = "ONLINE"; i.title = "GRAND THEFT AUTO ONLINE"; i.desc = "Play San Andreas with friends: join MP servers, host your own, and take on roleplay, races and gamemodes together."; i.action = "Go Online"; i.hint = "Opens the MP server list."; }
    else if(e.kind == K_MAP) { i.tab = "MAP"; i.title = "MAP"; i.desc = "The map of San Andreas with your position and blips."; i.action = "Open Map"; i.hint = "Look at the map of San Andreas."; }
    else if(e.kind == K_MODS) { i.tab = "MODS"; i.title = "MODS"; i.desc = "Every mod in the mods folder, and their settings."; i.action = "Open Mods"; i.hint = "The mods you have installed."; }
    else if(k == "FEP_RES") { i.tab = g_isPause ? "RESUME" : "STORY"; i.title = "STORY MODE"; i.desc = "Carry on with Carl Johnson's story where you left it: Los Santos, San Fierro and Las Venturas are waiting."; i.action = "Continue Story"; i.hint = "Continue Grand Theft Auto San Andreas' Story Mode."; }
    else if(k == "FEP_STG") { i.tab = g_isPause ? "NEW GAME" : "STORY"; i.title = "START STORY MODE"; i.desc = "Start a new story or load one of your saved games."; i.action = "Start Story"; i.hint = "Start or load Grand Theft Auto San Andreas' Story Mode."; }
    else if(k == "FEP_OPT") { i.tab = "SETTINGS"; i.title = "SETTINGS"; i.desc = "Display, audio, controls, language and game settings."; i.action = "Open Settings"; i.hint = "Change the game settings."; }
    else if(k == "FEP_QUI") { i.tab = "EXIT"; i.title = "QUIT GAME"; i.desc = "Close Grand Theft Auto San Andreas. Remember to save your progress first."; i.action = "Quit"; i.hint = "Leave the game."; }
    else if(k == "FEH_STA") { i.tab = "PROGRESS"; i.title = "PROGRESS"; i.desc = "Your stats and how far you are in the story."; i.action = "Open"; i.hint = "Your stats."; }
    else if(k == "FEH_BRI") { i.tab = "BRIEF"; i.title = "BRIEF"; i.desc = "The last mission messages and help texts."; i.action = "Open"; i.hint = "Mission messages."; }
    else { i.tab = e.label; i.title = e.label; i.desc = ""; i.action = "Select"; i.hint = ""; }
    if(UiLang() != LG_EN) { i.tab = e.label; }                    // other languages: the game's own words on the tabs
    return i;
}
static int EntryIndexOf(const char* key) { for(size_t i = 0; i < g_entries.size(); ++i) if(g_entries[i].key && !strcmp(g_entries[i].key, key)) return (int)i; return -1; }

static void VILayout()
{
    g_bank.xs = 1.0f; g_bank.bold = 0.0f; g_bank.spaceEx = 0.0f;
    g_beck.xs = 1.0f; g_beck.bold = 0.0f; g_beck.spaceEx = 0.0f;
    g_vif.xs = 1.0f; g_vif.bold = 0.0f; g_vif.spaceEx = 0.0f;
    L.firstY = 0.2725f * L.H; L.rowStep = 0.055f * L.H;          // Options tiles = the category column of the settings screen
    L.sTop = 0.245f * L.H; L.sStep = 0.055f * L.H;
    L.pListTop = 0.17f * L.H; L.pListBottom = 0.86f * L.H; L.pRowStep = 0.052f * L.H;
    L.pLabelX = 0.262f * L.W; L.pValueR = 0.645f * L.W; L.pBarW = 0.13f * L.W;
    L.pBackL = 0.70f * L.W; L.pBackR = L.W; L.pBackT = 0.90f * L.H; L.pBackB = L.H;
    L.listTop = 0.17f * L.H; L.listBottom = 0.86f * L.H; L.backY = 0.93f * L.H;
}

/* hit test of the root views (main / pause) against the boxes drawn last frame */
static int VIHit(float x, float y)
{
    for(int i = 0; i < g_viTabN; ++i) if(x >= g_viTab[i][0] && x <= g_viTab[i][2] && y >= g_viTab[i][1] && y <= g_viTab[i][3]) return i;
    for(int b = 0; b < 2; ++b) if(x >= g_viBtn[b][0] && x <= g_viBtn[b][2] && y >= g_viBtn[b][1] && y <= g_viBtn[b][3]) return b == 0 ? VI_HIT_ACTION : VI_HIT_SETTINGS;
    return -1;
}
static void VIActivate(int hit)
{
    if(hit == VI_HIT_ACTION) { ActivateMain(g_sel); return; }
    if(hit == VI_HIT_SETTINGS) { int i = EntryIndexOf("FEP_OPT"); if(i >= 0) { g_sel = i; ActivateMain(i); } return; }
    if(hit < 0 || hit >= (int)g_entries.size()) return;
    if(g_isPause || hit == g_viSelAtPress) ActivateMain(hit);     // pause list: a tap opens; main menu tabs: the first tap looks, the second opens
    else { g_sel = hit; SndPlay(g_sndSelect); }
}

static void DrawCursor(float, float, float) {}
static void DrawToast(const Theme&)
{
    if(g_toastFrames <= 0 || g_toast.empty()) return;
    float h = 0.018f * L.H, w = TxtW(0, g_toast, h) + 0.04f * L.W, cy = 0.885f * L.H;
    Pill(L.cx - w * 0.5f, cy - 0.026f * L.H, L.cx + w * 0.5f, cy + 0.026f * L.H, Col{ 0, 0, 0, 210 }, Col{ 0, 0, 0, 210 });
    Txt(0, g_toast, L.cx, cy, h, VI_WHITE, 1);
}
static void DrawScrollBar(float top, float bottom, float maxScroll, Col, Col)
{
    if(maxScroll <= 0) return;
    float trackH = bottom - top, thumbH = std::max(0.04f * L.H, trackH * trackH / (trackH + maxScroll));
    float ty = top + (trackH - thumbH) * (g_scroll / maxScroll), bx = 0.655f * L.W;
    FillRect(bx, top, bx + 0.003f * L.H, bottom, Col{ 255, 255, 255, 50 });
    GradV(bx, ty, bx + 0.003f * L.H, ty + thumbH, VI_ORANGE, VI_PINK);
}
static void DrawLoading()
{
    if(!TransparentBackdrop()) FillRect(0, 0, L.W, L.H, COL_BLACK);
    FillRect(0, 0, L.W, L.H, Col{ 0, 0, 0, 120 });
    float a = (float)(NowMs() % 1200) / 1200.0f * 6.2831853f, r = 0.035f * L.H;
    VRing(L.cx, L.H * 0.46f, r, 0.006f * L.H, Col{ 255, 255, 255, 40 });
    VRing(L.cx, L.H * 0.46f, r, 0.006f * L.H, VI_PINK, a, a + 2.0f);
    Txt(2, Tr("PLEASE WAIT"), L.cx, L.H * 0.56f, 0.022f * L.H, VI_WHITE, 1);
}

/* ---- top-right info of the pause menu (vihud.h reads the game) ---- */
struct VIGameInfo { bool ok; int money, hour, minute, day, wanted; float hp, armour; };
static VIGameInfo ReadGameInfo();
static void DrawMapPreview(float x0, float y0, float x1, float y1);   // vihud.h: the vector map of San Andreas around the player

static void DrawAvatar(float cx, float cy, float r, Col ring)
{
    VDisc(cx, cy, r, Col{ 54, 56, 66, 255 });
    if(g_viPortrait) DrawCover(g_viPortrait, cx - r * 0.86f, cy - r * 0.86f, cx + r * 0.86f, cy + r * 0.86f, VI_WHITE);
    else { VDisc(cx, cy - r * 0.22f, r * 0.30f, Col{ 210, 210, 220, 255 }); VRing(cx, cy + r * 0.62f, r * 0.42f, r * 0.30f, Col{ 210, 210, 220, 255 }, 3.4f, 6.0f); }
    VRing(cx, cy, r, std::max(2.0f, r * 0.12f), ring);
}

static void DrawTitleMenu()
{
    FillRect(0, 0, L.W, L.H, Col{ 8, 8, 10, 255 });
    if(g_viPattern) DrawCover(g_viPattern, 0, 0, L.W, L.H, Col{ 255, 255, 255, 255 });
    const float H = L.H, W = L.W;
    // avatar + name
    float ar = 0.032f * H, acx = 0.045f * W, acy = 0.075f * H;
    DrawAvatar(acx, acy, ar, Col{ 255, 196, 30, 255 });
    Txt(2, g_cfgName, acx + ar + 0.012f * W, acy, 0.021f * H, VI_WHITE);
    // tabs, right aligned: L1 ... R1
    float th = 0.022f * H, ty = 0.075f * H, x = 0.965f * W, bh = 0.045f * H, pad = 0.017f * W;
    x -= Bumper("R1", x, ty, 0.040f * H, VI_WHITE, 2) + pad;
    g_viTabN = 0;
    for(int i = (int)g_entries.size() - 1; i >= 0 && g_viTabN < 16; --i)
    {
        VIInfo in = InfoOf(g_entries[i]);
        std::string t = Upper(in.tab); float w = TxtW(2, t, th) + 2 * pad;
        bool sel = (i == g_sel);
        Col c = sel ? Col{ 10, 10, 12, 255 } : VI_WHITE;
        if(g_entries[i].kind == K_MP && !MpInstalled()) c = Alpha(c, 0.45f);
        if(sel) Slant(x - w, ty - bh * 0.5f, x, ty + bh * 0.5f, bh * 0.22f, VI_WHITE, VI_WHITE);
        Txt(2, t, x - w * 0.5f, ty, th, c, 1);
        g_viTab[i][0] = x - w; g_viTab[i][1] = ty - bh; g_viTab[i][2] = x; g_viTab[i][3] = ty + bh;
        x -= w + pad * 0.4f;
    }
    g_viTabN = (int)std::min<size_t>(g_entries.size(), 16);
    Bumper("L1", x - pad * 0.6f, ty, 0.040f * H, VI_WHITE, 2);
    // the art panel (top edge slightly tilted, like the reference)
    float px0 = 0.10f * W, px1 = 0.90f * W, py0 = 0.13f * H, py1 = 0.865f * H, sk = 0.025f * H;
    FillRect(px0, py0, px1, py1, Col{ 26, 22, 34, 255 });
    static int s_artPrev = -1, s_artCur = -1; static int64_t s_artMs = 0;
    int want = g_viArtN > 0 ? (g_sel < 0 ? 0 : g_sel) % g_viArtN : -1;
    if(want != s_artCur) { s_artPrev = s_artCur; s_artCur = want; s_artMs = s_artPrev < 0 ? NowMs() - 1000 : NowMs(); }
    float k = std::min(1.0f, (NowMs() - s_artMs) / 350.0f);
    if(s_artPrev >= 0 && k < 1) DrawCover(g_viArt[s_artPrev], px0, py0, px1, py1, VI_WHITE, sk);
    if(s_artCur >= 0) DrawCover(g_viArt[s_artCur], px0, py0, px1, py1, Col{ 255, 255, 255, (uint8_t)(255 * k) }, sk);
    else GradV(px0, py0, px1, py1, Col{ 255, 120, 90, 255 }, Col{ 120, 40, 140, 255 });
    GradV(px0, py0 + (py1 - py0) * 0.48f, px1, py1, Col{ 0, 0, 0, 0 }, Col{ 0, 0, 0, 225 });
    // logo, title and text of the selected tab
    VIInfo in = g_sel >= 0 && g_sel < (int)g_entries.size() ? InfoOf(g_entries[g_sel]) : VIInfo();
    float lh = 0.235f * H, lx = 0.06f * W, ly = 0.635f * H, lw = lh * 0.95f;
    if(g_viLogo) lw = DrawFit(g_viLogo, lx, ly, lh, VI_WHITE); else LogoFallback(lx, ly, lh, VI_WHITE);
    float tx = std::max(0.25f * W, lx + lw + 0.025f * W);
    std::string title = Upper(in.title.empty() ? std::string("GRAND THEFT AUTO VI") : in.title);
    Txt(2, Fit(VF(2), title, 0.040f * H / VF(2).capH, 0, 0.60f * W), tx, 0.715f * H, 0.040f * H, VI_WHITE);
    std::vector<std::string> lines; Wrap(0, in.desc, 0.0185f * H, 0.56f * W, lines);
    for(size_t i = 0; i < lines.size() && i < 3; ++i) Txt(0, lines[i], tx, 0.775f * H + i * 0.036f * H, 0.0185f * H, Col{ 235, 235, 240, 255 });
    // bottom line: hint left, Settings + action right
    float by = 0.935f * H, bhh = 0.019f * H;
    Txt(0, in.hint, 0.075f * W, by, 0.018f * H, VI_GREY);
    float xr = 0.965f * W;
    float l = Hint(in.action, 0, xr, by, bhh, VI_WHITE);
    g_viBtn[0][0] = l - 0.01f * W; g_viBtn[0][1] = by - 0.04f * H; g_viBtn[0][2] = xr + 0.01f * W; g_viBtn[0][3] = H;
    int iOpt = EntryIndexOf("FEP_OPT");
    if(iOpt >= 0 && iOpt != g_sel)
    {
        float r2 = l - 0.035f * W, gw = 0.024f * H;
        for(int j = 0; j < 3; ++j) FillRect(r2 - gw, by - gw * 0.42f + j * gw * 0.42f - 1, r2, by - gw * 0.42f + j * gw * 0.42f + 1.5f, VI_WHITE);   // the "menu" icon
        float l2 = Txt(0, "Settings", r2 - gw - 0.4f * bhh, by, bhh, VI_WHITE, 2);
        g_viBtn[1][0] = r2 - gw - 0.4f * bhh - l2 - 0.01f * W; g_viBtn[1][1] = by - 0.04f * H; g_viBtn[1][2] = r2 + 0.005f * W; g_viBtn[1][3] = H;
    }
    else { g_viBtn[1][0] = g_viBtn[1][1] = g_viBtn[1][2] = g_viBtn[1][3] = -1; }
}

static void DrawPauseMenu()
{
    const float H = L.H, W = L.W;
    if(!TransparentBackdrop()) { FillRect(0, 0, W, H, COL_BLACK); if(g_viSetBg) DrawCover(g_viSetBg, 0, 0, W, H, Col{ 120, 120, 120, 255 }); }
    FillRect(0, 0, W, H, Col{ 70, 66, 74, 110 });                  // grey veil: the paused game in the back
    GradH(0.21f * W, 0, 0.50f * W, H, Col{ 0, 0, 0, 150 }, Col{ 0, 0, 0, 0 });
    FillRect(0, 0, 0.21f * W, H, Col{ 6, 6, 9, 228 });
    // the outlined VI at the top of the column
    float lh = 0.20f * H;
    if(g_viLogoPause) DrawFit(g_viLogoPause, 0.105f * W, 0.09f * H, lh, VI_WHITE, 1);
    else if(g_viLogo) DrawFit(g_viLogo, 0.105f * W, 0.09f * H, lh, VI_WHITE, 1);
    else LogoFallback(0.105f * W - lh * 0.5f, 0.09f * H, lh, VI_WHITE);
    // the list
    float y = 0.40f * H, step = 0.064f * H, th = 0.022f * H, x0 = 0.022f * W, x1 = 0.188f * W;
    g_viTabN = 0;
    for(size_t i = 0; i < g_entries.size() && i < 16; ++i, y += step)
    {
        VIInfo in = InfoOf(g_entries[i]);
        bool sel = ((int)i == g_sel);
        if(sel) Pill(x0, y - step * 0.40f, x1, y + step * 0.40f, VI_ORANGE, VI_PINK);
        Txt(2, Upper(i == 0 && !strcmp(g_entries[i].key, "FEP_RES") ? std::string("RESUME") : (UiLang() == LG_EN ? in.tab : g_entries[i].label)), x0 + 0.016f * W, y, th, sel ? VI_WHITE : Col{ 225, 225, 230, 255 });
        g_viTab[i][0] = 0; g_viTab[i][1] = y - step * 0.5f; g_viTab[i][2] = 0.21f * W; g_viTab[i][3] = y + step * 0.5f;
        g_viTabN = (int)i + 1;
    }
    g_viBtn[0][0] = g_viBtn[0][1] = g_viBtn[0][2] = g_viBtn[0][3] = -1;
    g_viBtn[1][0] = g_viBtn[1][1] = g_viBtn[1][2] = g_viBtn[1][3] = -1;
    // name, day + time, money, portrait (top right)
    VIGameInfo gi = ReadGameInfo();
    float pr = 0.052f * H, pcx = 0.955f * W - pr, pcy = 0.085f * H;
    VDisc(pcx, pcy, pr + 0.006f * H, VI_PINK); DrawAvatar(pcx, pcy, pr, VI_ORANGE);
    float tr = pcx - pr - 0.018f * W;
    Txt(2, Upper(g_cfgName), tr, pcy - 0.028f * H, 0.021f * H, VI_WHITE, 2);
    static const char* days[7] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };
    char buf[64];
    if(gi.ok) snprintf(buf, sizeof(buf), "%s %02d:%02d", days[(gi.day % 7 + 7) % 7], gi.hour, gi.minute); else snprintf(buf, sizeof(buf), "SAN ANDREAS");
    Txt(0, buf, tr, pcy + 0.004f * H, 0.017f * H, VI_GREY, 2);
    if(gi.ok)
    {
        char m[32]; int v = gi.money < 0 ? -gi.money : gi.money; snprintf(m, sizeof(m), "%s$%d,%03d", gi.money < 0 ? "-" : "", v / 1000, v % 1000);
        if(v < 1000) snprintf(m, sizeof(m), "%s$%d", gi.money < 0 ? "-" : "", v);
        Txt(2, m, tr, pcy + 0.036f * H, 0.021f * H, Col{ 120, 230, 140, 255 }, 2);
    }
    // MAP selected: the live map preview
    if(g_sel >= 0 && g_sel < (int)g_entries.size() && g_entries[g_sel].kind == K_MAP && InGame())
    {
        float mx0 = 0.30f * W, my0 = 0.20f * H, mx1 = 0.955f * W, my1 = 0.86f * H;
        FillRect(mx0 - 3, my0 - 3, mx1 + 3, my1 + 3, Col{ 0, 0, 0, 160 });
        DrawMapPreview(mx0, my0, mx1, my1);
        GradH(mx0, my1 - 4, mx1, my1, VI_ORANGE, VI_PINK);
        Txt(0, "Select (X) to open the full map", mx0 + 0.012f * W, my1 - 0.03f * H, 0.017f * H, VI_WHITE);
    }
    HintsBackSelect(0.945f * H);
}

static void DrawMainView()
{
    if(g_isPause) DrawPauseMenu(); else DrawTitleMenu();
    DrawToast(TH_BLUE);
}

/* ---- the settings frame: background, top strip, left column, description panel ---- */
static std::string Lower(const std::string& s) { std::string r = s; for(char& c : r) if(c >= 'A' && c <= 'Z') c += 32; return r; }
static std::string Describe(const std::string& label)
{
    std::string n = Lower(label);
    struct D { const char* k; const char* t; };
    static const D d[] = {
        { "resolution", "How sharp the game is drawn. Lower values run faster on weaker phones." },
        { "draw distance", "How far away the world is drawn. Lower it for a smoother frame rate." },
        { "visual fx", "Quality of effects like smoke, fire and reflections." },
        { "shadow", "Real time shadows of people and cars. Turning them off saves a lot of power." },
        { "frame", "Limits the frame rate. 30 saves battery, higher feels smoother." },
        { "brightness", "Makes the whole picture lighter or darker." },
        { "subtitle", "Shows what characters say at the bottom of the screen." },
        { "radar", "How the radar and its blips are shown." },
        { "vibration", "The phone or controller vibrates on hits and crashes." },
        { "invert", "Turns the camera the other way when you look up and down." },
        { "steer", "How cars are steered: touch buttons, tilt or the stick." },
        { "sfx", "Volume of the sound effects: engines, guns, footsteps." },
        { "radio", "Volume of the radio stations in cars." },
        { "music", "Volume of the music." },
        { "language", "The language of menus, subtitles and help texts." },
        { "display", "Picture and HUD settings." }, { "audio", "Sound and radio settings." }, { "control", "Touch, tilt and controller settings." },
        { "game", "Gameplay settings and your save games." }, { "mods", "Settings of the installed mods." },
        { "load", "Load one of your saved games." }, { "save", "Save your progress in a slot." }, { "delete", "Delete a saved game." },
        { "lock-on", "Aim help: the camera turns to the nearest target." }, { "target", "Aim help and target markers." },
        { "hud", "Where the buttons and the HUD sit on the screen." }, { "camera", "How the camera follows you." },
    };
    for(const D& e : d) if(n.find(e.k) != std::string::npos) return e.t;
    return "Change " + label + ". Tap the row or use left / right to change the value.";
}
static void SettingsFrame(const std::string& page, int catSel, const std::string& desc)
{
    const float H = L.H, W = L.W;
    bool game = TransparentBackdrop();
    if(!game) FillRect(0, 0, W, H, Col{ 30, 16, 40, 255 });
    if(g_viSetBg) DrawCover(g_viSetBg, 0, 0, W, H, Col{ 255, 255, 255, (uint8_t)(game ? 150 : 255) });
    else GradV(0, 0, W, H, Col{ 255, 130, 70, (uint8_t)(game ? 120 : 255) }, Col{ 110, 40, 150, (uint8_t)(game ? 150 : 255) });
    FillRect(0, 0, W, H, Col{ 0, 0, 0, 70 });
    // top strip
    FillRect(0, 0, W, 0.075f * H, Col{ 0, 0, 0, 235 });
    GradH(0, 0.075f * H, W, 0.080f * H, VI_PINK, VI_ORANGE);
    if(g_viLogoPause) DrawFit(g_viLogoPause, 0.03f * W, 0.012f * H, 0.051f * H, VI_WHITE); else Txt(2, "VI", 0.03f * W, 0.0375f * H, 0.03f * H, VI_WHITE);
    float x = 0.965f * W, ty = 0.0375f * H;
    x -= Bumper("R1", x, ty, 0.036f * H, VI_WHITE, 2) + 0.014f * W;
    x -= Txt(2, "ONLINE", x, ty, 0.019f * H, Col{ 200, 200, 205, 255 }, 2) + 0.022f * W;
    x -= Txt(2, Upper(Tr("SETTINGS") == "SETTINGS" ? std::string("SETTINGS") : Tr("SETTINGS")), x, ty, 0.019f * H, VI_ORANGE, 2) + 0.014f * W;
    Bumper("L1", x, ty, 0.036f * H, VI_WHITE, 2);
    // left: SETTINGS header + categories
    float cx0 = 0.06f * W, cx1 = 0.235f * W;
    Pill(cx0, 0.17f * H - 0.024f * H, cx1, 0.17f * H + 0.024f * H, VI_ORANGE, VI_PINK);
    Txt(2, Upper(page.empty() ? std::string("SETTINGS") : page), cx0 + 0.014f * W, 0.17f * H, 0.02f * H, VI_WHITE);
    if(catSel != -2)
        for(size_t i = 0; i < g_viCats.size() && i < 12; ++i)
        {
            float cy = 0.2725f * H + 0.055f * H * i;
            bool cur = ((int)i == catSel);
            FillRect(cx0, cy - 0.023f * H, cx1, cy + 0.023f * H, Col{ 0, 0, 0, (uint8_t)(cur ? 190 : 130) });
            if(cur) FillRect(cx0, cy - 0.023f * H, cx0 + 0.004f * W, cy + 0.023f * H, VI_PINK);
            Txt(0, Fit(g_bank, g_viCats[i], 0.018f * H / g_bank.capH, 0, cx1 - cx0 - 0.03f * W), cx0 + 0.014f * W, cy, 0.018f * H, cur ? VI_PINK : VI_WHITE);
        }
    // right: DESCRIPTION
    float dx0 = 0.665f * W, dx1 = 0.935f * W, dy0 = 0.146f * H, dy1 = 0.62f * H;
    FillRect(dx0, dy0, dx1, dy1, Col{ 0, 0, 0, 175 });
    Txt(2, "DESCRIPTION", (dx0 + dx1) * 0.5f, dy0 + 0.035f * H, 0.018f * H, VI_WHITE, 1);
    FillRect(dx0 + 0.015f * W, dy0 + 0.065f * H, dx1 - 0.015f * W, dy0 + 0.065f * H + 1.5f, Col{ 255, 255, 255, 70 });
    std::vector<std::string> lines; Wrap(0, desc, 0.017f * H, dx1 - dx0 - 0.03f * W, lines);
    for(size_t i = 0; i < lines.size() && i < 10; ++i) Txt(0, lines[i], dx0 + 0.015f * W, dy0 + 0.10f * H + i * 0.034f * H, 0.017f * H, Col{ 230, 230, 235, 255 });
    HintsBackSelect(0.945f * H);
}
static int CatIndexOf(const std::string& title)
{
    std::string t = Lower(title);
    for(size_t i = 0; i < g_viCats.size(); ++i) if(Lower(g_viCats[i]) == t) return (int)i;
    return -1;
}

static void DrawBackButton(const Theme&) {}

static void DrawOptionsPage()
{
    bool start = (g_style == PS_TILES_LEFT);
    if(!start) { g_viCats.clear(); for(const OptRow& r : g_optRows) g_viCats.push_back(r.label); }
    std::string desc = g_sel >= 0 && g_sel < (int)g_optRows.size() ? Describe(g_optRows[g_sel].label) : "";
    SettingsFrame(start ? TitleOf("GAME") : OptionsTitle(), -2, desc);
    const float H = L.H, W = L.W;
    float cx0 = 0.06f * W, cx1 = start ? 0.40f * W : 0.235f * W;
    float top = PageWinTop(), bottom = PageWinBottom(), step = PageStep();
    if(g_optRows.empty()) Txt(0, Tr("NOTHING TO SHOW HERE"), cx0, top + step * 0.5f, 0.018f * H, VI_WHITE);
    for(size_t i = 0; i < g_optRows.size(); ++i)
    {
        float cy = PageRowCY((int)i);
        if(cy - step * 0.5f < top - 1 || cy + step * 0.5f > bottom + 1) continue;
        bool sel = ((int)i == g_sel);
        if(sel) GradH(cx0, cy - step * 0.42f, cx1, cy + step * 0.42f, VI_ORANGE, VI_PINK);
        else FillRect(cx0, cy - step * 0.42f, cx1, cy + step * 0.42f, Col{ 0, 0, 0, 140 });
        Txt(0, Fit(g_bank, g_optRows[i].label, 0.018f * H / g_bank.capH, 0, cx1 - cx0 - 0.03f * W), cx0 + 0.014f * W, cy, 0.018f * H, VI_WHITE);
    }
    DrawToast(TH_BLUE);
}

static void* PageBg(const std::string&) { return g_viSetBg; }
static void* SlotBg() { return g_viSetBg; }

static void DrawSettingsPage()
{
    bool slots = (g_style == PS_SLOTS);
    std::string title = slots ? TitleCase(g_slotTitle) : g_pageTitle;
    std::string desc = g_sel >= 0 && g_sel < (int)g_pageRows.size() ? Describe(g_pageRows[g_sel].label) : Describe(title);
    SettingsFrame("SETTINGS", CatIndexOf(title), desc);
    const float H = L.H, W = L.W;
    float rx0 = 0.247f * W, rx1 = 0.652f * W, th = 0.0175f * H;
    float top = PageWinTop(), bottom = PageWinBottom(), step = PageStep();
    Txt(2, Upper(title), rx0, 0.17f * H - 0.0f, 0.02f * H, VI_WHITE);
    if(g_pageRows.empty()) Txt(0, Tr("NOTHING TO SHOW HERE"), rx0 + 0.014f * W, top + step * 0.5f, th, VI_WHITE);
    for(size_t i = 0; i < g_pageRows.size(); ++i)
    {
        float cy = PageRowCY((int)i);
        if(cy - step * 0.5f < top - 1 || cy + step * 0.5f > bottom + 1) continue;
        if(cy - step * 0.5f < 0.20f * H) continue;                 // (the page title sits on the first slot)
        PRow& r = g_pageRows[i];
        bool sel = ((int)i == g_sel);
        if(sel) GradH(rx0, cy - step * 0.44f, rx1, cy + step * 0.44f, VI_ORANGE, Col{ 255, 110, 90, 255 });
        else FillRect(rx0, cy - step * 0.44f, rx1, cy + step * 0.44f, Col{ 0, 0, 0, 150 });
        Col c = VI_WHITE;
        if(!sel && r.kind == 3 && r.label.find("CLOUD") != std::string::npos) c = Col{ 140, 140, 150, 255 };
        Txt(0, Fit(g_bank, r.label, th / g_bank.capH, 0, (rx1 - rx0) * (r.kind == 1 || r.kind == 2 ? 0.52f : 0.92f)), rx0 + 0.014f * W, cy, th, c);
        float vr = L.pValueR;
        if(r.kind == 1)
        {
            SView v;
            if(ReadSetting(r.setId, v))
            {
                if(v.value != r.lastVal) { r.lastVal = v.value; r.valText = ToggleText(v); }
                std::string t = Fit(g_bank, r.valText, th / g_bank.capH, 0, 0.16f * W);
                float w = Txt(0, t, vr - 0.022f * W, cy, th, c, 2);
                Txt(2, ">", vr - 0.006f * W, cy, th, c, 2);
                Txt(2, "<", vr - 0.03f * W - w, cy, th, c, 2);
            }
        }
        else if(r.kind == 4)
        {
            if(g_cfgLang == r.data && (r.data != LG_AR || g_arOK)) Txt(0, Tr("ACTIVE"), vr - 0.006f * W, cy, th, c, 2);
        }
        else if(r.kind == 2)
        {
            SView v;
            if(ReadSetting(r.setId, v))
            {
                float range = (float)((long)v.vmax - (long)v.vmin);
                float f = range > 0 ? (float)(v.value - v.vmin) / range : 0.0f; f = f < 0 ? 0 : (f > 1 ? 1 : f);
                float bx1 = vr - 0.006f * W, bx0 = bx1 - L.pBarW, bh = 0.007f * H;
                FillRect(bx0, cy - bh * 0.5f, bx1, cy + bh * 0.5f, Col{ 255, 255, 255, 60 });
                if(sel) FillRect(bx0, cy - bh * 0.5f, bx0 + (bx1 - bx0) * f, cy + bh * 0.5f, VI_WHITE);
                else GradH(bx0, cy - bh * 0.5f, bx0 + (bx1 - bx0) * f, cy + bh * 0.5f, VI_ORANGE, VI_PINK);
                char pc[16]; snprintf(pc, sizeof(pc), "%d%%", (int)(f * 100.0f + 0.5f));
                Txt(0, pc, bx0 - 0.01f * W, cy, th, c, 2);
            }
        }
    }
    DrawScrollBar(std::max(top, 0.20f * H), bottom, PageMaxScroll(), VI_PINK, VI_GREY);
    DrawToast(TH_BLUE);
}

static void DrawDialogPage()
{
    const float H = L.H, W = L.W;
    if(!TransparentBackdrop()) FillRect(0, 0, W, H, COL_BLACK);
    FillRect(0, 0, W, H, Col{ 0, 0, 0, 140 });
    float top = g_dlgMsgTop - 0.07f * H, bottom = PageRowCY(std::max(0, (int)g_pageRows.size() - 1)) + 0.07f * H;
    float x0 = 0.16f * W, x1 = 0.84f * W;
    FillRect(x0, top, x1, bottom, Col{ 8, 8, 12, 238 });
    GradH(x0, top, x1, top + 0.006f * H, VI_PINK, VI_ORANGE);
    float th = 0.02f * H;
    for(size_t i = 0; i < g_dlgLines.size(); ++i) Txt(0, g_dlgLines[i], L.cx, g_dlgMsgTop + g_dlgLineH * ((float)i + 0.5f), th, VI_WHITE, 1);
    for(size_t i = 0; i < g_pageRows.size(); ++i)
    {
        float cy = PageRowCY((int)i);
        bool sel = ((int)i == g_sel);
        float w = std::max(0.16f * W, TxtW(2, g_pageRows[i].label, th) + 0.05f * W);
        if(sel) Pill(L.cx - w * 0.5f, cy - 0.026f * H, L.cx + w * 0.5f, cy + 0.026f * H, VI_ORANGE, VI_PINK);
        else PillOutline(L.cx - w * 0.5f, cy - 0.026f * H, L.cx + w * 0.5f, cy + 0.026f * H, 2.0f, Col{ 255, 255, 255, 120 });
        Txt(2, g_pageRows[i].label, L.cx, cy, th, VI_WHITE, 1);
    }
    DrawToast(TH_BLUE);
}

static void DrawModsView()
{
    int nFiles = 0; for(size_t i = 0; i < g_modRows.size(); ++i) if(g_modRows[i].kind == MR_FILE) ++nFiles;
    char hdr[96]; snprintf(hdr, sizeof(hdr), "%d %s", nFiles, Tr(nFiles == 1 ? "MOD FOUND" : "MODS FOUND").c_str());
    std::string desc = g_sel >= 0 && g_sel < (int)g_modRows.size() ? Upper(g_modRows[g_sel].file) : std::string(hdr);
    SettingsFrame("MODS", -2, desc);
    const float H = L.H, W = L.W;
    float rx0 = 0.06f * W, rx1 = 0.652f * W, th = 0.0175f * H, step = L.rowStep;
    if(g_modRows.empty()) Txt(0, Tr("NO MODS FOUND"), rx0 + 0.014f * W, L.listTop + step * 0.5f, th, VI_WHITE);
    for(size_t i = 0; i < g_modRows.size(); ++i)
    {
        float cy = L.listTop + step * ((float)i + 0.5f) - g_scroll;
        if(cy - step * 0.5f < L.listTop - 1 || cy + step * 0.5f > L.listBottom + 1) continue;
        if(cy - step * 0.5f < 0.20f * H) continue;
        bool sel = ((int)i == g_sel);
        if(sel) GradH(rx0, cy - step * 0.44f, rx1, cy + step * 0.44f, VI_ORANGE, VI_PINK); else FillRect(rx0, cy - step * 0.44f, rx1, cy + step * 0.44f, Col{ 0, 0, 0, 150 });
        Txt(0, Fit(g_bank, g_modRows[i].label, th / g_bank.capH, 0, rx1 - rx0 - 0.03f * W), rx0 + 0.014f * W, cy, th, VI_WHITE);
    }
    DrawScrollBar(std::max(L.listTop, 0.20f * H), L.listBottom, MaxScroll(), VI_PINK, VI_GREY);
    bool backSel = (g_sel == (int)g_modRows.size());
    float bw = 0.12f * W, cy = L.backY;
    if(backSel) Pill(L.cx - bw * 0.5f, cy - 0.024f * H, L.cx + bw * 0.5f, cy + 0.024f * H, VI_ORANGE, VI_PINK);
    Txt(2, Tr("BACK"), L.cx, cy, 0.019f * H, VI_WHITE, 1);
    DrawToast(TH_BLUE);
}

static void VILoadTextures()
{
    static bool done = false; if(done) return; done = true;
    if(!g_vif.Load("vi")) LOGI("pc/fonts/vi.png not found: tabs and buttons use the beck font");
    g_viLogo = LoadPNG("vi/logo.png"); g_viLogoPause = LoadPNG("vi/logo_pause.png");
    g_viPattern = LoadPNG("vi/pattern.png"); g_viSetBg = LoadPNG("vi/settings_bg.png"); g_viPortrait = LoadPNG("vi/portrait.png");
    g_viArtN = 0;
    for(int i = 1; i <= 6; ++i) { char p[32]; snprintf(p, sizeof(p), "vi/art%d.png", i); if(FileExists(g_dirPC + p)) if(void* t = LoadPNG(p)) g_viArt[g_viArtN++] = t; }
    LOGI("VI textures: logo %p pause logo %p pattern %p settings %p portrait %p, %d art picture(s)", g_viLogo, g_viLogoPause, g_viPattern, g_viSetBg, g_viPortrait, g_viArtN);
}
