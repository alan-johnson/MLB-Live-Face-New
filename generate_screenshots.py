#!/usr/bin/env python3
"""
Generate Pebble App Store screenshots for MLB Live Face.
Simulates the watch face at native resolution for all 6 target platforms.
Uses real live game data fetched from the MLB Stats API.
"""

from PIL import Image, ImageDraw, ImageFont
import os, math

BASE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(BASE, 'screenshots')
os.makedirs(OUT, exist_ok=True)

# ── Fonts ──────────────────────────────────────────────────────────────────
F_MLB_TTF      = os.path.join(BASE, 'resources/fonts/MLB_FONT.ttf')
F_CAPITAL_TTF  = os.path.join(BASE, 'resources/fonts/CAPITAL_FONT.ttf')
F_PHILLIES_TTF = os.path.join(BASE, 'resources/fonts/PHILLIES_FONT.ttf')

def load_font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()

# ── Live game data (ATH @ PHI, 8th Bottom, PHI winning 5–3) ─────────────
GAME = {
    'away':       'ATH',
    'home':       'PHI',
    'away_score': 3,
    'home_score': 5,
    'inning':     8,
    'half':       'Bottom',   # Top / Bottom
    'balls':      1,
    'strikes':    0,
    'outs':       1,
    'first':      1,
    'second':     1,
    'third':      0,
    'status':     2,          # 2 = Live
    'watch_time': '7:45',
}

# ── Colors ─────────────────────────────────────────────────────────────────
BG    = (0,   0,   0)
WHITE = (255, 255, 255)
GRAY  = (180, 180, 180)


def _int(x): return int(x)   # mimic C integer division truncation


def draw_bases(draw, w, h, first, second, third, is_round):
    """Draw the diamond base indicators exactly as the C code does."""
    mid_h = h // 2
    mid_w = w // 2
    col = WHITE

    if is_round:
        # PBL_ROUND bases use the same formula as rect
        pass  # falls through to rect logic below

    first_pts  = [(w - 20, mid_h), (w, mid_h + 20), (w, mid_h - 20)]
    second_pts = [(mid_w,  20), (mid_w - 20, 0), (mid_w + 20, 0)]
    third_pts  = [(20, mid_h), (0, mid_h + 20), (0, mid_h - 20)]

    for pts, occupied in [(first_pts, first), (second_pts, second), (third_pts, third)]:
        if occupied:
            draw.polygon(pts, fill=col)
        else:
            draw.polygon(pts, outline=col)


def draw_outs(draw, w, h, outs, half, is_round):
    """Draw the out indicators exactly as the C code does."""
    col = WHITE

    if is_round:
        # PBL_ROUND: circles near bottom center
        cx = w // 2
        cy = h - 15
        r  = 6
        if outs == 2:
            draw.ellipse([cx + 11 - r, cy - r, cx + 11 + r, cy + r], fill=col)
            draw.ellipse([cx - 11 - r, cy - r, cx - 11 + r, cy + r], fill=col)
        elif outs > 0:
            cy_s = h - 14
            draw.ellipse([cx - r, cy_s - r, cx + r, cy_s + r], fill=col)
            if half in ('Middle', 'End'):
                draw.ellipse([cx - 21 - r, cy - r, cx - 21 + r, cy + r], fill=col)
                draw.ellipse([cx + 21 - r, cy - r, cx + 21 + r, cy + r], fill=col)
    else:
        # PBL_RECT (also covers emery and gabbro)
        bx = _int((w / 20) * 19)
        if outs == 2:
            y1 = _int(h / 4 * 3) + 3
            y2 = _int(h / 4 * 3) + 21
            r  = 6
            draw.ellipse([bx - r, y1 - r, bx + r, y1 + r], fill=col)
            draw.ellipse([bx - r, y2 - r, bx + r, y2 + r], fill=col)
        elif outs > 0:
            y = _int(h / 4 * 3) + 12
            r = 6
            draw.ellipse([bx - r, y - r, bx + r, y + r], fill=col)
            if half in ('Middle', 'End'):
                y0 = _int(h / 4 * 3) - 7
                y3 = _int(h / 4 * 3) + 31
                draw.ellipse([bx - r, y0 - r, bx + r, y0 + r], fill=col)
                draw.ellipse([bx - r, y3 - r, bx + r, y3 + r], fill=col)


def draw_inning_arrow(draw, w, h, half, is_round, is_emery):
    """Draw the inning half arrow triangle (up = Top, down = Bottom)."""
    col  = WHITE
    is_bottom = half in ('Bottom', 'End')

    if is_round:
        ax = _int(w / 3) * 2 - 5
        if is_bottom:
            pts = [(ax, _int(h/10)*6+33), (ax-7, _int(h/10)*6+21), (ax+7, _int(h/10)*6+21)]
        else:
            pts = [(ax, _int(h/10)*6+21), (ax-7, _int(h/10)*6+33), (ax+7, _int(h/10)*6+33)]
    elif is_emery:
        ax = _int(w / 3) * 2 + 12
        if is_bottom:
            pts = [(ax, _int(h/10)*7+36), (ax-7, _int(h/10)*7+24), (ax+7, _int(h/10)*7+24)]
        else:
            pts = [(ax, _int(h/10)*7+24), (ax-7, _int(h/10)*7+36), (ax+7, _int(h/10)*7+36)]
    else:
        ax = _int(w / 3) * 2 - 5
        if is_bottom:
            pts = [(ax, _int(h/10)*7+34), (ax-7, _int(h/10)*7+22), (ax+7, _int(h/10)*7+22)]
        else:
            pts = [(ax, _int(h/10)*7+22), (ax-7, _int(h/10)*7+34), (ax+7, _int(h/10)*7+34)]

    draw.polygon(pts, fill=col)


def apply_circular_mask(img):
    """Crop a square image to a circle (for Chalk/Pebble Time Round)."""
    w, h = img.size
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, w - 1, h - 1], fill=255)
    result = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    result.paste(img.convert('RGBA'), mask=mask)
    # Composite onto a black circle background so the final PNG looks right
    bg = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    bg.paste(result, mask=result.split()[3])
    return bg.convert('RGB')


def generate(platform, w, h, logo_rel, is_round=False, is_emery=False,
             is_bw=False, is_gabbro=False):

    img  = Image.new('RGB', (w, h), BG)
    draw = ImageDraw.Draw(img)

    # ── 1. Team logo ─────────────────────────────────────────────────────
    logo_path = os.path.join(BASE, 'resources', logo_rel)
    if os.path.exists(logo_path):
        logo = Image.open(logo_path).convert('RGBA')
        lw, lh = logo.size

        if is_gabbro:
            lx, ly = 0, 0                          # GRect(0,0,260,163)
        elif is_emery:
            lx, ly = 0, -6                         # GRect(0,-6,200,128)
        elif is_round:
            lx, ly = 0, 0                          # GRect(0,0,w,113)
        else:
            lx, ly = -18, -6                       # GRect(-18,-6,w+18,119)

        # Create a temporary canvas the size of the image, paste logo onto it,
        # then composite onto the watch face (so negative offsets clip correctly)
        tmp = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        tmp.paste(logo, (lx, ly))
        # Use GCompOpSet: paste wherever logo pixel is not fully transparent
        img.paste(tmp.convert('RGB'), (0, 0), mask=tmp.split()[3])

    # ── 2. Fonts ─────────────────────────────────────────────────────────
    scale = w / 144  # scale relative to base 144px width
    sz_time     = max(20, int(40 * scale))
    sz_team     = max(10, int(20 * scale))
    sz_score    = max(10, int(22 * scale))
    sz_inning   = max(10, int(22 * scale))

    f_time    = load_font(F_MLB_TTF,      sz_time)
    f_team    = load_font(F_CAPITAL_TTF,  sz_team)
    f_score   = load_font(F_PHILLIES_TTF, sz_score)
    f_inning  = load_font(F_PHILLIES_TTF, sz_inning)

    # ── 3. Time (center of watch) ─────────────────────────────────────────
    time_y = _int(h / 10) * 4 - 6
    draw.text((w // 2, time_y), GAME['watch_time'],
              fill=WHITE, font=f_time, anchor='mt')

    # ── 4. Team names & scores ────────────────────────────────────────────
    if is_emery:
        away_tx = _int(w / 15)
        away_ty = _int(h / 10) * 7 + 2
        home_tx = _int(w / 15)
        home_ty = _int(h / 10) * 8 + 14
        data_x  = _int(w / 15) * 2 + 30
        data_ay = away_ty
        data_hy = home_ty
    elif is_round:
        away_tx = _int(w / 5)
        away_ty = _int(h / 10) * 6 + 5
        home_tx = _int(w / 5)
        home_ty = _int(h / 10) * 7 + 9
        data_x  = _int(w / 5) * 2 + 5
        data_ay = away_ty
        data_hy = home_ty
    else:
        away_tx = _int(w / 15)
        away_ty = _int(h / 10) * 7 + 2
        home_tx = _int(w / 15)
        home_ty = _int(h / 10) * 8 + 10
        data_x  = _int(w / 15) * 2 + 30
        data_ay = away_ty
        data_hy = home_ty

    draw.text((away_tx, away_ty), GAME['away'], fill=WHITE, font=f_team)
    draw.text((home_tx, home_ty), GAME['home'], fill=WHITE, font=f_team)
    draw.text((data_x,  data_ay), str(GAME['away_score']), fill=WHITE, font=f_score)
    draw.text((data_x,  data_hy), str(GAME['home_score']), fill=WHITE, font=f_score)

    # ── 5. Inning number ──────────────────────────────────────────────────
    if is_emery:
        inn_x = _int(w / 4) * 3 + 6
        inn_y = _int(h / 10) * 7 + 14
        inn_text = ' ' + str(GAME['inning'])
    elif is_round:
        inn_x = _int(w / 5) * 3
        inn_y = _int(h / 10) * 6 + 15
        inn_text = '      ' + str(GAME['inning'])
    else:
        inn_x = _int(w / 5) * 3
        inn_y = _int(h / 10) * 7 + 14
        inn_text = '      ' + str(GAME['inning'])

    draw.text((inn_x, inn_y), inn_text, fill=WHITE, font=f_inning)

    # ── 6. Inning arrow (half indicator) ─────────────────────────────────
    draw_inning_arrow(draw, w, h, GAME['half'], is_round, is_emery)

    # ── 7. Base runners ───────────────────────────────────────────────────
    draw_bases(draw, w, h, GAME['first'], GAME['second'], GAME['third'], is_round)

    # ── 8. Out indicators ─────────────────────────────────────────────────
    draw_outs(draw, w, h, GAME['outs'], GAME['half'], is_round)

    # ── 9. B&W conversion (aplite / diorite) ─────────────────────────────
    if is_bw:
        img = img.convert('L').point(lambda p: 255 if p > 128 else 0, '1')
        img = img.convert('RGB')

    # ── 10. Circular mask (chalk) ─────────────────────────────────────────
    if is_round:
        img = apply_circular_mask(img)

    out_path = os.path.join(OUT, f'{platform}.png')
    img.save(out_path)
    print(f'  saved {out_path}  ({w}×{h})')
    return out_path


# ── Platform definitions ──────────────────────────────────────────────────
PLATFORMS = [
    # (name,    w,   h,    logo_rel,                  round, emery, bw,   gabbro)
    ('aplite',  144, 168, 'images/phi.png',            False, False, True,  False),
    ('basalt',  144, 168, 'images/phi.png',            False, False, False, False),
    ('chalk',   180, 180, 'images/phi.png',            True,  False, False, False),
    ('diorite', 144, 168, 'images/phi.png',            False, False, True,  False),
    ('emery',   200, 228, 'images/emery/phi.png',      False, True,  False, False),
    ('gabbro',  144, 168, 'images/gabbro/phi.png',     False, False, False, True),
]

if __name__ == '__main__':
    print(f'Generating screenshots for: {GAME["away"]} @ {GAME["home"]}, '
          f'Inn {GAME["inning"]} {GAME["half"]}, '
          f'Score {GAME["away_score"]}–{GAME["home_score"]}, '
          f'Outs {GAME["outs"]}, Bases {GAME["first"]}{GAME["second"]}{GAME["third"]}')
    print()
    for (name, w, h, logo, is_round, is_emery, is_bw, is_gabbro) in PLATFORMS:
        generate(name, w, h, logo,
                 is_round=is_round, is_emery=is_emery,
                 is_bw=is_bw, is_gabbro=is_gabbro)
    print('\nDone.')
