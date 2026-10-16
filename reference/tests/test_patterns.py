import math, random
from tsref import patterns

def circle(n, r=57.15):
    return [(r * math.cos(math.radians(90 - 360 / n * i)), r * math.sin(math.radians(90 - 360 / n * i))) for i in range(n)]

def test_wheel_star_matches_the_documented_sequence():
    kind, order = patterns.solve(circle(5))
    assert kind == "circle" and [i + 1 for i in order] == [1, 3, 5, 2, 4]

def test_star_properties_for_many_n():
    for n in range(4, 13):
        order = patterns.star(circle(n))
        assert sorted(order) == list(range(n))
        if n % 2 == 0:
            for a, b in zip(order[::2], order[1::2]):          # diametric pairs
                assert (a - b) % n == n // 2
        else:
            for a, b in zip(order, order[1:]):                 # stride floor(n/2)
                assert (b - a) % n == n // 2

def test_line_is_center_out_and_alternating():
    pts = [(x, 0.0) for x in range(0, 700, 100)]               # 7 fasteners on a seam
    kind, order = patterns.solve(pts)
    assert kind == "line" and order[0] == 3                     # starts in the middle
    d = [abs(pts[i][0] - 300) for i in order]
    assert d == sorted(d)                                       # never moves back inward
    sides = [pts[i][0] > 300 for i in order[1:]]
    assert all(a != b for a, b in zip(sides, sides[1:]))        # alternates sides

def test_field_is_center_out_with_diagonal_pairs():
    pts = [(x, y) for y in (50, -50) for x in (-100, 0, 100)]   # 2 x 3 cover
    kind, order = patterns.solve(pts)
    assert kind == "field" and [i + 1 for i in order] == [2, 5, 3, 4, 6, 1]

def test_every_solver_returns_a_permutation_on_random_layouts():
    rng = random.Random(5)
    for _ in range(200):
        pts = [(rng.uniform(-200, 200), rng.uniform(-200, 200)) for _ in range(rng.randint(3, 16))]
        _, order = patterns.solve(pts)
        assert sorted(order) == list(range(len(pts)))

def test_an_explicit_order_overrides_derivation_and_is_recorded():
    from tsref import compiler, optics
    cam = optics.Camera("c", 1920, 69, (0, 0, 350), (0, 0, 0))
    m = {"profile": "p", "cameras": [], "groups": [{"id": "s", "family": "threaded_fastener",
         "states": ["absent", "seated", "fastened"], "feature_mm": 7,
         "members": [{"position_mm": [x, y, 0]} for x, y in ((-40, 40), (40, 40), (40, -40), (-40, -40))]}],
         "steps": [{"id": "f", "group": "s", "kind": "sequence", "order": [1, 2, 3, 4], "state": "fastened",
                    "max_duration_s": 30}]}
    prof, _, _ = compiler.compile_profile(m, [cam], 16)
    assert prof["steps"][0]["targets"] == ["s-1", "s-2", "s-3", "s-4"]          # round the box, as written
    assert prof["steps"][0]["order_source"] == "specified" and "pattern" not in prof["steps"][0]
    m["steps"][0]["order"] = "derive"
    prof, _, _ = compiler.compile_profile(m, [cam], 16)
    assert prof["steps"][0]["targets"] == ["s-1", "s-3", "s-2", "s-4"]          # across the diagonals
    assert prof["steps"][0]["order_source"] == "derived"
