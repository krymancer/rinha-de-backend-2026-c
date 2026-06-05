// Times vectorize+search per request over a payload file (test-data.json) AND
// validates the APPROVED decision (the only thing the grader checks) against the
// ground-truth expected_approved, at a given probe budget, vs a full exact scan.
//   measure <index.bin> <test-data.json> [init_probe] [max_probe]
// Ported 1:1 from measure.zig. This is the E=0 validator.

#include "rinha.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// Returns the index just past the matching '}' for the '{' at `start` (string
// aware, backslash-escape aware). Falls back to buf.len on imbalance.
static size_t brace_end(const uint8_t *buf, size_t len, size_t start) {
    size_t i = start;
    int depth = 0;
    bool in_str = false;
    for (; i < len; i++) {
        uint8_t c = buf[i];
        if (in_str) {
            if (c == '\\') i += 1;
            else if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{') {
            depth += 1;
        } else if (c == '}') {
            depth -= 1;
            if (depth == 0) return i + 1;
        }
    }
    return len;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t pct(const uint64_t *s, size_t n, double p) {
    if (n == 0) return 0;
    return s[(size_t)(p * (double)(n - 1))];
}

// memmem-like search for `needle` in buf[pos..len]. Returns offset or SIZE_MAX.
static size_t index_of_pos(const uint8_t *buf, size_t len, size_t pos,
                           const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0 || nl > len) return SIZE_MAX;
    void *p = memmem(buf + pos, len - pos, needle, nl);
    if (!p) return SIZE_MAX;
    return (size_t)((const uint8_t *)p - buf);
}

// Read an entire file into a malloc'd buffer; sets *out_len. Returns NULL on err.
static uint8_t *read_file(const char *path, size_t *out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (end < 0) { close(fd); return NULL; }
    size_t size = (size_t)end;
    uint8_t *buf = (uint8_t *)malloc(size ? size : 1);
    if (!buf) { close(fd); return NULL; }
    size_t off = 0;
    while (off < size) {
        ssize_t rc = read(fd, buf + off, size - off);
        if (rc <= 0) break;
        off += (size_t)rc;
    }
    close(fd);
    if (off != size) { free(buf); return NULL; }
    *out_len = size;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: measure <index.bin> <test-data.json> [init_probe] [max_probe]\n");
        return 1;
    }
    const char *idx_path = argv[1];
    const char *data_path = argv[2];
    size_t ip = (argc > 3) ? (size_t)strtoul(argv[3], NULL, 10) : 24;
    size_t mp = (argc > 4) ? (size_t)strtoul(argv[4], NULL, 10) : 96;

    ivf_index_t index;
    if (ivf_map(idx_path, &index, true) != 0) {
        fprintf(stderr, "measure: ivf_map(%s) failed\n", idx_path);
        return 1;
    }

    size_t vlen = 0;
    uint8_t *vbuf = read_file(data_path, &vlen);
    if (!vbuf) {
        fprintf(stderr, "measure: read %s failed\n", data_path);
        return 1;
    }

    uint64_t *keys = (uint64_t *)malloc(sizeof(uint64_t) * (index.n_clusters ? index.n_clusters : 1));
    if (!keys) { fprintf(stderr, "measure: oom keys\n"); return 1; }

    // Dynamic stat arrays.
    size_t cap = 1024, ns = 0;
    uint64_t *slat = (uint64_t *)malloc(sizeof(uint64_t) * cap);
    uint64_t *tlat = (uint64_t *)malloc(sizeof(uint64_t) * cap);
    uint64_t *probes = (uint64_t *)malloc(sizeof(uint64_t) * cap);
    if (!slat || !tlat || !probes) { fprintf(stderr, "measure: oom\n"); return 1; }

    size_t total = 0;
    // approx (ip,mp) vs ground truth
    size_t fp = 0;   // legit denied (expected approve, we reject)
    size_t fn_ = 0;  // fraud approved (expected reject, we approve)
    // exact full-scan vs ground truth (sanity)
    size_t efp = 0;
    size_t efn = 0;
    // approx vs exact (does the probe budget ever flip the approved bit?)
    size_t flips = 0;
    size_t fc_mism = 0; // fraud_count divergence (informational)

    const char *REQ = "\"request\":";
    const char *EA = "\"expected_approved\":";
    size_t REQ_len = strlen(REQ);
    size_t EA_len = strlen(EA);

    size_t pos = 0;
    for (;;) {
        size_t rs = index_of_pos(vbuf, vlen, pos, REQ);
        if (rs == SIZE_MAX) break;
        size_t p = rs + REQ_len;
        while (p < vlen && (vbuf[p] == ' ' || vbuf[p] == '\n')) p += 1;
        if (p >= vlen || vbuf[p] != '{') break;
        size_t req_end = brace_end(vbuf, vlen, p);
        const uint8_t *req = vbuf + p;
        size_t req_sz = req_end - p;
        size_t ea = index_of_pos(vbuf, vlen, req_end, EA);
        if (ea == SIZE_MAX) break;
        bool expected = vbuf[ea + EA_len] == 't';
        pos = ea + EA_len;
        total += 1;

        long t0 = now_ns();
        int16_t qv[VPAD];
        if (!vectorize(req, req_sz, qv)) {
            // SAFE -> approved=true; mirrors measure.zig literally.
            if (expected) fp += 1;
            continue;
        }
        long t1 = now_ns();
        size_t np = 0;
        result_t res = ivf_search(&index, qv, ip, mp, keys, &np);
        long t2 = now_ns();
        result_t exact = ivf_search(&index, qv, index.n_clusters, index.n_clusters, keys, NULL);

        if (res.approved != expected) {
            if (res.approved) fn_ += 1; else fp += 1;
        }
        if (exact.approved != expected) {
            if (exact.approved) efn += 1; else efp += 1;
        }
        if (res.approved != exact.approved) flips += 1;
        if (res.fraud_count != exact.fraud_count) fc_mism += 1;

        if (ns == cap) {
            cap *= 2;
            slat = (uint64_t *)realloc(slat, sizeof(uint64_t) * cap);
            tlat = (uint64_t *)realloc(tlat, sizeof(uint64_t) * cap);
            probes = (uint64_t *)realloc(probes, sizeof(uint64_t) * cap);
            if (!slat || !tlat || !probes) { fprintf(stderr, "measure: oom\n"); return 1; }
        }
        slat[ns] = (uint64_t)(t2 - t1);
        tlat[ns] = (uint64_t)(t2 - t0);
        probes[ns] = (uint64_t)np;
        ns += 1;
    }

    qsort(slat, ns, sizeof(uint64_t), cmp_u64);
    qsort(tlat, ns, sizeof(uint64_t), cmp_u64);
    qsort(probes, ns, sizeof(uint64_t), cmp_u64);

    size_t n = ns;
    long E_approx = (long)fp * 1 + (long)fn_ * 3;
    long E_exact = (long)efp * 1 + (long)efn * 3;
    uint64_t slat_max = n ? slat[n - 1] : 0;
    uint64_t probes_max = n ? probes[n - 1] : 0;

    printf("ip=%3zu mp=%3zu | search p50=%lluns p99=%lluns max=%lluns | "
           "probes p50=%llu p99=%llu max=%llu | "
           "approx: fp=%zu fn=%zu E=%ld %s | "
           "exact: fp=%zu fn=%zu E=%ld | flips(approx!=exact)=%zu fc_mism=%zu\n",
           ip, mp,
           (unsigned long long)pct(slat, n, 0.5),
           (unsigned long long)pct(slat, n, 0.99),
           (unsigned long long)slat_max,
           (unsigned long long)pct(probes, n, 0.5),
           (unsigned long long)pct(probes, n, 0.99),
           (unsigned long long)probes_max,
           fp, fn_, E_approx, (E_approx == 0) ? "E=0" : "FAIL",
           efp, efn, E_exact, flips, fc_mism);

    (void)total;
    free(vbuf);
    free(keys);
    free(slat);
    free(tlat);
    free(probes);
    rinha_free_index(&index);
    return 0;
}
