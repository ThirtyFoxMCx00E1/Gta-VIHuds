#!/usr/bin/env python3
"""Builds the VIHud folder (files/TYFMC/VIHud/ on the phone) from scratch:

    pc/fonts/bank.*   Montserrat SemiBold        (body text)
    pc/fonts/beck.*   Roboto Condensed Bold      (titles)
    pc/fonts/vi.*     Montserrat ExtraBold       (tabs, buttons, HUD numbers)
    vi/logo.png       stacked "grand theft auto" + the gradient VI
    vi/logo_pause.png the outlined VI of the pause menu
    vi/pattern.png    the dark icon pattern behind the main menu
    vi/settings_bg.png sunset with palm trees (settings pages)
    vi/art1..6.png    the main menu art panels (one per tab)
    pc/lds/mobilesc0..5.png loading screen pictures
    select_menu.wav / back_menu.wav  menu sounds

Every picture is painted here with PIL (gradients, skylines, palms, sun, water): no game or trailer
image is used.  usage: gen_vihud_assets.py <out dir>   (default: assets/VIHud)
"""
import math, os, random, struct, sys, wave
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_fonts  # noqa: E402

MONT = '/usr/local/share/fonts/asi-document/Montserrat-%s.ttf'
ROBOTO_C = '/usr/share/fonts/truetype/roboto/unhinted/RobotoCondensed-Bold.ttf'

PINK = (255, 74, 158); ORANGE = (255, 148, 62); PURPLE = (122, 52, 170)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vgrad(w, h, stops):
    """vertical gradient through (t, rgb) stops"""
    col = Image.new('RGB', (1, h))
    px = col.load()
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                k = (t - stops[i][0]) / max(1e-6, stops[i + 1][0] - stops[i][0])
                px[0, y] = lerp(stops[i][1], stops[i + 1][1], k)
                break
    return col.resize((w, h))


def palm(d, x, base, h, lean, col, rnd):
    """curved trunk + tapered drooping fronds"""
    pts = []
    for i in range(21):
        t = i / 20
        pts.append((x + lean * t * t * h, base - t * h))
    for i in range(20):
        w = max(3, h / 38 * (1 - i / 30))
        d.line([pts[i], pts[i + 1]], fill=col, width=int(w))
    tx, ty = pts[-1]
    for k in range(8):
        a = math.pi + (k + 0.5) / 8 * math.pi + rnd.uniform(-0.12, 0.12)   # spread over the upper half
        L = h * rnd.uniform(0.30, 0.42)
        droop = L * rnd.uniform(0.45, 0.8) * (1.0 - abs(math.sin(a)) * 0.5)
        left, right = [], []
        for i in range(17):
            t = i / 16
            cx = tx + math.cos(a) * L * t
            cy = ty + math.sin(a) * L * t * 0.75 + droop * t * t
            dx = math.cos(a) * L + 1e-3; dy = math.sin(a) * L * 0.75 + 2 * droop * t
            n = math.hypot(dx, dy); nx, ny = -dy / n, dx / n
            w = L * 0.09 * math.sin(math.pi * min(1, t * 1.15)) + 1
            left.append((cx + nx * w, cy + ny * w)); right.append((cx - nx * w, cy - ny * w))
        d.polygon(left + right[::-1], fill=col)
    d.ellipse([tx - h / 60, ty - h / 60, tx + h / 60, ty + h / 60], fill=col)


def skyline(d, w, base, rnd, col, lit, density=1.0, hmax=0.45, H=512):
    x = -10
    while x < w:
        bw = rnd.randint(int(18 * density), int(55 * density))
        bh = rnd.randint(int(H * 0.06), int(H * hmax))
        d.rectangle([x, base - bh, x + bw, base], fill=col)
        if rnd.random() < 0.25:   # spire
            d.rectangle([x + bw // 2 - 1, base - bh - rnd.randint(10, 40), x + bw // 2 + 1, base - bh], fill=col)
        for wy in range(base - bh + 6, base - 4, 8):
            for wx in range(x + 4, x + bw - 4, 7):
                if rnd.random() < 0.28:
                    d.rectangle([wx, wy, wx + 2, wy + 3], fill=lit)
        x += bw + rnd.randint(0, 6)


def scene(w, h, seed, mood):
    rnd = random.Random(seed)
    skies = {
        'sunset': [(0, (60, 26, 92)), (0.35, (196, 70, 140)), (0.62, (255, 142, 88)), (0.75, (255, 196, 120)), (1, (255, 170, 110))],
        'dusk':   [(0, (24, 18, 60)), (0.4, (104, 46, 140)), (0.68, (236, 92, 140)), (1, (255, 150, 110))],
        'night':  [(0, (8, 8, 30)), (0.5, (40, 20, 80)), (0.75, (150, 40, 130)), (1, (60, 30, 90))],
        'day':    [(0, (70, 140, 220)), (0.55, (160, 200, 240)), (0.75, (255, 200, 170)), (1, (250, 170, 150))],
        'purple': [(0, (30, 14, 50)), (0.45, (130, 50, 150)), (0.7, (240, 110, 150)), (1, (255, 170, 120))],
    }
    img = vgrad(w, h, skies[mood])
    d = ImageDraw.Draw(img)
    hor = int(h * rnd.uniform(0.6, 0.68))
    # sun
    if mood in ('sunset', 'dusk', 'purple', 'day'):
        sx, sr = int(w * rnd.uniform(0.25, 0.75)), int(h * rnd.uniform(0.09, 0.14))
        sun = Image.new('L', (w, h), 0)
        ImageDraw.Draw(sun).ellipse([sx - sr * 2.6, hor - sr * 2.6 - sr * 0.6, sx + sr * 2.6, hor + sr * 2.6 - sr * 0.6], fill=70)
        sun = sun.filter(ImageFilter.GaussianBlur(sr))
        img = Image.composite(Image.new('RGB', (w, h), (255, 220, 160)), img, sun)
        d = ImageDraw.Draw(img)
        for i in range(sr * 2):
            y = hor - sr * 0.6 - sr + i
            if i > sr * 1.05 and (i // 5) % 2 == 0:   # retro stripes in the lower half
                continue
            hw = math.sqrt(max(0, sr * sr - (i - sr) ** 2))
            d.line([(sx - hw, y), (sx + hw, y)], fill=lerp((255, 236, 150), (255, 110, 120), i / (2 * sr)))
    else:
        for _ in range(140):
            x, y = rnd.randint(0, w), rnd.randint(0, int(hor * 0.7))
            d.point((x, y), fill=(255, 255, 255))
    # clouds
    for _ in range(rnd.randint(3, 7)):
        cl = Image.new('L', (w, h), 0)
        cx, cy = rnd.randint(0, w), rnd.randint(int(h * 0.08), int(hor * 0.75))
        ImageDraw.Draw(cl).ellipse([cx - rnd.randint(80, 220), cy - 12, cx + rnd.randint(80, 220), cy + 14], fill=rnd.randint(40, 90))
        cl = cl.filter(ImageFilter.GaussianBlur(10))
        img = Image.composite(Image.new('RGB', (w, h), (255, 190, 200) if mood != 'night' else (110, 60, 140)), img, cl)
    d = ImageDraw.Draw(img)
    # skyline (far, near)
    far = lerp(skies[mood][1][1], (20, 10, 40), 0.55)
    skyline(d, w, hor, rnd, far, lerp(far, (255, 220, 160), 0.35), 0.8, 0.28, h)
    near = (22, 12, 36) if mood != 'day' else (60, 50, 90)
    skyline(d, w, hor, rnd, near, (255, 200, 120) if mood != 'day' else (200, 210, 230), 1.1, 0.42, h)
    # water with reflection
    ref = img.crop((0, hor - (h - hor), w, hor)).transpose(Image.FLIP_TOP_BOTTOM).filter(ImageFilter.GaussianBlur(3))
    water = vgrad(w, h - hor, [(0, (40, 30, 80)), (1, (12, 10, 30))])
    img.paste(Image.blend(ref, water, 0.55), (0, hor))
    d = ImageDraw.Draw(img)
    for _ in range(260):
        y = rnd.randint(hor + 2, h)
        x = rnd.randint(0, w)
        d.line([(x, y), (x + rnd.randint(8, 40), y)], fill=lerp((255, 170, 150), (60, 40, 90), (y - hor) / (h - hor)))
    # palms in the front
    for _ in range(rnd.randint(2, 4)):
        side = rnd.choice([0, 1])
        x = rnd.randint(-20, int(w * 0.18)) if side == 0 else rnd.randint(int(w * 0.82), w + 20)
        palm(d, x, h + 10, rnd.randint(int(h * 0.55), int(h * 0.95)), rnd.uniform(-0.25, 0.25) * (1 if side == 0 else -1) + (0.15 if side == 0 else -0.15), (10, 6, 18), rnd)
    # boat / heli details
    if rnd.random() < 0.7:
        bx, by = rnd.randint(int(w * 0.3), int(w * 0.7)), hor + int((h - hor) * 0.45)
        d.polygon([(bx - 40, by), (bx + 46, by), (bx + 30, by + 12), (bx - 34, by + 12)], fill=(240, 240, 250))
        d.rectangle([bx - 14, by - 10, bx + 16, by], fill=(220, 220, 235))
        d.line([(bx - 60, by + 13), (bx - 160, by + 18)], fill=(255, 255, 255), width=2)
    if mood in ('night', 'dusk', 'purple') and rnd.random() < 0.8:
        hx, hy = rnd.randint(int(w * 0.5), int(w * 0.85)), rnd.randint(int(h * 0.15), int(h * 0.35))
        d.ellipse([hx - 10, hy - 5, hx + 10, hy + 5], fill=(12, 8, 20)); d.line([(hx - 22, hy - 8), (hx + 22, hy - 8)], fill=(12, 8, 20), width=2)
        d.line([(hx + 8, hy), (hx + 24, hy - 2)], fill=(12, 8, 20), width=2); d.ellipse([hx - 3, hy + 3, hx + 3, hy + 9], fill=(255, 240, 200))
    # warm grade
    grade = Image.new('RGB', (w, h), (255, 80, 160))
    return Image.blend(img, grade, 0.08)


def gradient_text_img(text, font, stops, stroke=0, stroke_col=(0, 0, 0)):
    bb = font.getbbox(text, stroke_width=stroke)
    w, h = bb[2] - bb[0] + 8, bb[3] - bb[1] + 8
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((4 - bb[0], 4 - bb[1]), text, font=font, fill=255)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    if stroke:
        sm = Image.new('L', (w, h), 0)
        ImageDraw.Draw(sm).text((4 - bb[0], 4 - bb[1]), text, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
        out.paste(Image.new('RGBA', (w, h), stroke_col + (255,)), (0, 0), sm)
    g = vgrad(w, h, stops).convert('RGBA')
    out.paste(g, (0, 0), mask)
    return out


def make_logo(path):
    f = ImageFont.truetype(MONT % 'Black', 190)
    fv = ImageFont.truetype(MONT % 'Black', 400)
    ws = [gradient_text_img(wd, f, [(0, (255, 255, 255)), (1, (232, 232, 242))], stroke=10, stroke_col=(30, 12, 40)) for wd in ('grand', 'theft', 'auto')]
    v = gradient_text_img('VI', fv, [(0, (255, 214, 120)), (0.45, ORANGE), (0.7, PINK), (1, PURPLE)], stroke=14, stroke_col=(30, 12, 40))
    step = int(ws[0].height * 0.78)
    autoY = 10 + 2 * step
    vx = 16 + int(ws[2].width * 0.93)
    vy = autoY + ws[2].height - v.height + 26
    W = max(16 + ws[0].width, 16 + ws[1].width, vx + v.width) + 20
    H = max(autoY + ws[2].height, vy + v.height) + 20
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for i, t in enumerate(ws):
        glow = Image.new('RGBA', t.size, PINK + (0,)); glow.putalpha(t.split()[3].filter(ImageFilter.GaussianBlur(6)))
        img.alpha_composite(glow, (16, 10 + i * step)); img.alpha_composite(t, (16, 10 + i * step))
    img.alpha_composite(v, (vx, max(0, vy)))
    img = img.crop(img.getbbox())
    img.thumbnail((640, 640), Image.LANCZOS)
    img.save(path)


def make_logo_pause(path):
    f = ImageFont.truetype(MONT % 'Black', 360)
    bb = f.getbbox('VI', stroke_width=8)
    w, h = bb[2] - bb[0] + 20, bb[3] - bb[1] + 20
    outer = Image.new('L', (w, h), 0); ImageDraw.Draw(outer).text((10 - bb[0], 10 - bb[1]), 'VI', font=f, fill=255, stroke_width=8, stroke_fill=255)
    inner = Image.new('L', (w, h), 0); ImageDraw.Draw(inner).text((10 - bb[0], 10 - bb[1]), 'VI', font=f, fill=255)
    inner = inner.filter(ImageFilter.MinFilter(9))
    ring = ImageChops.subtract(outer, inner)
    img = Image.new('RGBA', (w, h), (255, 255, 255, 0)); img.putalpha(ring)
    img.thumbnail((360, 360), Image.LANCZOS)
    img.save(path)


def make_pattern(path, w=1920, h=1080):
    img = Image.new('RGB', (w, h), (6, 6, 8))
    d = ImageDraw.Draw(img)
    c = (19, 19, 23)
    f = ImageFont.truetype(MONT % 'Black', 26)
    rnd = random.Random(7)
    step = 100
    for gy in range(0, h + step, step):
        for gx in range(0, w + step, step):
            x, y = gx + (step // 2 if (gy // step) % 2 else 0), gy
            k = rnd.randint(0, 4)
            if k == 0:   # palm leaf
                for a in range(-3, 4):
                    ang = -math.pi / 2 + a * 0.35
                    d.line([(x, y + 22), (x + math.cos(ang) * 28, y + 22 + math.sin(ang) * 30)], fill=c, width=5)
            elif k == 1:   # pistol silhouette
                d.rectangle([x - 26, y - 8, x + 22, y + 2], fill=c); d.rectangle([x + 6, y, x + 18, y + 22], fill=c)
            elif k == 2:   # arrow sign
                d.rectangle([x - 18, y - 18, x + 18, y + 18], outline=c, width=4)
                d.line([(x - 8, y + 8), (x - 8, y - 4), (x + 10, y - 4)], fill=c, width=4); d.polygon([(x + 10, y - 12), (x + 18, y - 4), (x + 10, y + 4)], fill=c)
            elif k == 3:
                d.text((x - 10, y - 16), '$', font=f, fill=c)
            else:   # star
                pts = []
                for i in range(10):
                    r = 18 if i % 2 == 0 else 8
                    a = -math.pi / 2 + i * math.pi / 5
                    pts.append((x + math.cos(a) * r, y + math.sin(a) * r))
                d.polygon(pts, fill=c)
    img.save(path, optimize=True)


def save_jpgish_png(img, path, colors=192):
    img.convert('RGB').quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG).convert('RGB').save(path, optimize=True)   # (RGB: the game's PNG reader wants no palette)


def make_wav(path, freqs, ms=70, vol=0.35):
    sr = 22050
    n = int(sr * ms / 1000)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        frames = bytearray()
        for i in range(n):
            t = i / sr
            f = freqs[0] + (freqs[1] - freqs[0]) * i / n
            env = min(1, i / (sr * 0.004)) * math.exp(-t * 38)
            frames += struct.pack('<h', int(32767 * vol * env * math.sin(2 * math.pi * f * t)))
        w.writeframes(bytes(frames))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'assets', 'VIHud')
    out = os.path.abspath(out)
    fd = os.path.join(out, 'pc', 'fonts'); vi = os.path.join(out, 'vi'); lds = os.path.join(out, 'pc', 'lds')
    for p in (fd, vi, lds):
        os.makedirs(p, exist_ok=True)
    gen_fonts.build(MONT % 'SemiBold', os.path.join(fd, 'bank.png'), os.path.join(fd, 'bank.txt'))
    gen_fonts.build(ROBOTO_C, os.path.join(fd, 'beck.png'), os.path.join(fd, 'beck.txt'))
    gen_fonts.build(MONT % 'ExtraBold', os.path.join(fd, 'vi.png'), os.path.join(fd, 'vi.txt'))
    make_logo(os.path.join(vi, 'logo.png'))
    make_logo_pause(os.path.join(vi, 'logo_pause.png'))
    make_pattern(os.path.join(vi, 'pattern.png'))
    moods = ['sunset', 'night', 'dusk', 'day', 'purple', 'sunset']
    for i, m in enumerate(moods):
        save_jpgish_png(scene(1024, 512, 100 + i, m), os.path.join(vi, 'art%d.png' % (i + 1)))
        print('art', i + 1, m)
    save_jpgish_png(scene(1024, 512, 42, 'purple'), os.path.join(vi, 'settings_bg.png'))
    for i, m in enumerate(['sunset', 'dusk', 'night', 'purple', 'day', 'dusk']):
        save_jpgish_png(scene(1024, 512, 300 + i, m), os.path.join(lds, 'mobilesc%d.png' % i))
    make_wav(os.path.join(out, 'select_menu.wav'), (1250, 1500))
    make_wav(os.path.join(out, 'back_menu.wav'), (900, 650))
    print('done:', out)


if __name__ == '__main__':
    main()
