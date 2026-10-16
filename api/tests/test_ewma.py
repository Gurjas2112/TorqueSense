import random

from fleet.ewma import Chart, State


def feed(chart, xs):
    events = []
    for i, x in enumerate(xs):
        state, entered = chart.update(x)
        if entered:
            events.append(i)
    return events


def baseline(rng, n, mu, sd):
    return [rng.gauss(mu, sd) for _ in range(n)]


def test_in_control_raises_nothing():
    rng = random.Random(7)
    c = Chart("ip_conf_mean", "down", sigma_floor=0.005)
    assert feed(c, baseline(rng, 288 + 5000, 0.92, 0.01)) == []
    assert c.state is State.IN_CONTROL


def test_two_sigma_drop_is_found_quickly():
    rng = random.Random(11)
    c = Chart("ip_conf_mean", "down", sigma_floor=0.005)
    feed(c, baseline(rng, 288 + 200, 0.92, 0.01))
    drifted = [rng.gauss(0.90, 0.01) for _ in range(40)]       # 2-sigma drop
    events = feed(c, drifted)
    assert events and events[0] < 12                            # simulated ARL1 = 4.4


def test_wrong_direction_is_ignored():
    rng = random.Random(13)
    c = Chart("ip_conf_mean", "down", sigma_floor=0.005)
    feed(c, baseline(rng, 288 + 200, 0.92, 0.01))
    assert feed(c, [rng.gauss(0.95, 0.01) for _ in range(200)]) == []  # confidence UP is fine


def test_sigma_floor_stops_alarms_on_a_flat_baseline():
    c = Chart("dropped_frame_rate", "up", sigma_floor=0.001)
    feed(c, [0.0] * 288)                      # MAD = 0 without the floor
    assert c.sigma0 == 0.001
    assert feed(c, [0.0001] * 50) == []       # 0.1 sigma blips: nothing


def test_drift_clears_with_hysteresis_and_can_fire_again():
    rng = random.Random(17)
    c = Chart("evidence_p99_ms", "up", sigma_floor=2.0)
    feed(c, baseline(rng, 288 + 100, 60.0, 3.0))
    assert feed(c, [80.0] * 10)               # fires
    feed(c, [60.0] * 400)                     # long recovery
    assert c.state is State.IN_CONTROL
    assert feed(c, [80.0] * 10)               # fires again: a new episode


def test_reset_on_redeploy_returns_to_baseline():
    rng = random.Random(19)
    c = Chart("ip_conf_mean", "down", sigma_floor=0.005)
    feed(c, baseline(rng, 300, 0.92, 0.01))
    assert c.state is State.IN_CONTROL
    c.reset()
    assert c.state is State.BASELINE and c.mu0 is None


def test_state_round_trip_survives_a_restart():
    import json
    rng = random.Random(23)
    a = Chart("ip_conf_mean", "down", sigma_floor=0.005)
    feed(a, baseline(rng, 150, 0.92, 0.01))                  # mid-baseline
    b = Chart.from_state(json.loads(json.dumps(a.to_state())))
    xs = [rng.gauss(0.92, 0.01) for _ in range(200)] + [0.89] * 30
    assert feed(a, xs) == feed(b, xs)                        # identical behaviour after restore
    assert a.to_state() == b.to_state()
