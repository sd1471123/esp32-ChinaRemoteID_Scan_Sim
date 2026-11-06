# -*- coding: utf-8 -*-
"""
从一张方图母版生成 Android 全套启动图标（传统 mipmap + 自适应图标）。

用法：
    python tools/make_app_icon.py [--source android/icon/app_icon_source.png]

产物：
    android/app/src/main/res/mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png   48/72/96/144/192
    android/app/src/main/res/mipmap-{...}dpi/ic_launcher_foreground.png    108/162/216/324/432
    android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml
    android/app/src/main/res/values/colors.xml（ic_launcher_background）

要点：
  * 母版是「圆角方图 + 纯黑外框」，先按阈值裁掉黑边，再套圆角 alpha 蒙版；
  * 自适应图标前景里母版只占画布 86%，四周留一点余量，避免系统圆形遮罩
    把柴犬耳朵和机身裁掉；露出的边角由背景纯色（母版天空蓝）补上，看不出接缝；
  * 蒙版按 4 倍超采样绘制再缩小，避免虚边锯齿。
"""
import argparse
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "android", "app", "src", "main", "res")
DEFAULT_SRC = os.path.join(ROOT, "android", "icon", "app_icon_source.png")

# 目录名 -> 传统图标边长(px)
LEGACY = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}
# 目录名 -> 自适应前景边长(px)，108dp
FOREGROUND = {
    "mipmap-mdpi": 108,
    "mipmap-hdpi": 162,
    "mipmap-xhdpi": 216,
    "mipmap-xxhdpi": 324,
    "mipmap-xxxhdpi": 432,
}

CORNER_RATIO = 0.222  # 母版自身的圆角半径占边长比例
# 自适应图标前景里母版占画布的比例。满幅(1.0)会把柴犬耳朵和机身裁掉，
# 缩到 72/108 又太小、四周多出蓝边；0.86 实测主体完整且构图最饱满。
ART_SCALE = 0.86


def load_master(path):
    im = Image.open(path).convert("RGB")
    # 阈值化去掉纯黑外框：亮度 > 30 视为内容
    gray = im.convert("L").point(lambda v: 255 if v > 30 else 0)
    box = gray.getbbox()
    if box:
        im = im.crop(box)
    # 裁成正方形（居中）
    w, h = im.size
    side = min(w, h)
    im = im.crop(((w - side) // 2, (h - side) // 2, (w - side) // 2 + side, (h - side) // 2 + side))
    return im


def corner_bg_color(im):
    """取四角内侧区域的平均色作背景；跳过白云(过亮)与黑边(过暗)。
    该色只在自适应遮罩边角露出一点点，取天空蓝即可无缝衔接。"""
    side = im.size[0]
    pts = []
    for fx, fy in ((0.08, 0.08), (0.92, 0.08), (0.08, 0.92), (0.92, 0.92)):
        x, y = int(side * fx), int(side * fy)
        for dx in range(-10, 11, 2):
            for dy in range(-10, 11, 2):
                p = im.getpixel((min(side - 1, max(0, x + dx)), min(side - 1, max(0, y + dy))))
                if 20 < sum(p) < 690:  # 排除黑边与白云
                    pts.append(p)
    if not pts:
        return (53, 165, 239)
    r = sum(p[0] for p in pts) // len(pts)
    g = sum(p[1] for p in pts) // len(pts)
    b = sum(p[2] for p in pts) // len(pts)
    return (r, g, b)


def rounded(im, radius_ratio=CORNER_RATIO, ss=4):
    """给方图套圆角 alpha 蒙版（外面透明）。"""
    side = im.size[0]
    mask = Image.new("L", (side * ss, side * ss), 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle(
        [0, 0, side * ss - 1, side * ss - 1],
        radius=int(side * radius_ratio * ss),
        fill=255,
    )
    mask = mask.resize((side, side), Image.LANCZOS)
    out = im.convert("RGBA")
    out.putalpha(mask)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=DEFAULT_SRC)
    args = ap.parse_args()

    master = load_master(args.source)
    bg = corner_bg_color(master)
    art = rounded(master)
    print("master %dx%d  bg=#%02X%02X%02X" % (master.size[0], master.size[1], bg[0], bg[1], bg[2]))

    for d, px in LEGACY.items():
        path = os.path.join(RES, d)
        os.makedirs(path, exist_ok=True)
        art.resize((px, px), Image.LANCZOS).save(os.path.join(path, "ic_launcher.png"), optimize=True)
        print("  %-16s ic_launcher.png            %d" % (d, px))

    for d, px in FOREGROUND.items():
        path = os.path.join(RES, d)
        os.makedirs(path, exist_ok=True)
        canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
        inner = max(1, int(round(px * ART_SCALE)))
        a = art.resize((inner, inner), Image.LANCZOS)
        off = (px - inner) // 2
        canvas.alpha_composite(a, (off, off))
        canvas.save(os.path.join(path, "ic_launcher_foreground.png"), optimize=True)
        print("  %-16s ic_launcher_foreground.png %d" % (d, px))

    anydpi = os.path.join(RES, "mipmap-anydpi-v26")
    os.makedirs(anydpi, exist_ok=True)
    with open(os.path.join(anydpi, "ic_launcher.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <background android:drawable="@color/ic_launcher_background" />\n'
            '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
            "</adaptive-icon>\n"
        )

    with open(os.path.join(RES, "values", "colors.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            "<resources>\n"
            '    <color name="ic_launcher_background">#%02X%02X%02X</color>\n'
            "</resources>\n" % bg
        )
    print("done.")


if __name__ == "__main__":
    main()
