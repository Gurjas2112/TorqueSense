from tsref.capture import A2A1920_51GC as CAM, lens_for, link_mbps, hfov_deg

def test_wheel_lens():
    # hub framed at 900 mm with a 500 mm field; 17 mm bolt-head face
    f, px, field = lens_for(CAM, 900, 500, 17)
    assert f == 8 and px >= 16 and field >= 500           # 8 mm: ~44 px across the head, 745 mm field

def test_door_lens_has_a_narrow_window():
    # three cameras at 420 mm, ~400 mm of trim each; 4 mm clip latch
    f, px, field = lens_for(CAM, 420, 400, 4)
    assert f == 6 and px >= 16 and field >= 400

def test_no_stock_lens_when_the_rig_is_wrong():
    # one camera trying to cover a 1 m door from 1.2 m: the field forces a short lens,
    # the 4 mm latch demands a long one, and no stock focal length satisfies both
    assert lens_for(CAM, 1200, 1000, 4) is None

def test_gige_link_budget_forces_bayer_transfer():
    assert link_mbps(CAM, 30, "BayerRG8") < 1000           # ~580 Mbit/s: fits one GigE link
    assert link_mbps(CAM, 30, "YCbCr422_8") > 1000         # debayering in the camera does not fit
    assert link_mbps(CAM, 30, "RGB8") > 1000
    assert 2 * link_mbps(CAM, 30, "BayerRG8") > 1000       # two cameras cannot share one GigE port

def test_hfov_of_the_chosen_wheel_lens():
    assert 44 < hfov_deg(CAM, 8) < 46

# --- fixed-lens cameras used for the prototype ------------------------------------------------
from tsref.capture import LAPTOP_WEBCAM_720P as WEB, PHONE_1080P as PHONE, feature_px_at, max_distance_mm, field_mm

def test_laptop_webcam_resolves_a_harness_clip_at_400_mm():
    assert feature_px_at(WEB, 400, 8) >= 16                  # ~18 px across an 8 mm clip flap
    assert 450 < max_distance_mm(WEB, 8) < 470               # move it past ~457 mm and it cannot

def test_a_webcam_left_at_its_default_640x480_cannot():
    # OpenCV opens a DirectShow camera at 640x480 unless asked; same lens, half the pixels
    assert feature_px_at(WEB.at(640, 480), 400, 8) < 16

def test_phone_top_down_resolves_cover_screws_even_at_720p():
    assert feature_px_at(PHONE, 350, 7) >= 16                # ~28 px at 1080p
    assert feature_px_at(PHONE.at(1280, 720), 350, 7) >= 16  # ~19 px if Wi-Fi forces 720p

def test_field_at_working_distance_holds_the_desk_samples():
    w, h = field_mm(PHONE, 350)
    assert w >= 200 and h >= 200                             # a 100 mm box plus its marker
    w, h = field_mm(WEB, 400)
    assert w >= 340 and h >= 120                             # the clip board, ±170 mm by ±60 mm
