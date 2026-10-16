"""Printable fixtures for the desk samples and for calibration, drawn at exact scale.

    python samples/_print/make_prints.py

Print every PDF at 100 % ("Actual size", never "Fit to page") and check the 50 mm scale bar
with a ruler before using a sheet. Registration markers use DICT_4X4_50; the ChArUco board uses
DICT_5X5_100 so that its markers can never be mistaken for a registration marker."""
import pathlib
import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

DPI = 300
HERE = pathlib.Path(__file__).resolve().parent
def px(mm): return int(round(mm / 25.4 * DPI))

def page(w_mm=210, h_mm=297):
    im = Image.new("L", (px(w_mm), px(h_mm)), 255)
    return im, ImageDraw.Draw(im)

def font(mm):
    try:
        return ImageFont.truetype("DejaVuSans.ttf", px(mm))
    except OSError:
        return ImageFont.load_default()

def marker(im, dictionary, marker_id, size_mm, centre):
    d = cv2.aruco.getPredefinedDictionary(dictionary)
    m = cv2.aruco.generateImageMarker(d, marker_id, px(size_mm))
    x, y = px(centre[0] - size_mm / 2), px(centre[1] - size_mm / 2)
    im.paste(Image.fromarray(m), (x, y))

def crosshair(dr, c, arm=12):
    x, y = px(c[0]), px(c[1])
    dr.line([(x - px(arm), y), (x + px(arm), y)], fill=0, width=3)
    dr.line([(x, y - px(arm)), (x, y + px(arm))], fill=0, width=3)
    dr.ellipse([x - px(2), y - px(2), x + px(2), y + px(2)], outline=0, width=3)

def scale_bar(dr, x_mm, y_mm):
    dr.rectangle([px(x_mm), px(y_mm), px(x_mm + 50), px(y_mm + 3)], fill=0)
    dr.text((px(x_mm), px(y_mm + 5)), "50 mm — measure me", fill=0, font=font(3.5))

def save(im, name):
    im.save(HERE / name, "PDF", resolution=DPI)
    print("wrote", name)

def desk_cover_sheet():
    """Place the box centred on the crosshair, edges on the dashed square. The assembly origin is
    the crosshair; +x is right, +y is towards the top of the page (away from the marker)."""
    im, dr = page()
    origin = (105, 115)                                     # page mm, from the top-left corner
    crosshair(dr, origin)
    for i in range(0, 100, 6):                              # dashed 100 mm square
        for (a, b) in [((-50 + i, -50), (-47 + i, -50)), ((-50 + i, 50), (-47 + i, 50)),
                       ((-50, -50 + i), (-50, -47 + i)), ((50, -50 + i), (50, -47 + i))]:
            dr.line([(px(origin[0] + a[0]), px(origin[1] + a[1])), (px(origin[0] + b[0]), px(origin[1] + b[1]))], fill=0, width=3)
    for n, (x, y) in enumerate([(-40, 40), (40, 40), (40, -40), (-40, -40)], 1):
        cx, cy = origin[0] + x, origin[1] - y                   # assembly +y is page-up
        dr.ellipse([px(cx - 3.5), px(cy - 3.5), px(cx + 3.5), px(cy + 3.5)], outline=0, width=2)
        dr.text((px(cx + 5), px(cy - 2)), str(n), fill=0, font=font(5))
    marker(im, cv2.aruco.DICT_4X4_50, 0, 40, (origin[0], origin[1] + 90))   # centre_mm [0, -90, 0]
    f = font(4)
    dr.text((px(15), px(15)), "desk-cover-4screw · placement sheet", fill=0, font=font(6))
    dr.text((px(15), px(25)), "Box centred on the crosshair · screws numbered clockwise from top-left", fill=0, font=f)
    dr.text((px(15), px(250)), "Marker DICT_4X4_50 id 0, 40 mm, centre 90 mm below the crosshair", fill=0, font=f)
    scale_bar(dr, 15, 265)
    save(im, "placement_sheet_desk_cover.pdf")

def desk_harness_sheet():
    """The clip board is wider than A4: cut out the marker and stick it on the board so that its
    centre is 150 mm left of and 60 mm below the board's crosshair (draw that with a ruler)."""
    im, dr = page()
    marker(im, cv2.aruco.DICT_4X4_50, 1, 40, (105, 90))
    dr.rectangle([px(80), px(65), px(130), px(115)], outline=0, width=2)    # cut line
    dr.text((px(15), px(15)), "desk-harness-clips · registration marker", fill=0, font=font(6))
    dr.text((px(15), px(130)), "Cut along the box. Stick it with its centre 150 mm LEFT and 60 mm BELOW", fill=0, font=font(4))
    dr.text((px(15), px(137)), "the crosshair you draw on the board. Marker DICT_4X4_50 id 1, 40 mm.", fill=0, font=font(4))
    scale_bar(dr, 15, 150)
    save(im, "marker_desk_harness.pdf")

def charuco_board():
    """Calibration target for scripts/calibrate.py: 7 x 5 squares of 30 mm, markers 22 mm."""
    d = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_5X5_100)
    board = cv2.aruco.CharucoBoard((7, 5), 0.030, 0.022, d)
    img = board.generateImage((px(210), px(150)), marginSize=0)
    im, dr = page(297, 210)
    im.paste(Image.fromarray(img), (px(43.5), px(20)))
    dr.text((px(15), px(175)), "ChArUco 7×5 · square 30 mm · marker 22 mm · DICT_5X5_100 — glue flat to card", fill=0, font=font(4))
    scale_bar(dr, 15, 185)
    save(im, "charuco_7x5_30mm_a4.pdf")

if __name__ == "__main__":
    desk_cover_sheet(); desk_harness_sheet(); charuco_board()
