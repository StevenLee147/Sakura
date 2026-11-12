"""Build Sakura-樱 high-DPI Luminous Glass & Moonlit Ink-Sakura visual assets.

Deterministic procedural visual material generator (numpy + Pillow).
Generates:
  - resources/images/particles/*.png   (radial glows, petals, bokeh, starbursts, ripples, silk core)
  - resources/images/ui/*.png          (sakura crest, gilded ornaments, rank emblems SS/S/A/B/C/D)
  - resources/images/backgrounds/*.png (moonlit ink-sakura sky, mountain layers, misty canopy)
  - resources/images/covers/default_cover.png
  - resources/charts/*/cover.png       (1200x1200 bespoke covers for all 7 built-in charts)

IMPORTANT: Does NOT touch chart *.json or music.* files so replay/chart hashes remain 100% intact.
"""

from __future__ import annotations

import math
from pathlib import Path
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PARTICLES_DIR = ROOT / "resources" / "images" / "particles"
UI_DIR = ROOT / "resources" / "images" / "ui"
BG_DIR = ROOT / "resources" / "images" / "backgrounds"
COVERS_DIR = ROOT / "resources" / "images" / "covers"
CHARTS_DIR = ROOT / "resources" / "charts"
FONT_REG = ROOT / "resources" / "fonts" / "NotoSansSC-Regular.ttf"
FONT_BOLD = ROOT / "resources" / "fonts" / "NotoSansSC-Bold.ttf"


def ensure_dirs() -> None:
    for d in (PARTICLES_DIR, UI_DIR, BG_DIR, COVERS_DIR):
        d.mkdir(parents=True, exist_ok=True)


def load_font(bold: bool, size: int) -> ImageFont.FreeTypeFont:
    path = FONT_BOLD if (bold and FONT_BOLD.exists()) else FONT_REG
    return ImageFont.truetype(str(path), size)


def save_rgba(arr: np.ndarray, path: Path) -> None:
    arr_u8 = np.clip(np.rint(arr), 0, 255).astype(np.uint8)
    Image.fromarray(arr_u8, mode="RGBA").save(path, optimize=True)


# ============================================================================
# 1. Particle & Optical LUT Sprites
# ============================================================================

def build_radial_glow(size: int = 256) -> None:
    y, x = np.ogrid[:size, :size]
    c = (size - 1) * 0.5
    r = np.sqrt((x - c) ** 2 + (y - c) ** 2) / c
    mask = np.clip(1.0 - r, 0.0, 1.0)
    # Dual-Gaussian + smoothstep core for zero-banding additive/alpha glow
    core = np.exp(-5.2 * (r ** 2)) * (mask ** 1.35)
    halo = np.exp(-1.85 * (r ** 2)) * (mask ** 2.1)
    alpha = np.clip(0.68 * core + 0.42 * halo, 0.0, 1.0)
    arr = np.zeros((size, size, 4), dtype=np.float32)
    arr[..., :3] = 255.0
    arr[..., 3] = alpha * 255.0
    save_rgba(arr, PARTICLES_DIR / "glow_radial.png")


def build_anamorphic_streak(w: int = 512, h: int = 128) -> None:
    y, x = np.ogrid[:h, :w]
    nx = (x - (w - 1) * 0.5) / ((w - 1) * 0.5)
    ny = (y - (h - 1) * 0.5) / ((h - 1) * 0.5)
    horiz = np.exp(-3.6 * (np.abs(nx) ** 1.45)) * np.clip(1.0 - np.abs(nx), 0.0, 1.0) ** 1.2
    vert = np.exp(-14.0 * (ny ** 2)) * np.clip(1.0 - np.abs(ny), 0.0, 1.0) ** 1.5
    center = np.exp(-18.0 * (nx ** 2 + ny ** 2))
    alpha = np.clip(horiz * vert * 0.88 + center * 0.45, 0.0, 1.0)
    arr = np.zeros((h, w, 4), dtype=np.float32)
    arr[..., :3] = 255.0
    arr[..., 3] = alpha * 255.0
    save_rgba(arr, PARTICLES_DIR / "glow_streak.png")


def build_bokeh_disc(size: int = 256) -> None:
    y, x = np.ogrid[:size, :size]
    c = (size - 1) * 0.5
    dx = (x - c) / (c * 0.90)
    dy = (y - c) / (c * 0.90)
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    # Subtle 8-blade aperture polygon modulation
    poly = 0.965 + 0.035 * np.cos(8.0 * ang)
    rp = r / poly
    interior = np.clip((1.0 - rp) * 14.0, 0.0, 1.0)
    rim = np.exp(-42.0 * ((rp - 0.86) ** 2)) * (rp <= 1.03)
    fill = (0.28 + 0.24 * (rp ** 2)) * interior
    alpha = np.clip(fill + 0.62 * rim * interior, 0.0, 1.0)
    img = Image.fromarray(np.clip(alpha * 255.0, 0, 255).astype(np.uint8), mode="L")
    img = img.filter(ImageFilter.GaussianBlur(radius=2.2))
    a_np = np.asarray(img, dtype=np.float32)
    arr = np.zeros((size, size, 4), dtype=np.float32)
    arr[..., :3] = 255.0
    arr[..., 3] = a_np
    save_rgba(arr, PARTICLES_DIR / "bokeh_soft.png")


def build_starburst(size: int = 256) -> None:
    y, x = np.ogrid[:size, :size]
    c = (size - 1) * 0.5
    dx = (x - c) / c
    dy = (y - c) / c
    r = np.sqrt(dx * dx + dy * dy) + 1e-6
    falloff = np.clip(1.0 - r, 0.0, 1.0) ** 1.6
    # Primary 4-point cross + secondary 45-deg glint + bright core
    ray_hv = (np.exp(-95.0 * (dy ** 2)) * np.exp(-2.8 * np.abs(dx)) +
              np.exp(-95.0 * (dx ** 2)) * np.exp(-2.8 * np.abs(dy)))
    rx = (dx + dy) * 0.70710678
    ry = (dx - dy) * 0.70710678
    ray_diag = 0.52 * (np.exp(-140.0 * (ry ** 2)) * np.exp(-4.6 * np.abs(rx)) +
                       np.exp(-140.0 * (rx ** 2)) * np.exp(-4.6 * np.abs(ry)))
    core = np.exp(-28.0 * (r ** 2))
    alpha = np.clip((ray_hv + ray_diag + core * 1.15) * falloff, 0.0, 1.0)
    arr = np.zeros((size, size, 4), dtype=np.float32)
    arr[..., :3] = 255.0
    arr[..., 3] = alpha * 255.0
    save_rgba(arr, PARTICLES_DIR / "starburst.png")


def build_shockwave_ring(size: int = 256) -> None:
    y, x = np.ogrid[:size, :size]
    c = (size - 1) * 0.5
    r = np.sqrt((x - c) ** 2 + (y - c) ** 2) / (c * 0.92)
    outer_ring = np.exp(-115.0 * ((r - 0.84) ** 2))
    inner_wake = 0.34 * np.exp(-48.0 * ((r - 0.68) ** 2))
    shimmer = 0.12 * np.clip(1.0 - r / 0.86, 0.0, 1.0) * (r < 0.86)
    alpha = np.clip((outer_ring + inner_wake + shimmer) * (r <= 1.04), 0.0, 1.0)
    arr = np.zeros((size, size, 4), dtype=np.float32)
    arr[..., :3] = 255.0
    arr[..., 3] = alpha * 255.0
    save_rgba(arr, PARTICLES_DIR / "ring_ripple.png")


def build_sakura_petals() -> None:
    for variant, (wobble, notch_depth, width_scale, fname) in enumerate([
        (0.06, 0.24, 0.62, "petal_primary.png"),
        (-0.08, 0.18, 0.50, "petal_slender.png"),
    ]):
        ss = 512
        y, x = np.mgrid[0:ss, 0:ss].astype(np.float32)
        nx = (x - ss * 0.5) / (ss * 0.42)
        ny = (y - ss * 0.52) / (ss * 0.42)
        # Bend petal curve gently
        nx_bent = nx + wobble * (ny ** 2)
        # Normalized纵向参数 v in [-1 (base), +1 (notched tip)]
        v = -ny
        # Teardrop envelope + apical sakura cleft notch
        t = np.clip((v + 1.0) * 0.5, 0.0, 1.0)
        half_w = width_scale * (np.sin(np.pi * (t ** 0.76)) + 0.14 * (1.0 - t) * t)
        notch = notch_depth * np.exp(-34.0 * ((nx_bent / max(width_scale, 0.2)) ** 2))
        top_limit = 0.96 - notch
        inside_y = (v >= -0.95) & (v <= top_limit)
        dist_edge = half_w - np.abs(nx_bent)
        dist_top = top_limit - v
        dist_bot = v + 0.95
        sdf = np.minimum(np.minimum(dist_edge, dist_top), dist_bot)
        alpha = np.clip(sdf * 38.0, 0.0, 1.0) * inside_y

        # Delicate vein & base-to-tip luminous gradient (near-white so vertex Color tints cleanly)
        vein = np.exp(-26.0 * (nx_bent ** 2)) * np.clip(1.0 - t * 0.85, 0.0, 1.0)
        lum_g = np.clip(236.0 + 19.0 * t - 12.0 * vein, 0.0, 255.0)
        lum_r = np.full_like(lum_g, 255.0)
        lum_b = np.clip(244.0 + 11.0 * t - 6.0 * vein, 0.0, 255.0)
        # Subtle translucent rim highlight
        rim = np.exp(-120.0 * (sdf ** 2)) * (alpha > 0.05)
        alpha_shaded = alpha * (0.84 + 0.16 * (1.0 - t * 0.35) + 0.14 * rim)

        arr = np.stack([
            lum_r,
            lum_g,
            lum_b,
            np.clip(alpha_shaded, 0.0, 1.0) * 255.0,
        ], axis=-1)
        img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode="RGBA")
        img = img.resize((128, 128), Image.Resampling.LANCZOS)
        img.save(PARTICLES_DIR / fname, optimize=True)


# ============================================================================
# 2. UI Emblems, Sakura Crest & Ornaments
# ============================================================================

def draw_sakura_blossom_mask(size: int, outer_r: float, inner_r: float, rotation: float = -math.pi / 2) -> np.ndarray:
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    cx = cy = (size - 1) * 0.5
    dx = x - cx
    dy = y - cy
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx) - rotation
    # 5-petal modular coordinate
    sector = (2.0 * math.pi) / 5.0
    local_a = ((ang + sector * 0.5) % sector) - sector * 0.5
    u = np.abs(local_a) / (sector * 0.5)  # 0 at petal centerline, 1 between petals
    # Cleft notch at u ~ 0
    cleft = 0.13 * np.exp(-26.0 * (u ** 2))
    lobe = np.cos(u * math.pi * 0.5) ** 0.72
    target_r = inner_r + (outer_r - inner_r) * lobe - outer_r * cleft
    mask = np.clip((target_r - r) * 0.85, 0.0, 1.0)
    return mask


def build_sakura_crest() -> None:
    ss = 512
    mask_outer = draw_sakura_blossom_mask(ss, ss * 0.42, ss * 0.13)
    mask_inner = draw_sakura_blossom_mask(ss, ss * 0.25, ss * 0.08, rotation=-math.pi / 2 + math.pi / 5)

    y, x = np.mgrid[0:ss, 0:ss].astype(np.float32)
    c = (ss - 1) * 0.5
    r = np.sqrt((x - c) ** 2 + (y - c) ** 2) / (ss * 0.5)

    # Iridescent sakura-pearl to warm-gold gradient
    grad = np.clip(r / 0.85, 0.0, 1.0)
    cr = 255.0 * (1.0 - grad * 0.04)
    cg = 232.0 * (1.0 - grad) + 188.0 * grad
    cb = 242.0 * (1.0 - grad) + 216.0 * grad

    # Gilded outline & stamen ring
    edge = np.clip(mask_outer * (1.0 - mask_outer) * 4.0, 0.0, 1.0)
    ring = np.exp(-260.0 * ((r - 0.16) ** 2)) + 0.7 * np.exp(-220.0 * ((r - 0.74) ** 2))

    alpha = np.clip(mask_outer * 0.88 + mask_inner * 0.35 + ring * 0.55, 0.0, 1.0)
    cr = np.clip(cr + (250.0 - cr) * np.clip(edge + ring, 0.0, 1.0), 0.0, 255.0)
    cg = np.clip(cg + (216.0 - cg) * np.clip(edge + ring, 0.0, 1.0), 0.0, 255.0)
    cb = np.clip(cb + (157.0 - cb) * np.clip(edge + ring, 0.0, 1.0), 0.0, 255.0)

    img = Image.fromarray(np.clip(np.stack([cr, cg, cb, alpha * 255.0], axis=-1), 0, 255).astype(np.uint8), "RGBA")
    draw = ImageDraw.Draw(img)
    for i in range(10):
        a = i * math.tau / 10.0 - math.pi / 2
        x1 = c + math.cos(a) * (ss * 0.05)
        y1 = c + math.sin(a) * (ss * 0.05)
        x2 = c + math.cos(a) * (ss * 0.145)
        y2 = c + math.sin(a) * (ss * 0.145)
        draw.line([(x1, y1), (x2, y2)], fill=(252, 224, 170, 235), width=3)
        draw.ellipse([x2 - 5, y2 - 5, x2 + 5, y2 + 5], fill=(255, 242, 206, 250))
    draw.ellipse([c - 11, c - 11, c + 11, c + 11], fill=(255, 245, 222, 255))

    img.resize((256, 256), Image.Resampling.LANCZOS).save(UI_DIR / "sakura_crest.png", optimize=True)


def build_rank_emblems() -> None:
    ranks = [
        ("SS", (255, 232, 168), (255, 166, 212), (255, 248, 225)),
        ("S",  (252, 214, 138), (242, 148, 188), (255, 244, 214)),
        ("A",  (244, 162, 198), (168, 132, 236), (255, 236, 246)),
        ("B",  (132, 208, 236), (98, 142, 228),  (232, 248, 255)),
        ("C",  (156, 214, 186), (96, 166, 152),  (234, 250, 242)),
        ("D",  (178, 168, 192), (112, 104, 132), (232, 228, 238)),
    ]
    ss = 512
    c = ss * 0.5
    for label, col_top, col_bot, col_spec in ranks:
        img = Image.new("RGBA", (ss, ss), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        # Outer radial halo
        y, x = np.mgrid[0:ss, 0:ss].astype(np.float32)
        r = np.sqrt((x - c) ** 2 + (y - c) ** 2) / (ss * 0.5)
        halo = np.exp(-4.2 * (r ** 2)) * np.clip(1.0 - r, 0.0, 1.0)
        halo_arr = np.zeros((ss, ss, 4), dtype=np.uint8)
        halo_arr[..., 0] = col_top[0]
        halo_arr[..., 1] = col_top[1]
        halo_arr[..., 2] = col_top[2]
        halo_arr[..., 3] = np.clip(halo * 135.0, 0, 255).astype(np.uint8)
        img = Image.alpha_composite(img, Image.fromarray(halo_arr, "RGBA"))
        draw = ImageDraw.Draw(img)

        # Octagram / Diamond-blossom frame
        for k, rot in enumerate((0.0, math.pi / 4.0)):
            pts = []
            rad = ss * (0.37 if k == 0 else 0.34)
            for i in range(4):
                a = rot + i * math.pi / 2.0
                pts.append((c + math.cos(a) * rad, c + math.sin(a) * rad))
            fill_col = (24, 22, 42, 205) if k == 0 else (34, 28, 56, 155)
            draw.polygon(pts, fill=fill_col, outline=(*col_top, 230), width=5 if k == 0 else 3)

        # Concentric gilded ring
        ring_r = ss * 0.31
        draw.ellipse([c - ring_r, c - ring_r, c + ring_r, c + ring_r],
                     fill=(18, 20, 38, 220), outline=(*col_spec, 240), width=5)
        inner_r = ss * 0.275
        draw.ellipse([c - inner_r, c - inner_r, c + inner_r, c + inner_r],
                     outline=(*col_bot, 185), width=2)

        # Rank typography with gradient fill
        font_sz = 190 if len(label) == 1 else 152
        font = load_font(bold=True, size=font_sz)
        txt_mask = Image.new("L", (ss, ss), 0)
        td = ImageDraw.Draw(txt_mask)
        bbox = td.textbbox((0, 0), label, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        tx = c - tw * 0.5 - bbox[0]
        ty = c - th * 0.5 - bbox[1] - 4
        td.text((tx, ty), label, font=font, fill=255)

        # Soft glow behind glyph
        glow_mask = txt_mask.filter(ImageFilter.GaussianBlur(radius=14))
        glow_layer = Image.new("RGBA", (ss, ss), (*col_bot, 0))
        glow_layer.putalpha(ImageEnhance.Brightness(glow_mask).enhance(0.9))
        img = Image.alpha_composite(img, glow_layer)

        # Metallic vertical gradient inside glyph
        gy = np.linspace(0.0, 1.0, ss, dtype=np.float32)[:, None]
        grad_rgb = np.zeros((ss, ss, 4), dtype=np.uint8)
        for ch in range(3):
            v = col_spec[ch] * np.clip(1.0 - gy * 1.9, 0.0, 1.0) + \
                col_top[ch] * (1.0 - np.abs(gy - 0.48) * 1.6).clip(0.0, 1.0) * 0.65 + \
                col_bot[ch] * np.clip((gy - 0.35) * 1.5, 0.0, 1.0)
            grad_rgb[..., ch] = np.clip(v, 0, 255).astype(np.uint8)
        grad_rgb[..., 3] = np.asarray(txt_mask, dtype=np.uint8)
        img = Image.alpha_composite(img, Image.fromarray(grad_rgb, "RGBA"))

        img.resize((256, 256), Image.Resampling.LANCZOS).save(
            UI_DIR / f"rank_{label.lower()}.png", optimize=True
        )


# ============================================================================
# 3. Moonlit Ink-Sakura Backdrop & Cover Artwork Engine
# ============================================================================

def fbm_1d(x: np.ndarray, seed: int, octaves: int = 5) -> np.ndarray:
    rng = np.random.default_rng(seed)
    val = np.zeros_like(x, dtype=np.float32)
    amp = 0.5
    freq = 1.0
    for _ in range(octaves):
        phase = float(rng.uniform(0.0, math.tau))
        val += amp * np.sin(x * freq + phase)
        freq *= 2.07
        amp *= 0.5
    return val


def render_ink_sakura_artwork(
    width: int,
    height: int,
    palette: dict,
    seed: int,
    with_typography: dict | None = None,
    geometric_motif: str = "moon",
) -> Image.Image:
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:height, 0:width].astype(np.float32)
    nx = x / max(width - 1, 1)
    ny = y / max(height - 1, 1)

    top = np.array(palette["sky_top"], dtype=np.float32)
    mid = np.array(palette["sky_mid"], dtype=np.float32)
    horizon = np.array(palette["horizon"], dtype=np.float32)
    accent = np.array(palette["accent"], dtype=np.float32)
    gold = np.array(palette.get("gold", (250, 216, 157)), dtype=np.float32)

    # Multi-stop atmospheric night-ink sky
    t1 = np.clip(ny / 0.56, 0.0, 1.0)[..., None]
    t2 = np.clip((ny - 0.45) / 0.55, 0.0, 1.0)[..., None]
    canvas = top * (1.0 - t1) + mid * t1
    canvas = canvas * (1.0 - t2 * 0.85) + horizon * (t2 * 0.85)

    # Diagonal aurora / silk mist ribbon across sky
    wave_coord = nx * 3.4 + math.sin(seed) * 0.7
    silk_center = 0.36 + 0.11 * np.sin(wave_coord) + 0.05 * np.cos(nx * 6.2 + 1.1)
    silk_band = np.exp(-26.0 * ((ny - silk_center) ** 2)) * (0.45 + 0.55 * np.sin(nx * math.pi))
    canvas += accent[None, None, :] * silk_band[..., None] * 0.28

    # Luminous Moon + halo
    mcx = width * palette.get("moon_x", 0.62)
    mcy = height * palette.get("moon_y", 0.36)
    mr = min(width, height) * palette.get("moon_r", 0.21)
    md = np.sqrt((x - mcx) ** 2 + (y - mcy) ** 2) / mr
    moon_glow = np.exp(-1.55 * (md ** 2)) * 0.52 + np.exp(-0.45 * (md ** 2)) * 0.22
    canvas += (accent * 0.55 + gold * 0.45)[None, None, :] * moon_glow[..., None]

    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), mode="RGB").convert("RGBA")
    draw = ImageDraw.Draw(img)

    # Starfield pinpoints
    for _ in range(int(95 * (width / 1200.0))):
        sx = float(rng.uniform(0.02, 0.98)) * width
        sy = float(rng.uniform(0.02, 0.62)) * height
        sr = float(rng.uniform(0.7, 2.1))
        sa = int(rng.uniform(70, 210))
        draw.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=(255, 246, 232, sa))

    # Crisp moon disc + gilded astrolabe rings
    moon_disc = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    mdraw = ImageDraw.Draw(moon_disc)
    moon_fill = (
        min(255, int(gold[0] * 0.52 + 122)),
        min(255, int(gold[1] * 0.48 + 126)),
        min(255, int(accent[2] * 0.42 + 142)),
        238,
    )
    mdraw.ellipse([mcx - mr, mcy - mr, mcx + mr, mcy + mr], fill=moon_fill)
    for ring_scale, alpha_ring, w_ring in [(1.14, 120, 2), (1.30, 75, 2), (1.52, 40, 1)]:
        rr = mr * ring_scale
        mdraw.ellipse(
            [mcx - rr, mcy - rr, mcx + rr, mcy + rr],
            outline=(int(gold[0]), int(gold[1]), int(gold[2]), alpha_ring),
            width=max(1, int(w_ring * width / 1200)),
        )

    if geometric_motif == "prism":
        # Arcaea-style crystalline diamond prism lines
        for k, rad_mul in enumerate((0.88, 1.24, 1.56)):
            rr = mr * rad_mul
            pts = [
                (mcx, mcy - rr * 1.12),
                (mcx + rr * 0.92, mcy),
                (mcx, mcy + rr * 1.12),
                (mcx - rr * 0.92, mcy),
            ]
            mdraw.polygon(
                pts,
                outline=(int(accent[0]), int(accent[1]), 255, 130 - k * 32),
                width=max(2, int(3 * width / 1200)),
            )
    elif geometric_motif == "vortex":
        for i in range(18):
            a0 = i * 20.0 + seed * 7.0
            rr = mr * (0.72 + (i % 5) * 0.19)
            mdraw.arc(
                [mcx - rr, mcy - rr, mcx + rr, mcy + rr],
                start=a0,
                end=a0 + 68.0,
                fill=(255, 195, 222, 135),
                width=max(2, int(3 * width / 1200)),
            )
    elif geometric_motif == "clockwork":
        for i in range(24):
            ang = i * math.tau / 24.0
            r_in = mr * (1.05 if i % 2 == 0 else 1.10)
            r_out = mr * (1.26 if i % 6 == 0 else 1.18)
            mdraw.line(
                [(mcx + math.cos(ang) * r_in, mcy + math.sin(ang) * r_in),
                 (mcx + math.cos(ang) * r_out, mcy + math.sin(ang) * r_out)],
                fill=(int(gold[0]), int(gold[1]), int(gold[2]), 165),
                width=max(2, int(2 * width / 1200)),
            )

    img = Image.alpha_composite(img, moon_disc)

    # Layered sumi-ink mountain ridges with atmospheric mist separation
    xs = np.linspace(0.0, 1.0, width // 2 + 1, dtype=np.float32)
    ridge_cols = palette["ridges"]
    for idx, rcol in enumerate(ridge_cols):
        base_y = height * (0.60 + idx * 0.085)
        amp_y = height * (0.075 - idx * 0.012)
        curve = base_y - amp_y * fbm_1d(xs * (3.8 + idx * 1.3), seed + 31 * (idx + 1))
        poly = [(float(i * 2), float(curve[i])) for i in range(len(xs))]
        poly.extend([(float(width), float(height)), (0.0, float(height))])

        layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer)
        ld.polygon(poly, fill=(*rcol, 242))
        img = Image.alpha_composite(img, layer)

        # Soft valley mist strip between mountain layers
        if idx < len(ridge_cols) - 1:
            mist_y = base_y + height * 0.03
            mist = np.exp(-((y - mist_y) ** 2) / ((height * 0.055) ** 2)) * 0.24
            mist_arr = np.zeros((height, width, 4), dtype=np.uint8)
            mist_arr[..., 0] = min(255, int(accent[0] * 0.55 + 95))
            mist_arr[..., 1] = min(255, int(accent[1] * 0.55 + 95))
            mist_arr[..., 2] = min(255, int(accent[2] * 0.65 + 110))
            mist_arr[..., 3] = np.clip(mist * 255.0, 0, 255).astype(np.uint8)
            img = Image.alpha_composite(img, Image.fromarray(mist_arr, "RGBA"))

    # Foreground ink-wash sakura boughs & luminous blossom clusters
    bough_layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bough_layer)

    def draw_bough(bx: float, by: float, angle: float, length: float, depth: int) -> None:
        ex = bx + math.cos(angle) * length
        ey = by + math.sin(angle) * length
        mx = (bx + ex) * 0.5 + math.sin(angle * 2.3 + depth) * length * 0.08
        my = (by + ey) * 0.5 - math.cos(angle * 1.9 + depth) * length * 0.08
        w_line = max(1, int((1.5 + depth * 2.6) * (min(width, height) / 1200.0)))
        bark = (32, 22, 38, 245)
        bd.line([(bx, by), (mx, my), (ex, ey)], fill=bark, width=w_line, joint="curve")
        if depth > 0:
            draw_bough(ex, ey, angle - 0.34 + float(rng.uniform(-0.08, 0.08)), length * 0.74, depth - 1)
            draw_bough(ex, ey, angle + 0.48 + float(rng.uniform(-0.08, 0.08)), length * 0.63, depth - 1)
        if depth <= 2:
            cluster_n = 4 if depth < 2 else 2
            for c_i in range(cluster_n):
                fx = ex + float(rng.normal(0.0, min(width, height) * 0.018))
                fy = ey + float(rng.normal(0.0, min(width, height) * 0.015))
                fsize = min(width, height) * float(rng.uniform(0.008, 0.017))
                # Soft blossom glow
                bd.ellipse(
                    [fx - fsize * 2.1, fy - fsize * 2.1, fx + fsize * 2.1, fy + fsize * 2.1],
                    fill=(int(accent[0]), int(accent[1]), int(accent[2]), 44),
                )
                for p_i in range(5):
                    pa = p_i * math.tau / 5.0 + c_i * 0.5
                    px = fx + math.cos(pa) * fsize * 0.72
                    py = fy + math.sin(pa) * fsize * 0.72
                    pcol = (
                        min(255, int(accent[0] * 0.45 + 145)),
                        min(255, int(accent[1] * 0.42 + 132)),
                        min(255, int(accent[2] * 0.45 + 145)),
                        228,
                    )
                    bd.ellipse([px - fsize * 0.58, py - fsize * 0.58, px + fsize * 0.58, py + fsize * 0.58], fill=pcol)
                bd.ellipse([fx - fsize * 0.22, fy - fsize * 0.22, fx + fsize * 0.22, fy + fsize * 0.22],
                           fill=(255, 236, 192, 245))

    draw_bough(width * 1.04, height * 0.06, 2.72, min(width, height) * 0.30, 4)
    draw_bough(-width * 0.03, height * 0.20, 0.28, min(width, height) * 0.23, 3)
    img = Image.alpha_composite(img, bough_layer)

    # Floating wind-swept petals & golden bokeh
    fx_layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    fxd = ImageDraw.Draw(fx_layer)
    for _ in range(38):
        bx = float(rng.uniform(0.05, 0.95)) * width
        by = float(rng.uniform(0.08, 0.92)) * height
        br = min(width, height) * float(rng.uniform(0.006, 0.032))
        is_gold = rng.random() < 0.38
        col = (*[int(c) for c in (gold if is_gold else accent)], int(rng.uniform(22, 72)))
        fxd.ellipse([bx - br, by - br, bx + br, by + br], fill=col)
    fx_layer = fx_layer.filter(ImageFilter.GaussianBlur(radius=max(2, width // 260)))
    img = Image.alpha_composite(img, fx_layer)

    if with_typography is not None:
        typo_layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        td = ImageDraw.Draw(typo_layer)

        # Lower frosted gradient scrim for crisp cover legibility
        scrim = np.zeros((height, width, 4), dtype=np.uint8)
        sy = np.clip((ny - 0.56) / 0.44, 0.0, 1.0)
        scrim[..., 0] = 10
        scrim[..., 1] = 12
        scrim[..., 2] = 24
        scrim[..., 3] = np.clip((sy ** 1.35) * 198.0, 0, 255).astype(np.uint8)
        typo_layer = Image.alpha_composite(typo_layer, Image.fromarray(scrim, "RGBA"))
        td = ImageDraw.Draw(typo_layer)

        # Fine gilded framing border
        pad = int(width * 0.042)
        td.rectangle([pad, pad, width - pad, height - pad],
                     outline=(int(gold[0]), int(gold[1]), int(gold[2]), 118), width=2)
        inner_pad = pad + 10
        td.rectangle([inner_pad, inner_pad, width - inner_pad, height - inner_pad],
                     outline=(255, 255, 255, 38), width=1)

        f_tag = load_font(bold=False, size=int(width * 0.022))
        f_idx = load_font(bold=True, size=int(width * 0.082))
        f_title = load_font(bold=True, size=int(width * 0.048))
        f_sub = load_font(bold=False, size=int(width * 0.023))

        left = int(width * 0.078)
        td.text((left, int(height * 0.072)), with_typography["series"],
                font=f_tag, fill=(int(gold[0]), int(gold[1]), int(gold[2]), 225))
        td.text((left, int(height * 0.725)), with_typography["index"],
                font=f_idx, fill=(int(accent[0]), int(accent[1]), int(accent[2]), 210))
        td.line([(left, int(height * 0.838)), (int(width * 0.46), int(height * 0.838))],
                fill=(int(gold[0]), int(gold[1]), int(gold[2]), 175), width=2)
        td.text((left, int(height * 0.852)), with_typography["title"],
                font=f_title, fill=(248, 243, 248, 252))
        td.text((left, int(height * 0.916)), with_typography["subtitle"],
                font=f_sub, fill=(196, 190, 214, 215))

        img = Image.alpha_composite(img, typo_layer)

    return img


def build_backgrounds_and_covers() -> None:
    # 1920x1080 default moonlit ink-sakura menu/gameplay backdrop
    base_palette = {
        "sky_top": (9, 12, 26),
        "sky_mid": (22, 25, 50),
        "horizon": (58, 36, 68),
        "accent": (242, 158, 192),
        "gold": (250, 216, 157),
        "moon_x": 0.68,
        "moon_y": 0.33,
        "moon_r": 0.20,
        "ridges": [(28, 26, 48), (20, 19, 37), (13, 13, 26)],
    }
    bg_img = render_ink_sakura_artwork(1920, 1080, base_palette, seed=202601, with_typography=None, geometric_motif="moon")
    bg_img.convert("RGB").save(BG_DIR / "moonlit_sakura_bg.png", optimize=True)

    # Default fallback cover (1200x1200)
    default_cover = render_ink_sakura_artwork(
        1200, 1200, base_palette, seed=202699,
        with_typography={
            "series": "SAKURA-樱  ·  LUMINOUS ANTHOLOGY",
            "index": "🌸",
            "title": "Moonlit Reverie",
            "subtitle": "Sakura Soundworks",
        },
        geometric_motif="moon",
    )
    default_cover.convert("RGB").save(COVERS_DIR / "default_cover.png", optimize=True)

    # All 7 built-in song covers (1200x1200 high-DPI)
    song_specs = [
        (
            "tutorial_song", 1001, "clockwork",
            {
                "sky_top": (10, 20, 32), "sky_mid": (20, 42, 58), "horizon": (48, 84, 92),
                "accent": (148, 224, 204), "gold": (246, 222, 168),
                "moon_x": 0.64, "moon_y": 0.37, "moon_r": 0.21,
                "ridges": [(24, 42, 54), (17, 31, 42), (11, 21, 30)],
            },
            {"series": "SAKURA  ·  PRACTICE PRELUDE", "index": "00", "title": "First Steps", "subtitle": "100 BPM  ·  Sakura Soundworks"},
        ),
        (
            "spring_breeze", 1002, "moon",
            {
                "sky_top": (11, 20, 36), "sky_mid": (24, 44, 68), "horizon": (64, 96, 112),
                "accent": (134, 214, 218), "gold": (250, 220, 168),
                "moon_x": 0.62, "moon_y": 0.38, "moon_r": 0.22,
                "ridges": [(26, 40, 58), (19, 30, 45), (12, 20, 32)],
            },
            {"series": "SAKURA ORIGINALS  ·  VOL. 01", "index": "01", "title": "Spring Breeze", "subtitle": "130 BPM  ·  Sakura Soundworks"},
        ),
        (
            "cherry_blossom", 1003, "moon",
            {
                "sky_top": (14, 13, 30), "sky_mid": (34, 24, 54), "horizon": (88, 48, 82),
                "accent": (246, 162, 196), "gold": (252, 220, 166),
                "moon_x": 0.60, "moon_y": 0.36, "moon_r": 0.23,
                "ridges": [(36, 25, 48), (25, 18, 36), (16, 12, 25)],
            },
            {"series": "SAKURA ORIGINALS  ·  VOL. 01", "index": "02", "title": "Cherry Blossom", "subtitle": "155 BPM  ·  Sakura Soundworks"},
        ),
        (
            "digital_dream", 1004, "prism",
            {
                "sky_top": (8, 12, 30), "sky_mid": (18, 28, 66), "horizon": (48, 56, 118),
                "accent": (138, 182, 255), "gold": (216, 232, 255),
                "moon_x": 0.58, "moon_y": 0.37, "moon_r": 0.21,
                "ridges": [(22, 28, 58), (15, 20, 44), (10, 14, 30)],
            },
            {"series": "SAKURA ORIGINALS  ·  VOL. 01", "index": "03", "title": "Digital Dream", "subtitle": "175 BPM  ·  Sakura Soundworks"},
        ),
        (
            "sakura_storm", 1005, "vortex",
            {
                "sky_top": (18, 10, 26), "sky_mid": (46, 20, 48), "horizon": (112, 42, 78),
                "accent": (255, 128, 176), "gold": (255, 214, 152),
                "moon_x": 0.61, "moon_y": 0.35, "moon_r": 0.235,
                "ridges": [(42, 20, 38), (29, 14, 28), (18, 9, 19)],
            },
            {"series": "SAKURA ORIGINALS  ·  VOL. 01", "index": "04", "title": "Sakura Storm", "subtitle": "200 BPM  ·  Sakura Soundworks"},
        ),
        (
            "test-song", 1006, "clockwork",
            {
                "sky_top": (13, 14, 30), "sky_mid": (29, 28, 58), "horizon": (66, 56, 98),
                "accent": (192, 172, 242), "gold": (246, 218, 168),
                "moon_x": 0.63, "moon_y": 0.37, "moon_r": 0.205,
                "ridges": [(30, 27, 50), (21, 19, 38), (14, 13, 26)],
            },
            {"series": "SAKURA  ·  PRACTICE GARDEN", "index": "05", "title": "Timing Garden", "subtitle": "120 BPM  ·  Sakura Soundworks"},
        ),
        (
            "sakura_breath", 1007, "prism",
            {
                "sky_top": (12, 14, 32), "sky_mid": (32, 26, 62), "horizon": (92, 54, 96),
                "accent": (252, 168, 210), "gold": (252, 224, 174),
                "moon_x": 0.59, "moon_y": 0.35, "moon_r": 0.225,
                "ridges": [(34, 26, 52), (24, 18, 39), (15, 12, 27)],
            },
            {"series": "SAKURA ORIGINALS  ·  VOL. 02", "index": "06", "title": "樱落呼吸", "subtitle": "178 BPM  ·  Sakura Soundworks"},
        ),
    ]

    for folder_name, seed, motif, pal, typo in song_specs:
        chart_folder = CHARTS_DIR / folder_name
        if not chart_folder.exists():
            continue
        art = render_ink_sakura_artwork(1200, 1200, pal, seed=seed, with_typography=typo, geometric_motif=motif)
        art.convert("RGB").save(chart_folder / "cover.png", optimize=True)


def main() -> None:
    ensure_dirs()
    build_radial_glow()
    build_anamorphic_streak()
    build_bokeh_disc()
    build_starburst()
    build_shockwave_ring()
    build_sakura_petals()
    build_sakura_crest()
    build_rank_emblems()
    build_backgrounds_and_covers()
    print("Generated Sakura Luminous Glass & Moonlit Ink-Sakura assets successfully.")


if __name__ == "__main__":
    main()
