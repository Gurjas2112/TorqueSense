# Static Go binary on distroless *static*. On a station the agent reads the GPU from the node's
# dcgm-exporter over HTTP (-gpu-source=dcgm), so nothing in this image needs glibc or nvidia-smi.
FROM golang:1.26 AS build
WORKDIR /src
COPY fleet-agent/go.mod ./
COPY fleet-agent/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/fleet-agent .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/fleet-agent /fleet-agent
ENTRYPOINT ["/fleet-agent"]
