/* The shell's test and script facilities (S5.4).
 *
 *  f117 --shell-test REFROOT --game-dir SCRATCH   (ctest `shell`; port/notes/s54/shell_ref.py writes REFROOT and SCRATCH)
 *     runs START against the references the ORIGINAL START.EXE left under Unicorn (port/notes/s54/shell_emu.py): for every step of a script
 *     (REFROOT/<scenario>/script.txt) the CRC of the screen and of the DAC at the settled poll of ui_poll_input, then the comm block and
 *     ROSTER.FIL after the exit. SCRATCH is a copy of the game folder (it is overwritten: it must contain SCRATCH.MARK).
 *
 *  F117_SHELL_SCRIPT=file (normal runs, the scripted chain test): the same script format drives START's polls; no references.
 *     When the script runs out of steps START leaves a space key in the keyboard buffer on exit so the PLAYER title-2 animation ends at once.
 *
 * Script format: `seed N`, and steps `x y key...` (x, y: the cursor or '-', keys: BIOS key words in hex, queued in order); a step runs at the
 * 3rd ui_poll_input after the previous step. '#' starts a comment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/vga.h"
#include "prog.h"
#include "shell/shell.h"

typedef struct { int x, y; int nkeys; u16 keys[8]; } Step;
typedef struct {
    Step steps[64];
    int nsteps, next;
    u32 seed;
    bool have_seed;
    unsigned since;
    u32 snap[200][2];
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

static void snapshot(u32 out[2], int index)
{
    u8 dac[768];
    dac_get(0, 256, dac);
    out[0] = crc32(vga_page(0xA000), 64000);
    out[1] = crc32(dac, 768);
    if (S.dump_dir) {
        char path[2300];
        snprintf(path, sizeof path, "%s/p%02d.scr", S.dump_dir, index);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(vga_page(0xA000), 1, 64000, f); fwrite(dac, 1, 768, f); fclose(f); }
    }
}

/* at the entry of ui_poll_input */
static void on_poll(unsigned n)
{
    if (getenv("F117_SHELL_POLLS")) fprintf(stderr, "[poll %u] cursor %d,%d\n", n, (s16)DSW(START_DS_ui_cursor_x), (s16)DSW(START_DS_cursor_y));
    S.since++;
    if (S.since < 3 || S.next >= S.nsteps) return;
    S.since = 0;
    if (S.nsnap < 200) snapshot(S.snap[S.nsnap], S.nsnap), S.nsnap++;
    const Step *s = &S.steps[S.next++];
    if (host_trace()) fprintf(stderr, "[shell] step %d: cursor %d,%d key %04X (poll %u, cursor was %d,%d, devices %d)\n", S.next - 1, s->x, s->y, s->nkeys ? s->keys[0] : 0, n,
                                          (s16)DSW(START_DS_ui_cursor_x), (s16)DSW(START_DS_cursor_y), comm_get()->input_devices);
    if (s->x >= 0 || s->y >= 0)                                 /* through ui_cursor_set_pos: with the mouse present the host mouse follows */
        ui_cursor_set_pos(s->x >= 0 ? s->x : (s16)DSW(START_DS_ui_cursor_x), s->y >= 0 ? s->y : (s16)DSW(START_DS_cursor_y));
    for (int i = 0; i < s->nkeys; i++) kbd_inject(s->keys[i]);
}

/* called by start_main: the script of a normal run (F117_SHELL_SCRIPT) */
void shell_test_from_env(void)
{
    static bool done;
    const char *p = getenv("F117_SHELL_SCRIPT");
    if (done || !p || !*p) return;
    done = true;
    if (!load_script(p)) { fprintf(stderr, "f117: cannot read F117_SHELL_SCRIPT %s\n", p); return; }
    shell_test.active = true;
    shell_test.on_poll = on_poll;
    shell_test.use_seed = S.have_seed;
    shell_test.seed = S.seed;
    shell_test.leave_space = true;
    comm_get()->input_devices &= (u16)~2u;                       /* the script moves the cursor itself: the mouse would overwrite it at every poll */
    S.dump_dir = getenv("F117_SHELL_DUMP");
}

bool shell_test_script_done(void) { return S.next >= S.nsteps; }

/* ------------------------------------------------------------------------------------------------ --shell-test */

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
    size_t nsnap, ncomm, nin, nout;
    u8 *snap = slurp(dir, "snap.bin", &nsnap), *comm_ref = slurp(dir, "comm.bin", &ncomm);
    u8 *rin = slurp(dir, "roster_in.fil", &nin), *rout = slurp(dir, "roster_out.fil", &nout);
    CHECK(snap && comm_ref && rin && rout && ncomm == 5120, "%s: references missing in %s", name, dir);
    if (!(snap && comm_ref && rin && rout)) return;
    CHECK(load_script(path), "%s: cannot read %s", name, path);
    static char dump[2200];
    if (getenv("F117_SHELL_DUMP_ROOT")) { snprintf(dump, sizeof dump, "%s/%s/portdump", getenv("F117_SHELL_DUMP_ROOT"), name); S.dump_dir = dump; }

    char *rpath = host_game_path("ROSTER.FIL", true);
    FILE *f = rpath ? fopen(rpath, "wb") : NULL;
    CHECK(f != NULL, "%s: cannot write ROSTER.FIL in the scratch folder", name);
    if (f) { fwrite(rin, 1, nin, f); fclose(f); }

    prog_comm_init();                                       /* F117.COM and SETUP: the comm block as the reference run starts */
    Comm *comm = comm_get();
    memset(comm, 0, COMM_SIZE);
    strcpy(comm->gfx_driver, "Mgraphic.exe");
    strcpy(comm->snd_driver, "Asound.117");
    comm->clearance = 1;
    comm->adapter = 3;
    comm->input_devices = 0;

    shell_test.active = true;
    shell_test.on_poll = on_poll;
    shell_test.use_seed = S.have_seed;
    shell_test.seed = S.seed;
    shell_test.polls = 0;
    int code = platform_exec(PROG_START, start_main, "");
    shell_test.on_poll = NULL;
    shell_test.use_seed = false;
    if (S.nsnap < 200) snapshot(S.snap[S.nsnap], S.nsnap), S.nsnap++;

    size_t nexit;
    u8 *ex = slurp(dir, "exit.txt", &nexit);
    CHECK(ex && atoi((char *)ex) == code, "%s: START exit code %02Xh, the original %d", name, code, ex ? atoi((char *)ex) : -1);
    free(ex);
    printf("%s: START exit code %02Xh, %d polls, %d snapshots\n", name, code, shell_test.polls, S.nsnap);
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
    CHECK(diff == 0, "%s: comm block: %d bytes differ", name, diff);

    size_t nr = 0;
    u8 *got = slurp(host_game_dir(), "ROSTER.FIL", &nr);
    CHECK(got && nr == nout && !memcmp(got, rout, nout), "%s: ROSTER.FIL after the run is not byte-for-byte the original's (%zu bytes, original %zu)", name, got ? nr : 0, nout);
    if (got && nr == nout && memcmp(got, rout, nout)) {
        for (size_t i = 0; i < nout; i++) if (got[i] != rout[i]) { printf("  first difference at byte %zu: port %02X original %02X\n", i, got[i], rout[i]); break; }
    }
    free(got); free(rpath); free(snap); free(comm_ref); free(rin); free(rout);
}

int shell_selftest(const char *refroot)
{
    char path[2200];
    snprintf(path, sizeof path, "%s/SCRATCH.MARK", host_game_dir());
    FILE *m = fopen(path, "rb");
    if (!m) { printf("shell-test: %s is not a scratch copy of the game folder (no SCRATCH.MARK): refusing to overwrite ROSTER.FIL\n", host_game_dir()); return 77; }
    fclose(m);
    size_t n;
    u8 *list = slurp(refroot, "scenarios.txt", &n);
    CHECK(list != NULL, "scenarios.txt missing in %s", refroot);
    char names[16][64];
    int nn = 0;
    for (char *t = list ? strtok((char *)list, "\r\n") : NULL; t && nn < 16; t = strtok(NULL, "\r\n")) snprintf(names[nn++], sizeof names[0], "%s", t);
    free(list);
    for (int i = 0; i < nn; i++) run_scenario(refroot, names[i]);
    printf("shell-test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
