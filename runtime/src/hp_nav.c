/* Closed-loop input (hp_nav.h): reads player 1's position from guest memory
 * and holds movement keys toward waypoints. Registered as the input script's
 * extra key source when HPRECOMP_POS_LOG or HPRECOMP_NAV_SCRIPT is set. */
#include "hp_nav.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "guest_mem.h"
#include "input_script.h"

#define PLAYER1_PTR 0xF71DC0u
#define PLAYER2_PTR 0xF71DC4u
#define OBJ_POS 0x13F0u
#define CAL_HOLD_MS 350u
#define CAL_GAP_MS 200u
#define CAL_MAX_AGE_MS 5000u
#define TAP_MS 100u

enum { ST_START, ST_READY, ST_GOTO, ST_APPROACH, ST_HOLD, ST_WAIT, ST_BASIS };
enum { CAL_NONE, CAL_W, CAL_GAP1, CAL_D, CAL_GAP2 };

struct step {
    uint8_t kind;
    uint16_t dik;
    float x, z, r, ymin, wx, wz, dx, dz; /* ymin 0: none */
    uint32_t ms;
    int line;
};

static struct {
    struct step *st;
    size_t n, cur;
    int log_pos;
    uint64_t t0, step_t0, last_log, cal_t, cal_phase_t0;
    uint32_t last_p1, other; /* other: the player 1 object before the last switch */
    int cal, have_basis, fixed_basis;
    float cal_x, cal_z, wx, wz, dx, dz; /* world (x, z) per W and per D */
} s;

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint32_t g32(uint32_t a)
{
    uint32_t v;
    memcpy(&v, guest_ptr(a), 4);
    return v;
}

/* Position of the player object at guest pointer slot; 0 if none. */
static int player_pos(uint32_t slot, uint32_t *obj, float p[3])
{
    *obj = g32(slot);
    if (*obj < 0x10000u)
        return 0;
    memcpy(p, guest_ptr(*obj + OBJ_POS), 12);
    return 1;
}

int hp_nav_parse(const char *text, char *err, size_t err_size)
{
    struct step *st = NULL;
    size_t n = 0, cap = 0;
    int line_no = 0;
    const char *why = NULL;
    for (const char *p = text; *p && !why;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[256], verb[16], a1[32], a2[32], a3[32], a4[32], a5[32], extra[2];
        line_no++;
        if (len >= sizeof line) {
            why = "line too long";
            break;
        }
        memcpy(line, p, len);
        line[len] = 0;
        p += len + (eol != NULL);
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        int got = sscanf(line, "%15s %31s %31s %31s %31s %31s %1s", verb, a1, a2, a3, a4, a5, extra);
        if (got <= 0)
            continue;
        struct step e = {.line = line_no};
        if (strcmp(verb, "start") == 0 && got == 2)
            e.kind = ST_START, e.ms = (uint32_t)strtoul(a1, NULL, 10);
        else if (strcmp(verb, "ready") == 0 && got == 1)
            e.kind = ST_READY;
        else if (strcmp(verb, "wait") == 0 && got == 2)
            e.kind = ST_WAIT, e.ms = (uint32_t)strtoul(a1, NULL, 10);
        else if (strcmp(verb, "basis") == 0 && got == 5)
            e.kind = ST_BASIS, e.wx = strtof(a1, NULL), e.wz = strtof(a2, NULL),
            e.dx = strtof(a3, NULL), e.dz = strtof(a4, NULL);
        else if (strcmp(verb, "approach") == 0 && got == 3) {
            e.kind = ST_APPROACH, e.r = strtof(a1, NULL), e.ms = (uint32_t)strtoul(a2, NULL, 10);
            if (!(e.r > 0))
                why = "approach radius must be > 0";
        } else if (strcmp(verb, "goto") == 0 && (got == 5 || got == 6)) {
            e.kind = ST_GOTO, e.x = strtof(a1, NULL), e.z = strtof(a2, NULL);
            e.r = strtof(a3, NULL), e.ms = (uint32_t)strtoul(a4, NULL, 10);
            e.ymin = got == 6 ? strtof(a5, NULL) : 0;
            if (!(e.r > 0))
                why = "goto radius must be > 0";
        } else if ((strcmp(verb, "hold") == 0 && got == 3) || (strcmp(verb, "tap") == 0 && got == 2)) {
            e.kind = ST_HOLD, e.dik = input_script_dik(a1);
            e.ms = verb[0] == 't' ? TAP_MS : (uint32_t)strtoul(a2, NULL, 10);
            if (!e.dik)
                why = "unknown key";
        } else
            why = "want ready, start|wait <ms>, goto <x> <z> <r> <ms> [ymin], approach <r> <ms>, basis <wx> <wz> <dx> <dz>, hold <KEY> <ms> or tap <KEY>";
        if (why)
            break;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            st = realloc(st, cap * sizeof *st);
        }
        st[n++] = e;
    }
    if (why) {
        snprintf(err, err_size, "line %d: %s", line_no, why);
        free(st);
        return -1;
    }
    free(s.st);
    s.st = st, s.n = n, s.cur = 0, s.step_t0 = 0, s.cal = CAL_NONE, s.have_basis = 0, s.fixed_basis = 0;
    return 0;
}

static void next_step(uint64_t t, const char *how)
{
    const struct step *e = &s.st[s.cur];
    fprintf(stderr, "NAV t=%llu step %zu (line %d) %s\n", (unsigned long long)t, s.cur, e->line, how);
    s.cur++;
    s.step_t0 = t;
}

/* Sets W/A/S/D toward world direction (vx, vz) through the calibrated basis. */
static void steer(uint8_t keys[256], float vx, float vz)
{
    /* Solve v = a*w + b*d. */
    float det = s.wx * s.dz - s.wz * s.dx;
    if (fabsf(det) < 1e-6f)
        return;
    float a = (vx * s.dz - vz * s.dx) / det, b = (s.wx * vz - s.wz * vx) / det;
    float m = fmaxf(fabsf(a), fabsf(b));
    if (a > 0.4f * m) keys[0x11] |= 0x80; /* W */
    if (a < -0.4f * m) keys[0x1F] |= 0x80; /* S */
    if (b > 0.4f * m) keys[0x20] |= 0x80; /* D */
    if (b < -0.4f * m) keys[0x1E] |= 0x80; /* A */
}

/* Calibration state machine; returns 1 while it holds the keys. */
static int calibrate(uint8_t keys[256], uint64_t t, const float p[3])
{
    uint64_t dt = t - s.cal_phase_t0;
    float ex = p[0] - s.cal_x, ez = p[2] - s.cal_z, len = sqrtf(ex * ex + ez * ez);
    switch (s.cal) {
    case CAL_NONE:
        s.cal = CAL_W, s.cal_phase_t0 = t, s.cal_x = p[0], s.cal_z = p[2];
        keys[0x11] |= 0x80;
        return 1;
    case CAL_W:
        if (dt < CAL_HOLD_MS) {
            keys[0x11] |= 0x80;
            return 1;
        }
        if (len > 0.05f)
            s.wx = ex / len, s.wz = ez / len;
        fprintf(stderr, "NAV t=%llu cal W moved %.3f dir %.3f %.3f\n", (unsigned long long)t, len, ex, ez);
        s.cal = CAL_GAP1, s.cal_phase_t0 = t;
        return 1;
    case CAL_GAP1:
        if (dt < CAL_GAP_MS)
            return 1;
        s.cal = CAL_D, s.cal_phase_t0 = t, s.cal_x = p[0], s.cal_z = p[2];
        keys[0x20] |= 0x80;
        return 1;
    case CAL_D:
        if (dt < CAL_HOLD_MS) {
            keys[0x20] |= 0x80;
            return 1;
        }
        if (len > 0.05f)
            s.dx = ex / len, s.dz = ez / len;
        fprintf(stderr, "NAV t=%llu cal D moved %.3f dir %.3f %.3f\n", (unsigned long long)t, len, ex, ez);
        s.cal = CAL_GAP2, s.cal_phase_t0 = t;
        return 1;
    default:
        if (dt < CAL_GAP_MS)
            return 1;
        s.cal = CAL_NONE, s.cal_t = t;
        s.have_basis = fabsf(s.wx * s.dz - s.wz * s.dx) > 0.2f;
        return 0;
    }
}

static void run_step(uint8_t keys[256], uint64_t t, int have, const float p[3])
{
    if (s.cur >= s.n)
        return;
    if (!s.step_t0)
        s.step_t0 = t;
    const struct step *e = &s.st[s.cur];
    uint64_t dt = t - s.step_t0;
    switch (e->kind) {
    case ST_START:
        if (t >= e->ms)
            next_step(t, "start");
        break;
    case ST_READY:
        if (have && fabsf(p[0]) + fabsf(p[2]) > 0.01f) {
            char how[96];
            snprintf(how, sizeof how, "ready at %.2f %.2f %.2f", p[0], p[1], p[2]);
            next_step(t, how);
        }
        break;
    case ST_WAIT:
        if (dt >= e->ms)
            next_step(t, "wait done");
        break;
    case ST_HOLD:
        if (dt >= e->ms)
            next_step(t, "hold done");
        else
            keys[e->dik] |= 0x80;
        break;
    case ST_BASIS:
        s.wx = e->wx, s.wz = e->wz, s.dx = e->dx, s.dz = e->dz;
        s.have_basis = s.fixed_basis = 1;
        next_step(t, "basis set");
        break;
    case ST_APPROACH:
    case ST_GOTO: {
        if (!have)
            break;
        float tx = e->x, tz = e->z;
        if (e->kind == ST_APPROACH) {
            if (s.other < 0x10000u) {
                next_step(t, "approach: no other character");
                break;
            }
            float o[3];
            memcpy(o, guest_ptr(s.other + OBJ_POS), 12);
            tx = o[0], tz = o[2];
        }
        float vx = tx - p[0], vz = tz - p[2], dist = sqrtf(vx * vx + vz * vz);
        if (e->ymin > 0 && p[1] > e->ymin) {
            char how[96];
            snprintf(how, sizeof how, "goto rose above y %.2f at %.2f %.2f %.2f", e->ymin, p[0], p[1], p[2]);
            next_step(t, how);
            break;
        }
        if (dist <= e->r && s.cal == CAL_NONE) {
            char how[96];
            snprintf(how, sizeof how, "goto reached at %.2f %.2f %.2f", p[0], p[1], p[2]);
            next_step(t, how);
            break;
        }
        if (dt >= e->ms && s.cal == CAL_NONE) {
            char how[96];
            snprintf(how, sizeof how, "goto timeout at %.2f %.2f %.2f dist %.2f", p[0], p[1], p[2], dist);
            next_step(t, how);
            break;
        }
        if (!s.fixed_basis && (s.cal != CAL_NONE || !s.have_basis || t - s.cal_t > CAL_MAX_AGE_MS)) {
            if (calibrate(keys, t, p))
                break;
        }
        if (s.have_basis)
            steer(keys, vx, vz);
        break;
    }
    }
}

void hp_nav_keys(uint8_t keys[256])
{
    uint64_t now = mono_ms();
    if (!s.t0)
        s.t0 = now;
    uint64_t t = now - s.t0;
    uint32_t p1, p2;
    float a[3] = {0}, b[3] = {0};
    int have = player_pos(PLAYER1_PTR, &p1, a);
    if (s.log_pos) {
        if (p1 != s.last_p1) {
            fprintf(stderr, "POS t=%llu p1 object %08x -> %08x\n", (unsigned long long)t, s.last_p1, p1);
            s.other = s.last_p1;
        }
        s.last_p1 = p1;
        if (t - s.last_log >= 1000) {
            int have2 = player_pos(PLAYER2_PTR, &p2, b);
            s.last_log = t;
            float o[3] = {0};
            if (s.other >= 0x10000u)
                memcpy(o, guest_ptr(s.other + OBJ_POS), 12);
            fprintf(stderr,
                    "POS t=%llu p1=%08x %.3f %.3f %.3f p2=%08x %.3f %.3f %.3f other=%08x %.3f %.3f %.3f step=%zu\n",
                    (unsigned long long)t, p1, have ? a[0] : 0, have ? a[1] : 0, have ? a[2] : 0, p2,
                    have2 ? b[0] : 0, have2 ? b[1] : 0, have2 ? b[2] : 0, s.other, o[0], o[1], o[2], s.cur);
        }
    }
    run_step(keys, t, have, a);
}

__attribute__((constructor)) static void hp_nav_init(void)
{
    const char *log = getenv("HPRECOMP_POS_LOG"), *path = getenv("HPRECOMP_NAV_SCRIPT");
    s.log_pos = log && strcmp(log, "1") == 0;
    if (path && path[0]) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "hp_nav: cannot open %s\n", path);
            abort();
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *text = malloc((size_t)size + 1);
        size_t got = fread(text, 1, (size_t)size, f);
        fclose(f);
        text[got] = 0;
        char err[128];
        if (hp_nav_parse(text, err, sizeof err) != 0) {
            fprintf(stderr, "hp_nav: %s: %s\n", path, err);
            abort();
        }
        free(text);
        fprintf(stderr, "hp_nav: %s loaded, %zu steps\n", path, s.n);
    }
    if (s.log_pos || s.n)
        input_script_set_extra(hp_nav_keys);
}
