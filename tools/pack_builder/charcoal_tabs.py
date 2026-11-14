"""Charcoal tabs: the stock parchment tab atlases recoloured to dark charcoal parchment with light icons (owner recipe,
2026-11-13). Pixel-level recolouring of the stock texture, not regeneration: same size, alpha untouched (the caller keeps the
stock alpha blocks byte for byte), every stroke, tear, stain and shadow where it was.

Per pixel, in CIELAB (sRGB D65):
  * the local paper is the light paper level around the pixel (70th percentile over 61 px, taken at half resolution);
  * ink = darker than the local paper (2-D, along its row and along its column) AND at least as colourful as the texture's
    paper, or darker than the local paper AND clearly more colourful (dense line work) (brown/red-brown line work); a shadow or a torn edge is
    darker but greyer (lower chroma) and stays paper. The ink weight is soft (antialiasing kept; unclear pixels stay paper);
  * the paper's lightness (the ink pixels use the paper around them) runs through a tone curve onto the charcoal palette
    (deep black - dark charcoal - lighter charcoal), so grain, crumples, stains and the selected tab's lighter paper stay;
  * the ink's strength (how much darker than its paper, times a selected/inactive factor from the light-paper level around
    it: the stock lifts the selected tab's paper) runs through the icon palette (faded - inactive - secondary - primary),
    blended over the paper by the ink weight.
`glyph` mode (a stand-alone arrow on transparency): every pixel is ink at full strength (the alpha carries on/off).

No blur, sharpening or dithering of the result. The output is neutral grey (the palette's tints are smaller than one 565
step), encoded by the caller with the neutral-grey BC encoders (Pillow's BC encoder bands and greens dark greys).
Pure Python + Pillow (the frozen Pack Builder has no numpy).
"""
from PIL import Image, ImageFilter

# owner palette (sRGB): background stops and icon stops
PALETTE = {
    "black": (11, 11, 14), "charcoal": (23, 23, 25), "light": (41, 41, 45),
    "primary": (242, 240, 235), "secondary": (216, 213, 208), "inactive": (133, 133, 137), "faded": (85, 85, 91),
}
# background: brightness of the paper (x); icon: brightness of the icons (x); texture: paper contrast around its middle (x);
# pattern: brightness of a printed pattern (pattern_boxes, x)
PARAMS = {"background": 1.0, "icon": 1.0, "texture": 1.0, "pattern": 1.0}

PAPER_STOPS = (34.0, 66.0, 80.0)    # stock paper lightness (L*) that lands on black / charcoal / light
INK_FULL = 32.0                     # L* below the local paper at which a stroke is full strength (primary)
INK_GAMMA = 1.6                     # strength = (darkness / INK_FULL) ** gamma: faint (inactive) strokes stay dim
INK_FLOOR = 0.30                    # minimum strength of detected ink: a faint stock icon stays readable (owner, skull)
INK_STOPS = ((0.0, None), (0.18, "faded"), (0.38, "inactive"), (0.68, "secondary"), (1.0, "primary"))
INK_DARK = (3.0, 9.0)               # ink weight ramps in over this many L* below the local paper
# ... and when its chroma is above INK_CHROMA[0] x the paper's (median of the light paper), over INK_CHROMA[1] units
# (torn edges and most shadow are greyer than the paper)
INK_CHROMA = (0.65, 2.5)   # 0.65 (was 0.85): faint grey-brown icons (Enchanting skull, C 8.3 on paper 8.8) count
# darkness is measured against the local paper AND the medians along the pixel's row and column (LINE px): a shadow band along
# a tab edge is as dark as its row (or column) and stays paper; a stroke is darker than both (stock shadows are as colourful
# and red as thin ink: T_Tab_1 shadow a* 2.9, C 6.6; paper a* 0.8, C 6.2)
LINE = 61
# second route for dense line work (an icon's filled interior is as dark as its own row/column): darker than the local paper
# and clearly more colourful than the paper (chroma above INK_STRONG[0] x the paper's, over INK_STRONG[1]); shadows never are
INK_STRONG = (1.45, 3.0)
# a stroke core: ink weight > 0.7 and more than CORE_DARK L* below the paper; ink is kept within CORE_REACH px of a core
CORE_DARK, CORE_REACH = 14.0, 2
# icon_boxes: ink ramp (L* below the paper and below its row's 181 px median: a tab-bottom shadow band spans the whole tab,
# an icon does not) and core darkness inside a known icon box
BOX_DARK, BOX_CORE, BOX_LINE = (4.0, 10.0), 8.0, 181
BOX_CHROMA = 0.75   # inside a box ink still needs chroma >= 0.75 x the paper's (tab border lines are greyer; skull 0.94)
BOX_PAD = 3   # every icon box grown by this many px (inclusive x1/y1): antialiased edges stay inside
# pattern_boxes (a faint printed pattern, the character creation laurels): drawn toward PATTERN_COLOUR (owner: "white").
# Leaves are 3-7 L* darker than their paper (about the grain's size, but coherent shapes) and greyer (C 7.6 on 9.3); brown
# stains are warmer (C 14.3; T_UI_SkillsClass_TabLeft_On, _LeftPaperTab_Active). Weight = (darkness against the light-paper
# level, softened over PATTERN_SOFT px, minus PATTERN_FLOOR) / PATTERN_FULL, proportional (no threshold edges), x a grey gate:
# chroma below PATTERN_CHROMA x the paper's, or below PATTERN_LOCAL x the neighbourhood's (a leaf over a stain).
PATTERN_COLOUR = "primary"
PATTERN_SOFT, PATTERN_FLOOR, PATTERN_FULL, PATTERN_STRENGTH = 2, 1.0, 6.0, 0.8
PATTERN_CHROMA, PATTERN_LOCAL = 1.2, 0.88
# emblem mode: rim = chroma ramp EMBLEM_RIM, read through a Gaussian blur of EMBLEM_SMOOTH px (mask only: the stock rim's grain
# dips to chroma 11-14 and mottled the outline grey at screen size, RMI-099); the rim is drawn primary; fill grey =
# EMBLEM_FILL[1] + (L* - EMBLEM_FILL[0]) x EMBLEM_FILL[2]
EMBLEM_RIM, EMBLEM_SMOOTH, EMBLEM_FILL = (8.0, 13.0), 2, (48.0, 30.0, 0.9)
SEAM_GRAIN = 4   # seam option: box-mean radius that separates the paper's level from its grain
# selected vs inactive: the stock draws the icons as dark on every tab and lifts the selected tab's paper; the light-paper
# level around a pixel (70th percentile over 61 px) from ACTIVE_PAPER[0] (inactive) to [1] (selected) scales the ink strength
# from ACTIVE_DIM to 1, so inactive icons land near the inactive grey and the selected icon on primary
ACTIVE_PAPER, ACTIVE_DIM, ACTIVE_WINDOW = (68.0, 77.0), 0.5, 31


def _lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


_LIN = [_lin(v) for v in range(256)]


def _f(t):
    return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116


def lab(rgb):
    """sRGB 0-255 -> (L*, a*, b*), D65."""
    r, g, b = _LIN[rgb[0]], _LIN[rgb[1]], _LIN[rgb[2]]
    x = _f((0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047)
    y = _f(0.2126 * r + 0.7152 * g + 0.0722 * b)
    z = _f((0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883)
    return 116 * y - 16, 500 * (x - y), 200 * (y - z)


def lum(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def _smooth(v, lo, hi):
    t = min(1.0, max(0.0, (v - lo) / (hi - lo)))
    return t * t * (3 - 2 * t)


def _mix(a, b, t):
    return a + (b - a) * t


def paper_grey(l, palette=PALETTE, params=PARAMS):
    """Stock paper lightness -> charcoal grey (0-255) along the palette's background stops (extrapolated gently past both)."""
    lo, mid, hi = PAPER_STOPS
    l = mid + (l - mid) * params["texture"]
    black, charcoal, light = (lum(palette[k]) for k in ("black", "charcoal", "light"))
    if l <= mid:
        v = _mix(black, charcoal, (l - lo) / (mid - lo))
        v = max(v, black * 0.45)
    else:
        v = _mix(charcoal, light, (l - mid) / (hi - mid))
        v = min(v, light * 1.25)
    return max(0.0, v * params["background"])


def ink_grey(strength, paper, palette=PALETTE, params=PARAMS):
    """Ink strength 0..1 -> grey: from the paper under it through faded, inactive, secondary to primary."""
    s = min(1.0, max(0.0, strength))
    stops = [(t, paper if k is None else min(255.0, lum(palette[k]) * params["icon"])) for t, k in INK_STOPS]
    for (t0, v0), (t1, v1) in zip(stops, stops[1:]):
        if s <= t1:
            return _mix(v0, v1, (s - t0) / (t1 - t0))
    return stops[-1][1]


def _line_median(values, width, height, size, horizontal):
    """Median of each pixel's `size` neighbours along its row (horizontal) or column, edges clamped (sliding sorted window)."""
    from bisect import insort, bisect_left
    half = size // 2
    out = [0.0] * (width * height)
    lines, length = (height, width) if horizontal else (width, height)
    for j in range(lines):
        idx = (lambda i: j * width + i) if horizontal else (lambda i: i * width + j)
        line = [values[idx(i)] for i in range(length)]
        at = lambda i: line[min(length - 1, max(0, i))]   # noqa: E731
        window = sorted(at(i) for i in range(-half, half + 1))
        for i in range(length):
            out[idx(i)] = window[half]
            del window[bisect_left(window, at(i - half))]
            insort(window, at(i + half + 1))
    return out


def _local_light(img):
    """70th-percentile lightness over a 61 px window (at half resolution): the paper level, ignoring the icon strokes."""
    w, h = img.size
    small = img.resize((max(1, w // 2), max(1, h // 2)), Image.BILINEAR)
    rank = int(ACTIVE_WINDOW * ACTIVE_WINDOW * 0.7)
    return small.filter(ImageFilter.RankFilter(ACTIVE_WINDOW, rank)).resize((w, h), Image.BILINEAR)


def recolour(image, palette=PALETTE, params=PARAMS, glyph=False, seam=None, icon_boxes=(), faint=True, box_row_test=True, pattern_boxes=(), emblem=False):
    """RGBA stock image -> (grey 'L' image of the recoloured colour, 'L' ink weight image for the validation). Alpha is not
    touched here (the caller keeps the stock alpha). icon_boxes ((x0, y0, x1, y1), ...): known icon and arrow areas. Inside
    them being darker than the paper is enough (a faint filled drawing, the Enchanting skull, fails the row/column and colour
    tests); outside them nothing is ink (owner run: blotches on torn edges and shadows of the opaque BC1 strips). faint=False: the
    boxes only limit where ink may be (Inventory, Magic: no faint icons, and the relaxed test catches the tabs' side shadows).
    box_row_test=False: no wide-row test inside the boxes (a long thin rule line is as dark as its own row; character
    creation flourishes, boxes drawn tight so no shadow is inside). seam (row from, row to, grey): over those rows the paper's broad level
    (shadows included) is moved to `grey` (smoothstep in, kept below), its fine grain kept: the strip meets the panel under it
    without a tone step (RMI-091 seam)."""
    image = image.convert("RGBA")
    width, height = image.size
    pixels = list(image.get_flattened_data())
    cache = {}
    ls, cs, bs = [], [], []
    for p in pixels:
        key = p[:3]
        v = cache.get(key)
        if v is None:
            l, a, b = lab(key)
            v = cache[key] = (l, (a * a + b * b) ** 0.5, b)
        ls.append(v[0])
        cs.append(v[1])
        bs.append(v[2])
    if emblem:
        # a stand-alone embossed icon (the Stats tab icons T_Category_attributes ...): a thick brown rim (chroma 20-26,
        # L* 17-34) around a grainy paper fill (chroma 5-10, L* 43-55) on transparency. Rim -> light (secondary..primary by
        # its colour strength), fill -> charcoal with its grain (lightness kept around EMBLEM_FILL); alpha untouched.
        # The rim test reads a smoothed chroma (the grain must not punch dark specks into the outline, RMI-099).
        rim_lo, rim_hi = EMBLEM_RIM
        c_img = Image.new("L", image.size)
        c_img.putdata([max(0, min(255, round(c * 6))) for c in cs])
        cs_s = [v / 6 for v in c_img.filter(ImageFilter.GaussianBlur(EMBLEM_SMOOTH)).get_flattened_data()]
        ink = ink_grey(1.0, 0.0, palette, params)
        out = []
        for l, c in zip(ls, cs_s):
            fill = max(0.0, EMBLEM_FILL[1] + (l - EMBLEM_FILL[0]) * EMBLEM_FILL[2]) * params["background"]
            out.append(max(0, min(255, round(_mix(fill, ink, _smooth(c, rim_lo, rim_hi))))))
        grey = Image.new("L", image.size)
        grey.putdata(out)
        weight = Image.new("L", image.size)
        weight.putdata([round(255 * _smooth(c, rim_lo, rim_hi)) for c in cs_s])
        return grey, weight
    if glyph:
        primary = min(255.0, lum(palette["primary"]) * params["icon"])
        grey = Image.new("L", image.size, round(primary))
        return grey, Image.new("L", image.size, 255)
    l_img = Image.new("L", image.size)
    l_img.putdata([max(0, min(255, round(l * 2.55))) for l in ls])
    # the local paper: its light level (70th percentile over 61 px; the strokes, even a dense icon's, are below it)
    l_med = [v / 2.55 for v in _local_light(l_img).get_flattened_data()]
    a0, a1 = ACTIVE_PAPER
    active = [ACTIVE_DIM + (1 - ACTIVE_DIM) * _smooth(v, a0, a1) for v in l_med]
    # chroma for the ink test read through a 3x3 average (the stock's BC blocks make shadow chroma jump block by block);
    # the output itself is never blurred
    c_img = Image.new("L", image.size)
    c_img.putdata([max(0, min(255, round(c * 4))) for c in cs])
    c_avg = [v / 4 for v in c_img.filter(ImageFilter.BoxBlur(1)).get_flattened_data()]
    # the paper's own chroma: median over the light opaque paper (a shadow is greyer than this, ink at least as colourful)
    light = [i for i, (l, p) in enumerate(zip(ls, pixels)) if p[3] > 200 and l > PAPER_STOPS[1]] or range(len(ls))
    cm = sorted(c_avg[i] for i in light)[len(light) // 2]
    rows = _line_median(ls, width, height, LINE, horizontal=True)
    cols = _line_median(ls, width, height, LINE, horizontal=False)
    d0, d1 = INK_DARK
    k, ramp = INK_CHROMA
    sk, sramp = INK_STRONG
    raw, core = [], []
    boxed = bytearray(len(ls))
    for x0, y0, x1, y1 in ((x0 - BOX_PAD, y0 - BOX_PAD, x1 + BOX_PAD + 1, y1 + BOX_PAD + 1) for x0, y0, x1, y1 in icon_boxes):
        for y in range(max(0, y0), min(height, y1)):
            boxed[y * width + max(0, x0):y * width + min(width, x1)] = bytes([1]) * (min(width, x1) - max(0, x0))
    fb0, fb1 = BOX_DARK
    wide = _line_median(ls, width, height, BOX_LINE, horizontal=True) if icon_boxes and faint and box_row_test else l_med
    for i, (l, c, lm, lr, lc, lw, p) in enumerate(zip(ls, c_avg, l_med, rows, cols, wide, pixels)):
        thin = _smooth(min(lm, lr, lc) - l, d0, d1) * _smooth(c - k * cm, 0.0, ramp)
        dense = _smooth(lm - l, d0, d1) * _smooth(c - sk * cm, 0.0, sramp)
        box = _smooth(min(lm, lw) - l, fb0, fb1) * _smooth(c - BOX_CHROMA * cm, 0.0, ramp) if faint and boxed[i] else 0.0
        w = max(thin, dense, box) * _smooth(p[3], 128, 230)   # no ink on see-through edges
        if icon_boxes and not boxed[i]:   # boxes given = every icon and arrow is in one: nothing outside is ink (torn edges)
            w = 0.0
        raw.append(w)
        core.append(255 if w > 0.7 and lm - l > (BOX_CORE if faint and boxed[i] else CORE_DARK) else 0)
    # ink only near a definite stroke core (within CORE_REACH px): faint isolated blotches in shadows and stains stay paper
    core_img = Image.new("L", image.size)
    core_img.putdata(core)
    near = core_img.filter(ImageFilter.MaxFilter(2 * CORE_REACH + 1)).get_flattened_data()
    weights_k = [w if n else 0.0 for w, n in zip(raw, near)]
    papers = [paper_grey(l + (lm - l) * w if lm > l else l, palette, params) for l, lm, w in zip(ls, l_med, weights_k)]
    if seam:
        # over the seam rows the paper's broad level (4 px box mean: the shadows, the gradient) goes to the seam grey and its
        # fine grain (paper - that mean) stays
        p_img = Image.new("L", image.size)
        p_img.putdata([max(0, min(255, round(v))) for v in papers])
        mean = list(p_img.filter(ImageFilter.BoxBlur(SEAM_GRAIN)).get_flattened_data())
        for i in range(len(papers)):
            t = _smooth(i // width, seam[0], seam[1])
            if t:
                papers[i] = max(0.0, papers[i] + t * (seam[2] - mean[i]))
    pattern = bytearray(len(ls))
    for x0, y0, x1, y1 in pattern_boxes:
        for y in range(max(0, y0), min(height, y1)):
            pattern[y * width + max(0, x0):y * width + min(width, x1)] = bytes([1]) * (min(width, x1) - max(0, x0))
    if pattern_boxes:
        # leaf darkness against the light-paper level, read through a PATTERN_SOFT px blur (mask only: the fine grain averages
        # out, a 10 px leaf stays)
        d_img = Image.new("L", image.size)
        d_img.putdata([max(0, min(255, round((lm - l) * 20 + 128))) for l, lm in zip(ls, l_med)])
        d_soft = [(v - 128) / 20 for v in d_img.filter(ImageFilter.BoxBlur(PATTERN_SOFT)).get_flattened_data()]
        # the neighbourhood's chroma (median over 61 px at half resolution): a leaf on a warm stain is greyer than the stain
        cl_img = Image.new("L", image.size)
        cl_img.putdata([max(0, min(255, round(c * 8))) for c in c_avg])
        w2, h2 = image.size
        c_local = [v / 8 for v in cl_img.resize((max(1, w2 // 2), max(1, h2 // 2)), Image.BILINEAR)
                   .filter(ImageFilter.MedianFilter(ACTIVE_WINDOW)).resize(image.size, Image.BILINEAR).get_flattened_data()]
        pattern_grey = min(255.0, lum(palette[PATTERN_COLOUR]) * params["icon"] * params["pattern"])
    out, weights = [], []
    for i, (l, lm, act, w, paper) in enumerate(zip(ls, l_med, active, weights_k, papers)):
        dark = lm - l
        v = paper if w <= 0 else _mix(paper, ink_grey(max(INK_FLOOR, act * min(1.0, max(0.0, dark) / INK_FULL) ** INK_GAMMA), paper, palette, params), w)
        if pattern[i] and w < 0.5:
            grey_enough = max(_smooth(PATTERN_CHROMA * cm - c_avg[i], 0.0, ramp),
                              _smooth(PATTERN_LOCAL * c_local[i] - c_avg[i], 0.0, ramp))
            pw = min(1.0, max(0.0, d_soft[i] - PATTERN_FLOOR) / PATTERN_FULL) * grey_enough
            if pw > 0:   # proportional: the leaf's paper level, lifted toward the pattern colour by how dark the stock leaf is
                v = _mix(paper_grey(lm, palette, params), pattern_grey, pw * PATTERN_STRENGTH) if l < lm else v
        out.append(max(0, min(255, round(v))))
        weights.append(round(255 * w))
    grey = Image.new("L", image.size)
    grey.putdata(out)
    weight = Image.new("L", image.size)
    weight.putdata(weights)
    return grey, weight


# ---- neutral-grey BC1 (the two BC1 Enchanting strips are fully opaque: four-colour blocks, no punch-through) ----
GREY5 = [(k << 3) | (k >> 2) for k in range(32)]


def neutral_grey_bc1(blocks, grey):
    """BC1 blocks rewritten as exact neutral greys (565 green = 2 x red/blue): endpoints bracket the block's range on the 5-bit
    grey ladder (c0 > c1, four-colour mode), each pixel the nearest of the four palette greys. No dither. Refuses a stock
    block that uses punch-through transparency (its alpha would change)."""
    out = bytearray(blocks)
    width, height = grey.size
    values = list(grey.get_flattened_data())
    at = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            c0, c1 = int.from_bytes(out[at:at + 2], "little"), int.from_bytes(out[at + 2:at + 4], "little")
            bits = int.from_bytes(out[at + 4:at + 8], "little")
            if c0 <= c1 and any(((bits >> (2 * i)) & 3) == 3 for i in range(16)):
                raise ValueError("a stock BC1 block uses punch-through transparency")
            block = [values[min(by + y, height - 1) * width + min(bx + x, width - 1)] for y in range(4) for x in range(4)]
            lo, hi = min(block), max(block)
            k0 = max([k for k in range(32) if GREY5[k] <= lo] or [0])
            k1 = min([k for k in range(32) if GREY5[k] >= hi] or [31])
            if k1 <= k0:
                k0, k1 = (k0, k0 + 1) if k0 < 31 else (30, 31)
            p0, p1 = GREY5[k1], GREY5[k0]
            palette = (p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3)
            index = 0
            for i, v in enumerate(block):
                index |= min(range(4), key=lambda j: abs(palette[j] - v)) << (2 * i)
            e0 = (k1 << 11) | ((2 * k1) << 5) | k1
            e1 = (k0 << 11) | ((2 * k0) << 5) | k0
            out[at:at + 8] = e0.to_bytes(2, "little") + e1.to_bytes(2, "little") + index.to_bytes(4, "little")
            at += 8
    return bytes(out)


# ---- presets: the recipe charcoal:<preset> (tools/menus/tabs.json); boxes measured on the stock (game 1.512.105.0) ----
_ARROWS = tuple((c - 12, 36, c + 14, 63) for c in (108, 235, 279, 405, 450, 577, 623, 750, 795, 921))   # make_dark_tab_art.ARROWS
_SKILLCLASS_FLOURISH = ((40, 86, 508, 97), (128, 78, 390, 106))   # rule line (x from 40: a stain on _LeftPaperTab_Active) + ornament
_NOWHERE = ((-9, -9, -9, -9),)   # a box outside the image: nothing is ink
PRESETS = {
    "": {},   # generic tests over the whole texture (no data row uses it since RMI-098)
    # T_Tab_1-5 (the Stats strip): arrows only; outside the arrow boxes nothing is ink (RMI-098: the generic test lit the torn
    # tile corners, bottom tears and the notch under tile 4)
    "arrows": {"icon_boxes": _ARROWS, "faint": False},
    # icon and arrow boxes as an ink mask only (no faint icons; the relaxed test caught the tabs' warm side shadows)
    "inventory": {"icon_boxes": _ARROWS + ((134, 12, 208, 87), (312, 13, 373, 86), (477, 13, 548, 84), (647, 13, 722, 86),
                                           (821, 13, 893, 86)), "faint": False},
    "magic": {"icon_boxes": _ARROWS + ((142, 13, 201, 86), (311, 15, 374, 84), (476, 14, 549, 86), (647, 13, 720, 86),
                                       (821, 14, 893, 84)), "faint": False},
    # faint filled icons (the skull, the inactive chalices) need the relaxed test inside their boxes
    "enchanting": {"icon_boxes": ((94, 38, 120, 65), (168, 9, 239, 94), (286, 38, 312, 65), (403, 38, 430, 65), (483, 13, 545, 90),
                                  (595, 38, 621, 65), (712, 38, 738, 65), (780, 9, 862, 93), (903, 38, 929, 65))},
    "quests": {"icon_boxes": ((121, 38, 147, 67), (264, 16, 323, 89), (441, 38, 465, 66), (555, 38, 580, 66), (689, 12, 764, 92),
                              (874, 38, 899, 66))},
    "glyph": {"glyph": True},
    "emblem": {"emblem": True},
    # character creation: no icons; the flourish rule is ink, the laurels a white pattern
    "paper": {"icon_boxes": _NOWHERE},
    "laurel": {"icon_boxes": _NOWHERE, "pattern_boxes": ((0, 0, 512, 100),)},
    "flourish": {"icon_boxes": _SKILLCLASS_FLOURISH, "box_row_test": False, "pattern_boxes": ((0, 0, 512, 84),)},
    "header_l": {"icon_boxes": ((16, 138, 500, 150), (130, 124, 390, 166)), "box_row_test": False, "pattern_boxes": ((0, 0, 512, 132),)},
    "header_r": {"icon_boxes": ((524, 138, 1008, 150), (640, 124, 900, 166)), "box_row_test": False,
                 "pattern_boxes": ((512, 0, 1024, 132),)},
}
