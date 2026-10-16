/* The debrief's test and script facilities (S5.8).
 *
 *  f117 --debrief-test REFROOT --game-dir SCRATCH   (ctest `debrief`; port/notes/s58/end_ref.py writes REFROOT and SCRATCH)
 *     runs END once per scenario against the references the ORIGINAL END.EXE left under Unicorn (port/notes/s58/end_emu.py): the comm block of the scenario
 *     (REFROOT/<scenario>/comm_in.bin: a mission with its event log and pilot record), the script (script.txt), and for every step the CRC of the screen and of the
 *     DAC; after the exit the comm block (the pilot record END wrote, byte for byte) and the exit code 23h.
 *
 *  F117_END_SCRIPT=file (normal runs, the scripted chain test): the same script format drives END's prompts and map menu; no references.
 *
 * Script format: `seed N`, `skipmarks`, and steps `x y key...` (x, y: the cursor or '-', keys: BIOS key words in hex, queued in order); a step runs at the
 * 3rd poll_input after the previous step. Every wait_input entry is a snapshot point; a wait for a key (ticks = FFFFh) is answered with Enter, and with `skipmarks`
 * the first wait of 8 ticks (the site icons appearing one by one) with Space. '#' starts a comment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debrief/debrief.h"
#include "platform/vga.h"
#include "prog.h"

typedef struct { int x, y; int nkeys; u16 keys[8]; } Step;
typedef struct {
    Step steps[64];
    int nsteps, next;
    u32 seed;
    bool have_seed, skipmarks, skipped;
    unsigned since;
    u32 snap[600][2];
    int nsnap;
    const char *dump_dir;
} Script;
static Script S;

static u32 crc32(const u8 *p, size_t n)
{
    static u32 table[256];
    if (!table[1]) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static bool load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    memset(&S, 0, sizeof S);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char *tok[12];
        int n = 0;
        for (char *t = strtok(line, " \t\r\n"); t && n < 12; t = strtok(NULL, " \t\r\n")) tok[n++] = t;
        if (n == 0) continue;
        if (!strcmp(tok[0], "seed") && n > 1) { S.seed = (u32)strtoul(tok[1], NULL, 0); S.have_seed = true; continue; }
        if (!strcmp(tok[0], "skipmarks")) { S.skipmarks = true; continue; }
        if (n < 2 || S.nsteps >= 64) continue;
        Step *s = &S.steps[S.nsteps++];
        s->x = strcmp(tok[0], "-") ? atoi(tok[0]) : -1;
        s->y = strcmp(tok[1], "-") ? atoi(tok[1]) : -1;
        for (int i = 2; i < n && s->nkeys < 8; i++) s->keys[s->nkeys++] = (u16)strtoul(tok[i], NULL, 16);
    }
    fclose(f);
    S.since = 99;
    return true;
}

static void snapshot(void)
{
    if (S.nsnap >= 600) return;
    u8 dac[768];
    dac_get(0, 256, dac);
    u32 *out = S.snap[S.nsnap];
    out[0] = crc32(vga_page(0xA000), 64000);
    out[1] = crc32(dac, 768);
    if (S.dump_dir) {
        char path[2300];
        snprintf(path, sizeof path, "%s/p%02d.scr", S.dump_dir, S.nsnap);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(vga_page(0xA000), 1, 64000, f); fwrite(dac, 1, 768, f); fclose(f); }
    }
    S.nsnap++;
}

/* at the entry of wait_input */
static void on_wait(unsigned ticks)
{
    snapshot();
    if (ticks == 0xFFFF) kbd_inject(0x1C0D);
    else if (ticks == 8 && S.skipmarks && !S.skipped) { S.skipped = true; kbd_inject(0x3920); }
}

/* at the entry of poll_input */
static void on_poll(unsigned n)
{
    (void)n;
    S.since++;
    if (S.since < 3 || S.next >= S.nsteps) return;
    S.since = 0;
    snapshot();
    const Step *s = &S.steps[S.next++];
    if (host_trace()) fprintf(stderr, "[debrief] step %d: cursor %d,%d key %04X\n", S.next - 1, s->x, s->y, s->nkeys ? s->keys[0] : 0);
    if (s->x >= 0 || s->y >= 0) cursor_set_pos(s->x >= 0 ? s->x : (s16)DSW(0x7214), s->y >= 0 ? s->y : (s16)DSW(0x7216));
    for (int i = 0; i < s->nkeys; i++) kbd_inject(s->keys[i]);
}

/* called by debrief_main: the script of a normal run (F117_END_SCRIPT) */
void end_test_from_env(void)
{
    static bool done;
    const char *p = getenv("F117_END_SCRIPT");
    if (done || !p || !*p || end_test.active) return;
    done = true;
    if (!load_script(p)) { fprintf(stderr, "f117: cannot read F117_END_SCRIPT %s\n", p); return; }
    end_test.active = true;
    end_test.on_wait = on_wait;
    end_test.on_poll = on_poll;
    end_test.use_seed = S.have_seed;
    end_test.seed = S.seed;
    comm_get()->input_devices &= (u16)~2u;                       /* the script moves the cursor itself: the mouse would overwrite it at every poll */
    S.dump_dir = getenv("F117_END_DUMP");
}

/* ------------------------------------------------------------------------------------------------------ --debrief-test */

static int checks, fails;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails <= 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static u8 *slurp(const char *dir, const char *name, size_t *len)
{
    char path[2300];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    if (len) *len = (size_t)n;
    return b;
}

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static void run_scenario(const char *root, const char *name)
{
    char dir[2048], path[2200];
    snprintf(dir, sizeof dir, "%s/%s", root, name);
    snprintf(path, sizeof path, "%s/script.txt", dir);
    size_t nsnap, ncomm, nin;
    u8 *snap = slurp(dir, "snap.bin", &nsnap), *comm_ref = slurp(dir, "comm.bin", &ncomm), *comm_in = slurp(dir, "comm_in.bin", &nin);
    CHECK(snap && comm_ref && comm_in && ncomm == 5120 && nin == 5120, "%s: references missing in %s", name, dir);
    if (!(snap && comm_ref && comm_in)) return;
    CHECK(load_script(path), "%s: cannot read %s", name, path);
    static char dump[2200];
    if (getenv("F117_END_DUMP_ROOT")) { snprintf(dump, sizeof dump, "%s/%s/portdump", getenv("F117_END_DUMP_ROOT"), name); S.dump_dir = dump; }

    prog_comm_init();                                       /* F117.COM: the comm block, then the scenario's mission over it */
    memcpy(comm_get(), comm_in, 5120);
    prog_log("[debrief] scenario %s", name);

    end_test.active = true;
    end_test.on_wait = on_wait;
    end_test.on_poll = on_poll;
    end_test.use_seed = S.have_seed;
    end_test.seed = S.seed;
    end_test.polls = end_test.waits = 0;
    int code = platform_exec(PROG_END, end_main, "");
    end_test.on_wait = end_test.on_poll = NULL;
    end_test.use_seed = false;
    snapshot();

    size_t nexit;
    u8 *ex = slurp(dir, "exit.txt", &nexit);
    CHECK(ex && atoi((char *)ex) == code, "%s: END exit code %02Xh, the original %d", name, code, ex ? atoi((char *)ex) : -1);
    free(ex);
    printf("%s: END exit code %02Xh, %u waits, %u polls, %d snapshots\n", name, code, end_test.waits, end_test.polls, S.nsnap);
    CHECK(S.next == S.nsteps, "%s: %d of %d script steps ran", name, S.next, S.nsteps);
    CHECK((size_t)S.nsnap * 8 == nsnap, "%s: %d snapshots, the original %zu", name, S.nsnap, nsnap / 8);
    int bad = 0;
    for (int i = 0; i < S.nsnap && (size_t)(i + 1) * 8 <= nsnap; i++) {
        u32 rs = le32(snap + 8 * i), rd = le32(snap + 8 * i + 4);
        if (S.snap[i][0] != rs || S.snap[i][1] != rd) {
            bad++;
            if (bad <= 12) printf("  step %2d: screen %08X (original %08X)%s, DAC %08X (original %08X)%s\n", i, S.snap[i][0], rs,
                                  S.snap[i][0] == rs ? " ok" : " DIFFERS", S.snap[i][1], rd, S.snap[i][1] == rd ? " ok" : " DIFFERS");
        }
    }
    CHECK(bad == 0, "%s: %d of %d screens differ from the original", name, bad, S.nsnap);

    int diff = 0;
    const u8 *cb = (const u8 *)comm_get();
    for (int i = 0; i < 5120; i++) {
        if (i >= 0x1A && i < 0x22) continue;                 /* the driver segments of F117.COM */
        if (cb[i] != comm_ref[i]) { diff++; if (diff <= 16) printf("  comm+%03X port %02X original %02X\n", i, cb[i], comm_ref[i]); }
    }
    CHECK(diff == 0, "%s: comm block (the pilot record): %d bytes differ", name, diff);
    free(snap); free(comm_ref); free(comm_in);
}

int debrief_selftest(const char *refroot)
{
    size_t n;
    u8 *list = slurp(refroot, "scenarios.txt", &n);
    CHECK(list != NULL, "scenarios.txt missing in %s", refroot);
    char names[16][64];
    int nn = 0;
    for (char *t = list ? strtok((char *)list, "\r\n") : NULL; t && nn < 16; t = strtok(NULL, "\r\n")) snprintf(names[nn++], sizeof names[0], "%s", t);
    free(list);
    for (int i = 0; i < nn; i++) run_scenario(refroot, names[i]);
    printf("debrief-test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
