/* Analysis tracer loaded next to the Genesis Plus GX core (built with HOOK_CPU=1).
 * It records, for the reference emulator only:
 *   - execution coverage per ROM address
 *   - data-read coverage per ROM byte, with the PC of the first reader
 *   - PC breakpoints with a full register snapshot
 *   - memory access watches (read/write ranges)
 *   - every DMA transfer the game sets up (source, length, VRAM destination)
 *   - the PC of the last writer for every VRAM byte
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE 0x200000
enum { H_EXEC = 1, H_READ = 2, H_WRITE = 4, H_VRAM_R = 8, H_VRAM_W = 16, H_CRAM_W = 64 };

typedef unsigned (*getreg_fn)(int);
static getreg_fn g_getreg;
static uint32_t g_pc, g_frame;
static int cov_on = 1;

static uint8_t  exec_cov[ROM_SIZE];
static uint8_t  read_cov[ROM_SIZE];
static uint32_t read_pc[ROM_SIZE];
static uint32_t vram_wpc[0x10000];

typedef struct { uint32_t frame, pc, d[8], a[8], sr; } BpRec;
static uint8_t bp_bits[ROM_SIZE / 8];
static BpRec *bplog; static uint32_t bplog_n, bplog_cap;

typedef struct { uint32_t frame, pc, addr, val; uint32_t width_w; } MemRec;
static struct { uint32_t lo, hi; int r, w; } watch[64]; static int nwatch;
static MemRec *memlog; static uint32_t memlog_n, memlog_cap;

typedef struct { uint32_t frame, pc, src, len, dest, code; } DmaRec;
static DmaRec *dmalog; static uint32_t dmalog_n, dmalog_cap;
static uint8_t vreg[32]; static int cmd_pending; static uint32_t cmd_first;

void tr_init(getreg_fn f) { g_getreg = f; }
void tr_set_frame(uint32_t f) { g_frame = f; }
void tr_cov(int on) { cov_on = on; }
void tr_reset_cov(void) { memset(exec_cov, 0, sizeof exec_cov); memset(read_cov, 0, sizeof read_cov); memset(read_pc, 0, sizeof read_pc); }
uint8_t  *tr_exec_cov(void) { return exec_cov; }
uint8_t  *tr_read_cov(void) { return read_cov; }
uint32_t *tr_read_pc(void)  { return read_pc; }
uint32_t *tr_vram_wpc(void) { return vram_wpc; }

void tr_bp_add(uint32_t pc) { if (pc < ROM_SIZE) bp_bits[pc >> 3] |= 1 << (pc & 7); }
void tr_bp_clear(void) { memset(bp_bits, 0, sizeof bp_bits); bplog_n = 0; }
BpRec *tr_bp_log(void) { return bplog; }
uint32_t tr_bp_count(void) { return bplog_n; }
void tr_bp_reset_log(void) { bplog_n = 0; }

void tr_watch_add(uint32_t lo, uint32_t hi, int r, int w) { if (nwatch < 64) { watch[nwatch].lo = lo; watch[nwatch].hi = hi; watch[nwatch].r = r; watch[nwatch].w = w; nwatch++; } }
void tr_watch_clear(void) { nwatch = 0; memlog_n = 0; }
MemRec *tr_mem_log(void) { return memlog; }
uint32_t tr_mem_count(void) { return memlog_n; }
void tr_mem_reset_log(void) { memlog_n = 0; }

DmaRec *tr_dma_log(void) { return dmalog; }
uint32_t tr_dma_count(void) { return dmalog_n; }
void tr_dma_reset_log(void) { dmalog_n = 0; }

static void rec_bp(void) {
    if (bplog_n == bplog_cap) { bplog_cap = bplog_cap ? bplog_cap * 2 : 4096; bplog = realloc(bplog, bplog_cap * sizeof *bplog); }
    BpRec *r = &bplog[bplog_n++];
    r->frame = g_frame; r->pc = g_pc;
    for (int i = 0; i < 8; i++) { r->d[i] = g_getreg(i); r->a[i] = g_getreg(8 + i); }
    r->sr = g_getreg(17);
}
static void rec_mem(uint32_t addr, uint32_t val, int width, int w) {
    if (memlog_n == memlog_cap) { memlog_cap = memlog_cap ? memlog_cap * 2 : 4096; memlog = realloc(memlog, memlog_cap * sizeof *memlog); }
    MemRec *r = &memlog[memlog_n++];
    r->frame = g_frame; r->pc = g_pc; r->addr = addr; r->val = val; r->width_w = (uint32_t)width | (w ? 0x100 : 0);
}
static void vdp_ctrl_word(uint32_t w) {
    w &= 0xFFFF;
    if (!cmd_pending && (w & 0xC000) == 0x8000) { vreg[(w >> 8) & 0x1F] = w & 0xFF; return; }
    if (!cmd_pending) { cmd_first = w; cmd_pending = 1; return; }
    cmd_pending = 0;
    uint32_t code = ((cmd_first >> 14) & 3) | ((w >> 2) & 0x3C);
    uint32_t dest = (cmd_first & 0x3FFF) | ((w & 3) << 14);
    if ((code & 0x20) && (vreg[1] & 0x10)) {
        if (dmalog_n == dmalog_cap) { dmalog_cap = dmalog_cap ? dmalog_cap * 2 : 4096; dmalog = realloc(dmalog, dmalog_cap * sizeof *dmalog); }
        DmaRec *r = &dmalog[dmalog_n++];
        r->frame = g_frame; r->pc = g_pc;
        r->len = (uint32_t)vreg[19] | ((uint32_t)vreg[20] << 8);
        r->src = (((uint32_t)vreg[23] & 0x7F) << 17) | ((uint32_t)vreg[22] << 9) | ((uint32_t)vreg[21] << 1);
        if (vreg[23] & 0x80) r->src = 0x80000000u | vreg[23] << 16 | vreg[22] << 8 | vreg[21];
        r->dest = dest; r->code = code;
    }
}

static unsigned long ym_hist[0x200]; static unsigned char ym_latch[2];
unsigned long *tr_ym_hist(void) { return ym_hist; }

void tr_hook(int type, int width, unsigned addr, unsigned val) {
    if (type == 2048 && (addr & 0xE000) == 0x4000) {   /* Z80 write to the FM chip */
        int part = (addr >> 1) & 1;
        if (!(addr & 1)) ym_latch[part] = (unsigned char)val; else ym_hist[(part << 8) | ym_latch[part]]++;
        return;
    }
    switch (type) {
    case H_EXEC:
        g_pc = addr & 0xFFFFFF;
        if (g_pc < ROM_SIZE) {
            if (cov_on && exec_cov[g_pc] != 255) exec_cov[g_pc]++;
            if (bp_bits[g_pc >> 3] & (1 << (g_pc & 7))) rec_bp();
        }
        break;
    case H_READ:
        addr &= 0xFFFFFF;
        if (cov_on && addr < ROM_SIZE) {
            for (int i = 0; i < width && addr + i < ROM_SIZE; i++) {
                read_cov[addr + i] = 1;
                if (!read_pc[addr + i]) read_pc[addr + i] = g_pc;
            }
        }
        for (int i = 0; i < nwatch; i++) if (watch[i].r && addr + width > watch[i].lo && addr <= watch[i].hi) { rec_mem(addr, val, width, 0); break; }
        break;
    case H_WRITE:
        addr &= 0xFFFFFF;
        if (addr >= 0xC00004 && addr <= 0xC00007) {
            if (width == 4) { vdp_ctrl_word(val >> 16); vdp_ctrl_word(val); } else vdp_ctrl_word(val);
        }
        for (int i = 0; i < nwatch; i++) if (watch[i].w && addr + width > watch[i].lo && addr <= watch[i].hi) { rec_mem(addr, val, width, 1); break; }
        break;
    case H_VRAM_W:
        vram_wpc[addr & 0xFFFF] = g_pc; vram_wpc[(addr + 1) & 0xFFFF] = g_pc;
        break;
    default: break;
    }
}
