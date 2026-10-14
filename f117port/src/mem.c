/* Real-mode memory: low memory, DOS arena, helpers. See mem.h. */
#include "mem.h"
#include "host.h"

#include <string.h>

u8 mem[MEM_SIZE];
u16 cur_dgroup;
u16 dg_sp;
u16 dos_owner = PSP_SEG;

u16 stk_alloc(u16 bytes)
{
    u16 n = (u16)((bytes + 1) & ~1u);
    if ((u32)(dg_sp ? dg_sp : 0x10000u) < (u32)n + 0x100) host_fatal("DGROUP stack model overflow");
    dg_sp = (u16)(dg_sp - n);
    return dg_sp;
}

/* ------------------------------------------------------------------ low memory */

#define BIOS_IRET_OFF 0xFF53                 /* F000:FF53 = IRET, the default vector target */

void mem_init_machine(void)
{
    memset(mem, 0, sizeof mem);
    wr8(0xF000, BIOS_IRET_OFF, 0xCF);
    for (int n = 0; n < 256; n++) ivt_set((u8)n, far_make(0xF000, BIOS_IRET_OFF));
    wr8(0xF000, 0xFFFE, 0xFC);               /* AT model byte: is_tandy() = 0 */
    wr16(0x40, 0x10, 0x0021);                /* equipment: 80x25 colour, FPU none, 1 floppy flag */
    wr16(0x40, 0x13, 640);                   /* conventional memory in KB */
    wr8 (0x40, 0x17, 0x00);                  /* shift flags (NumLock off) */
    wr16(0x40, 0x1A, 0x1E);                  /* type-ahead buffer head/tail: empty */
    wr16(0x40, 0x1C, 0x1E);
    wr8 (0x40, 0x49, 0x03);                  /* text mode 3 before the first program */
    wr16(0x40, 0x4A, 80);
    wr16(0x40, 0x63, 0x3D4);                 /* CRTC port (drv_flip reads it) */
    wr16(0x40, 0x80, 0x1E);                  /* buffer start/end */
    wr16(0x40, 0x82, 0x3E);
}

/* ------------------------------------------------------------------ DOS memory arena */

#define MCB_FIRST ((u16)(PSP_SEG - 1))

typedef struct { u8 type; u16 owner, size; } Mcb;

static Mcb mcb_get(u16 m) { Mcb c = { rd8(m, 0), rd16(m, 1), rd16(m, 3) }; return c; }
static void mcb_set(u16 m, u8 type, u16 owner, u16 size) { wr8(m, 0, type); wr16(m, 1, owner); wr16(m, 3, size); }

static void mcb_check(u16 m, Mcb c)
{
    if (c.type != 'M' && c.type != 'Z') host_fatal("DOS memory arena corrupt at %04X", m);
}

/* F117.COM is the only owner at start: its PSP block (PSP_SEG - 1 is the MCB) and the free rest. */
void dos_arena_reset(u16 psp_seg, u16 psp_paras)
{
    mcb_set((u16)(psp_seg - 1), 'M', psp_seg, psp_paras);
    mcb_set((u16)(psp_seg + psp_paras), 'Z', 0, (u16)(MEM_TOP_SEG - (psp_seg + psp_paras) - 1));
}

/* DOS 5 first fit: free blocks are merged with following free blocks while the chain is walked. */
static void merge_free(u16 m)
{
    Mcb c = mcb_get(m);
    while (c.type == 'M' && c.owner == 0) {
        u16 nx = (u16)(m + 1 + c.size);
        Mcb n = mcb_get(nx);
        mcb_check(nx, n);
        if (n.owner != 0) break;
        c.size = (u16)(c.size + 1 + n.size);
        c.type = n.type;
        mcb_set(m, c.type, 0, c.size);
    }
}

bool dos_mem_alloc(u16 paras, u16 *seg_or_largest)
{
    u16 largest = 0;
    for (u16 m = MCB_FIRST;;) {
        Mcb c = mcb_get(m);
        mcb_check(m, c);
        if (c.owner == 0) {
            merge_free(m);
            c = mcb_get(m);
            if (c.size >= paras) {
                if (c.size > paras) {
                    mcb_set((u16)(m + 1 + paras), c.type, 0, (u16)(c.size - paras - 1));
                    c.type = 'M';
                }
                mcb_set(m, c.type, dos_owner, paras);
                *seg_or_largest = (u16)(m + 1);
                return true;
            }
            if (c.size > largest) largest = c.size;
        }
        if (c.type == 'Z') break;
        m = (u16)(m + 1 + c.size);
    }
    *seg_or_largest = largest;
    return false;
}

bool dos_mem_free(u16 seg)
{
    u16 m = (u16)(seg - 1);
    Mcb c = mcb_get(m);
    if (c.type != 'M' && c.type != 'Z') return false;      /* error 9: invalid block */
    mcb_set(m, c.type, 0, c.size);
    return true;
}

bool dos_mem_resize(u16 seg, u16 paras, u16 *largest)
{
    u16 m = (u16)(seg - 1);
    Mcb c = mcb_get(m);
    if (c.type != 'M' && c.type != 'Z') { if (largest) *largest = 0; return false; }
    /* grow into following free blocks */
    u16 avail = c.size;
    if (c.type == 'M') {
        u16 nx = (u16)(m + 1 + c.size);
        merge_free(nx);
        Mcb n = mcb_get(nx);
        if (n.owner == 0) avail = (u16)(c.size + 1 + n.size);
    }
    if (paras > avail) { if (largest) *largest = avail; return false; }
    u8 type = c.type;
    if (avail != c.size) {                                  /* absorb the free neighbour */
        u16 nx = (u16)(m + 1 + c.size);
        type = mcb_get(nx).type;
    }
    if (paras < avail) {
        mcb_set((u16)(m + 1 + paras), type, 0, (u16)(avail - paras - 1));
        type = 'M';
    }
    mcb_set(m, type, c.owner, paras);
    return true;
}

u16 dos_mem_largest(void)
{
    u16 v;
    if (dos_mem_alloc(0xFFFF, &v)) host_fatal("DOS arena: 0FFFFh paragraphs allocated?");
    return v;
}

/* INT 21h/4Ch: DOS frees every block the terminating process owns (S5.1: prog_image_free). */
void dos_mem_free_owned(u16 owner)
{
    for (u16 m = MCB_FIRST;;) {
        Mcb c = mcb_get(m);
        mcb_check(m, c);
        if (c.owner == owner) mcb_set(m, c.type, 0, c.size);
        if (c.type == 'Z') break;
        m = (u16)(m + 1 + c.size);
    }
}
