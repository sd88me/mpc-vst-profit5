/* Patch import: u-he Repro-5 .h2p text patches (loose files or inside .zip) -> Profit-5 panel values.
 * Pure code, no I/O, no allocation. The DEFLATE decoder and the zip walk are adapted from TinyPro5 (MIT, (c) 2026 Mathew Taylor,
 * github.com/fleshinachair/TinyPro5, src/h2p.c), itself after Mark Adler's public-domain "puff". The key table follows the same
 * file format; the mapping onto Profit-5's controls is our own (src/p5_import.c). Files are the user's own: none ship with the plugin. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "p5_engine.h"

typedef struct {
    float v[P5P_COUNT];     /* panel values 0..1 (engine units) */
    uint64_t mask;          /* bit i set: v[i] is part of the patch; the others (master tune, volume, voices, quality, aftertouch) stay as they are */
    char name[28];
} p5_import_t;

/* One patch from the text of a .h2p. Returns 1 for a Repro-5 patch with at least one recognised key, else 0 (another instrument, empty, binary). */
int p5_import_h2p(const char *text, size_t len, p5_import_t *out);
void p5_import_init(p5_import_t *out);          /* the neutral patch, named "Init" (an unused program slot) */

/* Calls cb(entry name, data, len, user) for every file in a zip whose name ends in `suffix` (case-insensitive) and unpacks to at most scratch_cap bytes.
 * cb returns non-zero to stop. Returns the number of files delivered, or < 0 for a damaged / unsupported archive (encrypted, zip64). */
typedef int (*p5_zip_cb)(const char *name, const uint8_t *data, size_t len, void *user);
int p5_zip_walk(const uint8_t *zip, size_t zlen, const char *suffix, uint8_t *scratch, size_t scratch_cap, p5_zip_cb cb, void *user);
int p5_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen);
