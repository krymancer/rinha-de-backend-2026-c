# Rinha de Backend 2026 — C

Fraud-detection vector-search API in C. IVF (int16 x10000, exact) nearest-neighbour
search with an AVX2 `vpmaddwd` pair-SoA distance kernel, a SCM_RIGHTS fd-passing load
balancer, and warm-core epoll API workers. Port of the krymancer-zig submission.

`GET /ready`, `POST /fraud-score` on :9999. `docker compose up --build`.

MIT.
