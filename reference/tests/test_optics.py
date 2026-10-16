from tsref import optics

def wheel_cam(width):
    return optics.Camera("cam-1", width, 70.4, (0, 0, 900), (0, 0, 0))

def test_wheel_crops_must_come_from_the_full_resolution_frame():
    full = optics.footprint(wheel_cam(1920), (0, 57.15, 0), (0, 0, 1), 17)
    down = optics.footprint(wheel_cam(960), (0, 57.15, 0), (0, 0, 1), 17)
    assert full.feature_px >= 16 > down.feature_px             # 25.6 px vs 12.8 px

def test_one_distant_camera_cannot_verify_door_clips():
    cam = optics.Camera("wide", 1920, 50, (500, 0, 1200), (500, 0, 0))
    assert optics.footprint(cam, (0, 0, 0), (0, 0, 1), 4).feature_px < 16

def test_out_of_frame_points_are_not_visible():
    cam = optics.Camera("c", 1920, 50, (0, 0, 420), (0, 0, 0))
    assert not optics.footprint(cam, (400, 0, 0), (0, 0, 1), 10).visible

def test_a_feature_facing_away_is_not_visible():
    cam = optics.Camera("c", 1920, 50, (0, 0, 420), (0, 0, 0))
    assert not optics.footprint(cam, (0, 0, 0), (0, 0, -1), 10).visible
