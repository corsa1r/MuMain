#!/usr/bin/env python3
"""
make_diablo4_lut.py — bake a Diablo-4-inspired color grade into a .cube 3D LUT.

A .cube LUT is an "answer key" for color: for a grid of input RGB values it
stores the graded output. A LutPass in the engine samples it per pixel (3D
texture lookup + hardware interpolation), so the whole grade becomes one texture
fetch. This script computes that grid by running a fixed color transform over
every cell.

The transform here APPROXIMATES Diablo 4's art direction from general knowledge
(muted/desaturated, cool teal shadows, muted amber highlights, mild gothic
contrast, lifted cool-grey blacks, slightly darker overall). It is NOT a
reference-matched grade — expect to eye-tune the constants below.

Output: standard Adobe/Resolve .cube, LUT_3D_SIZE 33, red varying fastest.
A LUT with the IDENTITY transform (no grade) would reproduce the input exactly;
swapping in that identity is the way to verify the loader is wired correctly.
"""

SIZE = 33  # 33^3 = 35937 entries; common high-quality grade size


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def grade(r, g, b):
    # Rec.709-ish luminance (matches the engine's ColorGrade pass weights).
    lum = 0.299 * r + 0.587 * g + 0.114 * b

    # 1) Desaturate toward luminance — D4 reads muted / painterly, never candy.
    sat = 0.70
    r = lum + (r - lum) * sat
    g = lum + (g - lum) * sat
    b = lum + (b - lum) * sat

    # 2) Split-tone: cool teal-blue in shadows, muted amber in highlights.
    #    Weighted by where the pixel sits on the tonal range.
    ws = (1.0 - lum) ** 1.4   # shadow weight
    wh = lum ** 1.4           # highlight weight
    # shadow tint (pull red down, push green/blue up -> teal)
    r += -0.020 * ws
    g += 0.040 * ws
    b += 0.100 * ws
    # highlight tint (amber: up red, slight green, down blue)
    r += 0.080 * wh
    g += 0.040 * wh
    b += -0.040 * wh

    # 3) Gentle contrast S-curve about a slightly low pivot (gothic mood).
    pivot = 0.42
    contrast = 1.12
    r = (r - pivot) * contrast + pivot
    g = (g - pivot) * contrast + pivot
    b = (b - pivot) * contrast + pivot

    # 4) Lift blacks toward a cool grey (filmic, not crushed to pure black) and
    #    darken the whole image a touch.
    lift = 0.015
    r = r * 0.97 + lift * 0.6
    g = g * 0.97 + lift * 0.8
    b = b * 0.97 + lift * 1.0

    return clamp01(r), clamp01(g), clamp01(b)


def main():
    out = []
    out.append('TITLE "Diablo4-Inspired Grimdark"')
    out.append(f"LUT_3D_SIZE {SIZE}")
    out.append("DOMAIN_MIN 0.0 0.0 0.0")
    out.append("DOMAIN_MAX 1.0 1.0 1.0")
    # .cube ordering: red varies fastest, then green, then blue.
    denom = SIZE - 1
    for bi in range(SIZE):
        for gi in range(SIZE):
            for ri in range(SIZE):
                r = ri / denom
                g = gi / denom
                b = bi / denom
                gr, gg, gb = grade(r, g, b)
                out.append(f"{gr:.6f} {gg:.6f} {gb:.6f}")

    import os
    dst = os.path.join(os.path.dirname(__file__), "..", "src", "bin", "Data",
                       "PostProcess", "diablo4.cube")
    dst = os.path.normpath(dst)
    with open(dst, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {dst}  ({len(out)} lines, LUT_3D_SIZE={SIZE})")


if __name__ == "__main__":
    main()
