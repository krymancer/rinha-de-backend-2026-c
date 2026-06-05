# Build the C binaries + bake the IVF index, then assemble a scratch image.
FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc make curl ca-certificates libc6-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /build

# Fetch the official references first (fixed for the edition) so source edits
# don't bust this layer.
ARG REFS_URL=https://raw.githubusercontent.com/zanfranceschi/rinha-de-backend-2026/main/resources/references.json.gz
RUN curl -fsSL "${REFS_URL}" -o refs.json.gz && gunzip refs.json.gz

COPY Makefile ./
COPY src ./src
RUN make

# Bake the IVF index (k-means) into the image.
ARG N_CLUSTERS=2048
ARG KMEANS_ITERS=12
RUN ./bin/indexer refs.json /build/index.bin ${N_CLUSTERS} ${KMEANS_ITERS} && rm -f refs.json

FROM scratch
COPY --from=builder /build/bin/lb /lb
COPY --from=builder /build/bin/server /server
COPY --from=builder /build/index.bin /index.bin
