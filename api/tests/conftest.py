import pathlib, sys
# the API's modules (outbox, fleet.ewma) import as top-level names, the way `cd api; pytest` sees them,
# so the suite also runs from the repository root as CI runs it: `pytest sidecar/tests api/tests`
sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
