"""Serve the built dashboard (web/dist) from the API, so a release needs no Vite dev server.

    from api.spa import mount_spa
    mount_spa(app, Path(release_root) / "web" / "dist")     # after every API route is registered

Real files are served as they are, with long caching for Vite's hashed assets. Any other GET that is
not an API route returns index.html, so a deep link such as /hmi/W-14 survives a kiosk reload.
API paths that do not exist still return 404 as JSON: the fallback never hides a wrong URL.
"""
from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse

API_PREFIXES = ("/v1/", "/health", "/docs", "/openapi.json")


def mount_spa(app: FastAPI, dist: Path) -> None:
    dist = dist.resolve()
    index = dist / "index.html"
    if not index.is_file():
        raise RuntimeError(f"{index} not found: build the dashboard (npm run build) into the release")

    @app.get("/{path:path}", include_in_schema=False)
    async def spa(path: str, request: Request):
        if request.url.path.startswith(API_PREFIXES):
            raise HTTPException(status_code=404)
        target = (dist / path).resolve()
        if path and target.is_file() and target.is_relative_to(dist):   # no ../ escapes
            immutable = target.parent.name == "assets"                   # Vite content-hashes these
            return FileResponse(target, headers={"Cache-Control": "public, max-age=31536000, immutable"
                                                 if immutable else "no-cache"})
        return FileResponse(index, headers={"Cache-Control": "no-cache"})
