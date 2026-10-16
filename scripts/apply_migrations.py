"""Apply supabase/migrations/*.sql to the project in DATABASE_URL, in order, each in one transaction,
and record them where the Supabase CLI records them — so a later `npx supabase db push` sees them as
applied and `npx supabase db diff` stays clean (spec Section 29). For machines where the CLI cannot
log in non-interactively; `db push` remains the normal path.

    python scripts/apply_migrations.py --env-file .env [--dry-run]
"""
from __future__ import annotations
import argparse, pathlib, sys

import psycopg

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_env(path: pathlib.Path) -> dict[str, str]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env-file", type=pathlib.Path, default=ROOT / ".env")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    url = read_env(a.env_file)["DATABASE_URL"]
    files = sorted((ROOT / "supabase" / "migrations").glob("*.sql"))
    with psycopg.connect(url, autocommit=True, prepare_threshold=None) as c:
        c.execute("create schema if not exists supabase_migrations")
        c.execute("create table if not exists supabase_migrations.schema_migrations "
                  "(version text primary key, statements text[], name text)")
        done = {r[0] for r in c.execute("select version from supabase_migrations.schema_migrations")}
        for f in files:
            version, name = f.stem.split("_", 1)
            if version in done:
                print(f"already applied  {f.name}")
                continue
            sql = f.read_text(encoding="utf-8")
            if a.dry_run:
                print(f"would apply      {f.name}")
                continue
            with c.transaction():
                c.execute(sql)
                c.execute("insert into supabase_migrations.schema_migrations (version, statements, name) values (%s, %s, %s)",
                          (version, [sql], name))
            print(f"applied          {f.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
