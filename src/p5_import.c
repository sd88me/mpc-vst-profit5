/* Patch import: DEFLATE + zip (adapted from TinyPro5, MIT, (c) 2026 Mathew Taylor), the Repro-5 .h2p reader and its mapping onto Profit-5.
 * See p5_import.h. */
#include <math.h>
#include <string.h>
#include "p5_import.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in; size_t inlen, incnt;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t outcap, outcnt;
} infl;

typedef struct { short count[MAXBITS + 1]; short symbol[MAXLCODES]; } huff;

static int get_bits(infl *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static int decode_sym(infl *s, const huff *h)
{
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        const int b = get_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

static int construct(huff *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int inflate_codes(infl *s, const huff *lc, const huff *dc)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;
    do {
        symbol = decode_sym(s, lc);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outcap) return -3;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            int len, dist, e;
            symbol -= 257;
            if (symbol >= 29) return -1;
            e = get_bits(s, lext[symbol]);
            if (e < 0) return -1;
            len = lens[symbol] + e;
            symbol = decode_sym(s, dc);
            if (symbol < 0 || symbol >= 30) return -1;
            e = get_bits(s, dext[symbol]);
            if (e < 0) return -1;
            dist = dists[symbol] + e;
            if ((size_t)dist > s->outcnt) return -1;
            if (s->outcnt + (size_t)len > s->outcap) return -3;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - (size_t)dist]; s->outcnt++; }
        }
    } while (symbol != 256);
    return 0;
}

static int inflate_stored(infl *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = s->in[s->incnt] | (unsigned)s->in[s->incnt + 1] << 8;
    nlen = s->in[s->incnt + 2] | (unsigned)s->in[s->incnt + 3] << 8;
    s->incnt += 4;
    if (len != (~nlen & 0xFFFFu)) return -1;
    if (s->incnt + len > s->inlen) return -1;
    if (s->outcnt + len > s->outcap) return -3;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int inflate_fixed(infl *s)
{
    static huff lc, dc;
    static int built;
    if (!built) {
        short lengths[FIXLCODES];
        int sym;
        for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lc, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&dc, lengths, MAXDCODES);
        built = 1;
    }
    return inflate_codes(s, &lc, &dc);
}

static int inflate_dynamic(infl *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff lc, dc;
    int nlen, ndist, ncode, index, err, v;
    v = get_bits(s, 5); if (v < 0) return -1; nlen = v + 257;
    v = get_bits(s, 5); if (v < 0) return -1; ndist = v + 1;
    v = get_bits(s, 4); if (v < 0) return -1; ncode = v + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -1;
    for (index = 0; index < ncode; index++) {
        v = get_bits(s, 3);
        if (v < 0) return -1;
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lc, lengths, 19) != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode_sym(s, &lc), len, rep;
        if (symbol < 0) return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                v = get_bits(s, 2); if (v < 0) return -1; rep = 3 + v;
            } else if (symbol == 17) {
                v = get_bits(s, 3); if (v < 0) return -1; rep = 3 + v;
            } else {
                v = get_bits(s, 7); if (v < 0) return -1; rep = 11 + v;
            }
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return inflate_codes(s, &lc, &dc);
}

int p5_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap, size_t *outlen)
{
    infl s;
    int last, type, err;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outcap = outcap;
    do {
        last = get_bits(&s, 1);
        type = get_bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        err = type == 0 ? inflate_stored(&s) : type == 1 ? inflate_fixed(&s) : type == 2 ? inflate_dynamic(&s) : -1;
        if (err) return err;
    } while (!last);
    if (outlen) *outlen = s.outcnt;
    return 0;
}

/* ====================================================================================================================
 * zip
 * ==================================================================================================================== */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static int ends_with_ci(const char *s, const char *suffix)
{
    const size_t a = strlen(s), b = strlen(suffix);
    size_t i;
    if (b > a) return 0;
    for (i = 0; i < b; i++) {
        char x = s[a - b + i], y = suffix[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

int p5_zip_walk(const uint8_t *zip, size_t zlen, const char *suffix, uint8_t *scratch, size_t scratch_cap,
                 p5_zip_cb cb, void *user)
{
    size_t eocd, cd, i, delivered = 0;
    unsigned entries, e;
    if (!zip || zlen < 22) return -1;
    for (eocd = zlen - 22;; eocd--) {                       /* the end record is within the last 64 KB + 22 bytes */
        if (rd32(zip + eocd) == 0x06054b50u) break;
        if (eocd == 0 || zlen - eocd > 65557u) return -1;
    }
    entries = rd16(zip + eocd + 10);
    cd = rd32(zip + eocd + 16);
    if (entries == 0xFFFFu || cd == 0xFFFFFFFFu) return -3;     /* zip64 */
    i = cd;
    for (e = 0; e < entries; e++) {
        unsigned flags, method, nlen, xlen, clen;
        size_t csize, usize, lho, data;
        char name[260];
        if (i + 46 > zlen || rd32(zip + i) != 0x02014b50u) return -2;
        flags = rd16(zip + i + 8);
        method = rd16(zip + i + 10);
        csize = rd32(zip + i + 20);
        usize = rd32(zip + i + 24);
        nlen = rd16(zip + i + 28);
        xlen = rd16(zip + i + 30);
        clen = rd16(zip + i + 32);
        lho = rd32(zip + i + 42);
        if (i + 46 + nlen + xlen + clen > zlen) return -2;
        if (nlen >= sizeof name) nlen = sizeof name - 1;
        memcpy(name, zip + i + 46, nlen);
        name[nlen] = 0;
        i += 46 + rd16(zip + i + 28) + xlen + clen;
        if (name[0] && name[strlen(name) - 1] == '/') continue;             /* directory */
        if (!ends_with_ci(name, suffix)) continue;
        if (flags & 1u) return -3;                                           /* encrypted */
        if (lho + 30 > zlen || rd32(zip + lho) != 0x04034b50u) return -2;
        data = lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
        if (data + csize > zlen || usize > scratch_cap) continue;            /* too big for the scratch buffer: skip */
        if (method == 0) {
            if (csize != usize) continue;
            memcpy(scratch, zip + data, usize);
        } else if (method == 8) {
            size_t got = 0;
            if (p5_inflate(zip + data, csize, scratch, scratch_cap, &got) != 0 || got != usize) continue;   /* corrupt entry */
        } else {
            continue;
        }
        delivered++;
        if (cb(name, scratch, usize, user) != 0) break;
    }
    return (int)delivered;
}


/* ====================================================================================================================
 * u-he Repro-5 .h2p (text part: "#cm=Section" lines followed by Key=value lines; the binary tail after "$$$$" is ignored)
 * ==================================================================================================================== */
enum {
    H_FENV_ATK, H_FENV_DEC, H_FENV_SUS, H_FENV_REL, H_AENV_ATK, H_AENV_DEC, H_AENV_SUS, H_AENV_REL,
    H_A_OCT, H_A_FREQ, H_A_FINE, H_A_SAW, H_A_RECT, H_A_PW, H_A_SYNC, H_A_VFM, H_A_VPWM,
    H_B_OCT, H_B_FREQ, H_B_FINE, H_B_SAW, H_B_TRI, H_B_RECT, H_B_PW, H_B_LOW, H_B_KPITCH,
    H_VMOD_B, H_VMOD_ENV, H_MIX_A, H_MIX_B, H_MIX_NOISE,
    H_FLT_CUT, H_FLT_RES, H_FLT_KEY, H_FLT_ENV, H_FLT_VMOD, H_FLT_POL, H_GLIDE,
    H_FENV_VEL, H_AENV_VEL, H_FLT_CHIP, H_VDETUNE,
    H_LFO_FREQ, H_LFO_SQR, H_LFO_TRI, H_LFO_SAW, H_LFO_ISAW, H_GM_MIX, H_GM_LOW, H_A_GFM, H_A_GPWM, H_B_GFM, H_B_GPWM, H_FLT_GMOD, H_COUNT
};

static const struct { const char *sec, *key; int idx; } k_map[] = {
    { "FEnv", "Atk", H_FENV_ATK }, { "FEnv", "Dec", H_FENV_DEC }, { "FEnv", "Sus", H_FENV_SUS }, { "FEnv", "Rel", H_FENV_REL },
    { "AEnv", "Atk", H_AENV_ATK }, { "AEnv", "Dec", H_AENV_DEC }, { "AEnv", "Sus", H_AENV_SUS }, { "AEnv", "Rel", H_AENV_REL },
    { "OscA", "Oct", H_A_OCT }, { "OscA", "Freq", H_A_FREQ }, { "OscA", "Fine", H_A_FINE }, { "OscA", "SawOn", H_A_SAW },
    { "OscA", "RectOn", H_A_RECT }, { "OscA", "PWidth", H_A_PW }, { "OscA", "Sync", H_A_SYNC }, { "OscA", "VModFM", H_A_VFM },
    { "OscA", "VModPWM", H_A_VPWM },
    { "OscB", "Oct", H_B_OCT }, { "OscB", "Freq", H_B_FREQ }, { "OscB", "Fine", H_B_FINE }, { "OscB", "SawOn", H_B_SAW },
    { "OscB", "TriOn", H_B_TRI }, { "OscB", "RectOn", H_B_RECT }, { "OscB", "PWidth", H_B_PW }, { "OscB", "LowFrq", H_B_LOW },
    { "OscB", "KPitch", H_B_KPITCH },
    { "VMod", "OscBAmt", H_VMOD_B }, { "VMod", "EnvAmt", H_VMOD_ENV },
    { "MIX", "OscA", H_MIX_A }, { "MIX", "OscB", H_MIX_B }, { "MIX", "Noise", H_MIX_NOISE },
    { "Filter", "Cutoff", H_FLT_CUT }, { "Filter", "Reso", H_FLT_RES }, { "Filter", "KeyAmt", H_FLT_KEY },
    { "Filter", "EnvAmt", H_FLT_ENV }, { "Filter", "VMod", H_FLT_VMOD }, { "Filter", "EnvPol", H_FLT_POL },
    { "Glide", "Amt", H_GLIDE },
    { "FEnv", "VelDep", H_FENV_VEL }, { "AEnv", "VelDep", H_AENV_VEL },
    { "FltrIC", "Chip", H_FLT_CHIP }, { "VDetune", "Amount", H_VDETUNE },
    { "LFO", "Freq", H_LFO_FREQ }, { "LFO", "Sqre", H_LFO_SQR }, { "LFO", "Triang", H_LFO_TRI }, { "LFO", "Saw", H_LFO_SAW },
    { "LFO", "InvSaw", H_LFO_ISAW }, { "GMod", "SrcMix", H_GM_MIX }, { "GMod", "LowLim", H_GM_LOW },
    { "OscA", "GModFM", H_A_GFM }, { "OscA", "GModPWM", H_A_GPWM }, { "OscB", "GModFM", H_B_GFM }, { "OscB", "GModPWM", H_B_GPWM },
    { "Filter", "GMod", H_FLT_GMOD },
};

typedef struct { float v[H_COUNT]; uint8_t has[H_COUNT]; int n_keys; } h2p_t;

/* locale-independent number reader: [+-]digits[.digits][e[+-]digits]; 0 when the text is not a number */
static int parse_num(const char *s, const char *end, float *out)
{
    double v = 0.0, frac = 0.1;
    int neg = 0, digits = 0;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    if (s < end && (*s == '-' || *s == '+')) { neg = *s == '-'; s++; }
    while (s < end && *s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; digits++; }
    if (s < end && *s == '.') {
        s++;
        while (s < end && *s >= '0' && *s <= '9') { v += (*s - '0') * frac; frac *= 0.1; s++; digits++; }
    }
    if (!digits) return 0;
    if (s < end && (*s == 'e' || *s == 'E')) {
        int eneg = 0, ev = 0;
        s++;
        if (s < end && (*s == '-' || *s == '+')) { eneg = *s == '-'; s++; }
        while (s < end && *s >= '0' && *s <= '9' && ev < 400) { ev = ev * 10 + (*s - '0'); s++; }
        while (ev-- > 0) v = eneg ? v * 0.1 : v * 10.0;
    }
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
    if (s != end) return 0;
    *out = (float)(neg ? -v : v);
    return 1;
}

static int h2p_parse(const char *text, size_t len, h2p_t *out)
{
    char sec[24] = "";
    int foreign = 0;
    size_t i = 0;
    memset(out, 0, sizeof *out);
    if (!text) return 0;
    while (i < len) {
        const char *ls = text + i, *le;
        size_t n = 0;
        while (i + n < len && text[i + n] != '\n') n++;
        le = ls + n;
        i += n + 1;
        if (n > 0 && le[-1] == '\r') le--;
        if (le - ls >= 4 && memcmp(ls, "$$$$", 4) == 0) break;
        if (le - ls >= 10 && memcmp(ls, "// Section", 10) == 0) break;
        if (le <= ls) continue;
        if (*ls == '#') {
            if (le - ls > 4 && memcmp(ls, "#cm=", 4) == 0) {
                size_t m = (size_t)(le - ls - 4);
                if (m >= sizeof sec) m = sizeof sec - 1;
                memcpy(sec, ls + 4, m);
                sec[m] = 0;
            } else if (le - ls > 4 && memcmp(ls, "#AM=", 4) == 0) {
                foreign = !((le - ls - 4) == 7 && memcmp(ls + 4, "Repro-5", 7) == 0);
            }
            continue;
        }
        {
            const char *eq = ls;
            while (eq < le && *eq != '=') eq++;
            if (eq < le && eq > ls && sec[0]) {
                const size_t kl = (size_t)(eq - ls);
                size_t m;
                float v;
                for (m = 0; m < sizeof k_map / sizeof *k_map; m++) {
                    if (strcmp(sec, k_map[m].sec) == 0 && strlen(k_map[m].key) == kl && memcmp(ls, k_map[m].key, kl) == 0) {
                        if (parse_num(eq + 1, le, &v) && v == v && v > -1e6f && v < 1e6f) {
                            out->v[k_map[m].idx] = v;
                            out->has[k_map[m].idx] = 1;
                            out->n_keys++;
                        }
                        break;
                    }
                }
            }
        }
    }
    return !foreign && out->n_keys > 0;
}

/* ---- Repro-5 -> Profit-5 ------------------------------------------------------------------------------------------ */
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static void put(p5_import_t *o, int id, float v) { o->v[id] = clampf(v, 0.f, 1.f); o->mask |= 1ull << id; }
static float dial(const h2p_t *h, int k, int id, p5_import_t *o) { if (h->has[k]) put(o, id, h->v[k] / 100.f); return h->v[k]; }
#define HV(i, d) (h->has[i] ? h->v[i] : (d))
#define ON(i, d) (HV(i, d) > 0.5f)

/* Repro-5's Filter Tweak chips in the manual's order: 0 Crispy, 1 Rounded, 2 Driven, 3 Poly. Crispy / Driven / Poly are the CEM-style voicing
 * (Rev 4), Rounded the SSM-style one (Rev 2). The numbering is inferred (TinyPro5 does the same), it is not written in the patch file. */
static int rev4(const h2p_t *h)
{
    int chip;
    if (!h->has[H_FLT_CHIP]) return 0;
    chip = (int)(h->v[H_FLT_CHIP] + 0.5f);
    return chip == 0 || chip == 2 || chip == 3;
}

static void map_h2p(const h2p_t *h, p5_import_t *o)
{
    int id;
    /* start from a neutral patch so nothing of the previous sound leaks in: everything off, filter open, amp organ-like */
    for (id = 0; id <= P5P_F_KBD; id++) put(o, id, 0.f);
    put(o, P5P_RELEASE, 1.f);
    put(o, P5P_A_SAW, 1.f);
    put(o, P5P_MIX_A, 0.8f);
    put(o, P5P_CUTOFF, 1.f);
    put(o, P5P_A_S, 1.f);
    put(o, P5P_F_S, 1.f);
    put(o, P5P_UNISON, 0.f);
    put(o, P5P_LFO_INIT, 0.f);
    put(o, P5P_KBD_HALF, 0.f);
    put(o, P5P_REV, rev4(h) ? 1.f : 0.f);

    dial(h, H_FLT_CUT, P5P_CUTOFF, o);
    dial(h, H_FLT_RES, P5P_RESO, o);
    if (h->has[H_FLT_ENV]) put(o, P5P_ENV_AMT, ON(H_FLT_POL, 0.f) ? 0.f : h->v[H_FLT_ENV] / 100.f);   /* no inverted envelope in Profit-5: off */
    if (h->has[H_FLT_KEY]) {                       /* Repro's key amount is continuous; Profit-5 has off / half / full */
        const float k = h->v[H_FLT_KEY];
        put(o, P5P_F_KBD, k >= 25.f ? 1.f : 0.f);
        put(o, P5P_KBD_HALF, k >= 25.f && k < 75.f ? 1.f : 0.f);
    }
    dial(h, H_FENV_ATK, P5P_F_A, o); dial(h, H_FENV_DEC, P5P_F_D, o); dial(h, H_FENV_SUS, P5P_F_S, o); dial(h, H_FENV_REL, P5P_F_R, o);
    dial(h, H_AENV_ATK, P5P_A_A, o); dial(h, H_AENV_DEC, P5P_A_D, o); dial(h, H_AENV_SUS, P5P_A_S, o); dial(h, H_AENV_REL, P5P_A_R, o);
    dial(h, H_GLIDE, P5P_GLIDE, o);

    /* oscillator A: pitch in whole semitones above the key (the pot steps), shapes are independent switches */
    if (h->has[H_A_OCT] || h->has[H_A_FREQ] || h->has[H_A_FINE])
        put(o, P5P_A_FREQ, floorf(clampf(12.f * (HV(H_A_OCT, 1.f) - 1.f) + HV(H_A_FREQ, 0.f) + HV(H_A_FINE, 0.f) / 100.f + 0.5f, 0.f, 60.f)) / 60.f);
    if (h->has[H_A_SAW] || h->has[H_A_RECT]) {
        put(o, P5P_A_SAW, ON(H_A_SAW, 0.f) ? 1.f : 0.f);
        put(o, P5P_A_PULSE, ON(H_A_RECT, 0.f) ? 1.f : 0.f);
    }
    dial(h, H_A_PW, P5P_A_PW, o);
    if (h->has[H_A_SYNC]) put(o, P5P_A_SYNC, ON(H_A_SYNC, 0.f) ? 1.f : 0.f);

    /* oscillator B: whole semitones on the frequency pot plus the fine pot (0 .. +1 semitone) for the remainder */
    if (h->has[H_B_OCT] || h->has[H_B_FREQ] || h->has[H_B_FINE]) {
        const float t = clampf(12.f * (HV(H_B_OCT, 2.f) - 1.f) + HV(H_B_FREQ, 0.f) + HV(H_B_FINE, 0.f) / 100.f, 0.f, 60.f);
        const float whole = floorf(t);
        put(o, P5P_B_FREQ, whole / 60.f);
        put(o, P5P_B_FINE, (t - whole));
    }
    if (h->has[H_B_SAW] || h->has[H_B_TRI] || h->has[H_B_RECT]) {
        put(o, P5P_B_SAW, ON(H_B_SAW, 0.f) ? 1.f : 0.f);
        put(o, P5P_B_TRI, ON(H_B_TRI, 0.f) ? 1.f : 0.f);
        put(o, P5P_B_PULSE, ON(H_B_RECT, 0.f) ? 1.f : 0.f);
    }
    dial(h, H_B_PW, P5P_B_PW, o);
    if (h->has[H_B_LOW]) put(o, P5P_B_LO, ON(H_B_LOW, 0.f) ? 1.f : 0.f);
    if (h->has[H_B_KPITCH]) put(o, P5P_B_KBD, ON(H_B_KPITCH, 1.f) ? 1.f : 0.f);

    dial(h, H_MIX_A, P5P_MIX_A, o);
    dial(h, H_MIX_B, P5P_MIX_B, o);
    dial(h, H_MIX_NOISE, P5P_MIX_NOISE, o);

    dial(h, H_VMOD_ENV, P5P_PM_FENV, o);
    dial(h, H_VMOD_B, P5P_PM_OSCB, o);
    put(o, P5P_PM_DEST_FA, ON(H_A_VFM, 0.f) ? 1.f : 0.f);
    put(o, P5P_PM_DEST_PWA, ON(H_A_VPWM, 0.f) ? 1.f : 0.f);
    put(o, P5P_PM_DEST_FILT, ON(H_FLT_VMOD, 0.f) ? 1.f : 0.f);

    /* LFO and wheel-mod. Repro-5's "global mod" is Profit-5's wheel-mod: a source mix (LFO .. noise), per-destination switches,
     * and an amount that runs from LowLim (wheel down) to UppLim (wheel up). Profit-5's wheel always reaches full depth, so
     * UppLim is not carried; LowLim becomes INIT AMT. The LFO rate is taken straight (0..100 -> the pot): the laws differ. The
     * mod matrix (MM1/MM2) has no equivalent here. */
    dial(h, H_LFO_FREQ, P5P_LFO_FREQ, o);
    put(o, P5P_LFO_SAW, ON(H_LFO_SAW, 0.f) || ON(H_LFO_ISAW, 0.f) ? 1.f : 0.f);
    put(o, P5P_LFO_TRI, ON(H_LFO_TRI, 0.f) ? 1.f : 0.f);
    put(o, P5P_LFO_SQR, ON(H_LFO_SQR, 0.f) ? 1.f : 0.f);
    dial(h, H_GM_MIX, P5P_WM_MIX, o);
    if (h->has[H_GM_LOW]) put(o, P5P_LFO_INIT, h->v[H_GM_LOW] / 100.f);
    put(o, P5P_WM_FA, ON(H_A_GFM, 0.f) ? 1.f : 0.f);
    put(o, P5P_WM_FB, ON(H_B_GFM, 0.f) ? 1.f : 0.f);
    put(o, P5P_WM_PWA, ON(H_A_GPWM, 0.f) ? 1.f : 0.f);
    put(o, P5P_WM_PWB, ON(H_B_GPWM, 0.f) ? 1.f : 0.f);
    put(o, P5P_WM_FILT, ON(H_FLT_GMOD, 0.f) ? 1.f : 0.f);

    /* voice detune is Profit-5's vintage (drift and tolerances); the velocity depths only exist in Rev 4 */
    if (h->has[H_VDETUNE]) put(o, P5P_VINTAGE, h->v[H_VDETUNE] / 100.f);
    if (rev4(h)) {
        if (h->has[H_FENV_VEL]) put(o, P5P_VEL_FILT, h->v[H_FENV_VEL] / 100.f);
        if (h->has[H_AENV_VEL]) put(o, P5P_VEL_AMP, h->v[H_AENV_VEL] / 100.f);
    } else {
        put(o, P5P_VEL_FILT, 0.f);
        put(o, P5P_VEL_AMP, 0.f);
    }
}

int p5_import_h2p(const char *text, size_t len, p5_import_t *out)
{
    h2p_t h;
    memset(out, 0, sizeof *out);
    if (!h2p_parse(text, len, &h)) return 0;
    map_h2p(&h, out);
    return 1;
}

/* the neutral patch (an empty file): what an unused program slot loads */
void p5_import_init(p5_import_t *out)
{
    h2p_t h;
    memset(out, 0, sizeof *out);
    memset(&h, 0, sizeof h);
    map_h2p(&h, out);
    memcpy(out->name, "Init", 5);
}
