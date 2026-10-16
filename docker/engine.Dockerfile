# Headless Linux engine for the kind smoke test and, later, Tier 2 edge nodes (deferred: spec Appendix D).
# Built with the linux-headless preset: plant I/O only, no OpenCV, no CAD kernel.
# Dependencies come from apt plus cmake/deps.cmake (FetchContent, pinned tags) — no vcpkg.
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates \
      libgtest-dev libgmock-dev libbenchmark-dev nlohmann-json3-dev libspdlog-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake/ cmake/
COPY third_party/ third_party/
COPY contracts/ contracts/
COPY perception/ perception/
COPY procedure/ procedure/
COPY ipc/ ipc/
COPY control-app/ control-app/
COPY softplc/ softplc/
RUN cmake --preset linux-headless && cmake --build --preset linux-headless --target torquesense softplc

FROM ubuntu:24.04
RUN useradd --system --uid 10001 ts
COPY --from=build /src/build/linux-headless/control-app/torquesense /usr/local/bin/
COPY --from=build /src/build/linux-headless/softplc/softplc       /usr/local/bin/
COPY tests/fixtures/detections/ /opt/torquesense/fixtures/
USER 10001
ENTRYPOINT ["/usr/local/bin/torquesense"]
