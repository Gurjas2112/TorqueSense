"""Tightening-order solvers. Each takes fastener positions in the part's own plane (mm) and
returns indices in fastening order. Chosen by the compiler from the pattern's geometry, so the
order is derived, never typed."""
from __future__ import annotations
import math

def _angle(p, c):
    return math.atan2(p[1] - c[1], p[0] - c[0]) % (2 * math.pi)

def centroid(pts):
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

def classify(pts, tol=0.05):
    """circle: all points equidistant from the centroid (bolt circle, flange);
    line: collinear (rail, seam); field: anything else (cover, cylinder head, panel)."""
    c = centroid(pts)
    r = [math.dist(p, c) for p in pts]
    if len(pts) >= 3 and max(r) > 0 and (max(r) - min(r)) / max(r) < tol:
        return "circle"
    if len(pts) >= 3:
        (x0, y0), (x1, y1) = pts[0], max(pts, key=lambda p: math.dist(p, pts[0]))
        L = math.dist((x0, y0), (x1, y1))
        if L > 0 and all(abs((x1 - x0) * (y0 - p[1]) - (x0 - p[0]) * (y1 - y0)) / L < tol * L for p in pts):
            return "line"
    return "field"

def star(pts):
    """Bolt circle. Odd N: stride floor(N/2), one cycle. Even N: diametric pairs, pair starts
    strided by N/4 with fallback to the first unvisited start."""
    c = centroid(pts)
    by_angle = sorted(range(len(pts)), key=lambda i: _angle(pts[i], c))
    k = by_angle.index(0)                          # start at the first member…
    by_angle = by_angle[k:] + by_angle[:k]
    if len(by_angle) > 2 and by_angle[-1] < by_angle[1]:
        by_angle = [by_angle[0]] + by_angle[1:][::-1]   # …and run in the numbering's direction
    n = len(pts)
    if n % 2:
        return [by_angle[(k * (n // 2)) % n] for k in range(n)]
    half, step, seen, out, s = n // 2, max(1, n // 4), [False] * (n // 2), [], 0
    for _ in range(half):
        if seen[s]:
            s = seen.index(False)
        seen[s] = True
        out += [by_angle[s], by_angle[s + half]]
        s = (s + step) % half
    return out

def center_out(pts):
    """Line or field: start nearest the centroid and work outward, alternating sides, so
    clamp load spreads from the middle and the part is never pinned at one end first."""
    c = centroid(pts)
    rest = sorted(range(len(pts)), key=lambda i: (round(math.dist(pts[i], c), 6), _angle(pts[i], c)))
    out = [rest.pop(0)]
    while rest:
        last = pts[out[-1]]
        # nearest the centre first; among those, the point most nearly opposite the last one
        def cos_to_last(p):
            a, b = (p[0] - c[0], p[1] - c[1]), (last[0] - c[0], last[1] - c[1])
            na, nb = math.hypot(*a), math.hypot(*b)
            return 0.0 if na == 0 or nb == 0 else (a[0] * b[0] + a[1] * b[1]) / (na * nb)
        def key(i):
            p = pts[i]
            return (round(math.dist(p, c), 6), round(cos_to_last(p), 6), _angle(p, c))
        nxt = min(rest, key=key)
        rest.remove(nxt)
        out.append(nxt)
    return out

def solve(pts):
    kind = classify(pts)
    return kind, (star(pts) if kind == "circle" else center_out(pts))
