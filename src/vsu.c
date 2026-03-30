/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * VSU stub - accepts register writes, generates silence.
 */

#include "vbrecomp/vsu.h"
#include <string.h>

/* Wave table RAM: 5 banks x 32 samples x 1 byte = 160 bytes
 * Mapped at 0x01000000, each bank at 128-byte intervals */
static uint8_t wave_tables[5 * 128];

/* Modulation table: 32 entries at 0x01000280 */
static uint8_t mod_table[256];

/* Channel registers: 6 channels, each with several registers */
#define VSU_NUM_CHANNELS 6

typedef struct {
    uint8_t sxint;   /* Interval */
    uint8_t sxlrv;   /* Left/right volume */
    uint16_t sxfql;  /* Frequency low */
    uint16_t sxfqh;  /* Frequency high */
    uint8_t sxev0;   /* Envelope data 0 */
    uint8_t sxev1;   /* Envelope data 1 */
    uint8_t sxram;   /* Wave table select (ch1-5) / noise tap (ch6) */
} vsu_channel_t;

static vsu_channel_t channels[VSU_NUM_CHANNELS];
static uint8_t sstop; /* Sound stop register */

void vb_vsu_init(void) {
    memset(wave_tables, 0, sizeof(wave_tables));
    memset(mod_table, 0, sizeof(mod_table));
    memset(channels, 0, sizeof(channels));
    sstop = 0;
}

uint8_t vb_vsu_read8(vb_addr_t addr) {
    /* Wave tables: 0x000-0x27F */
    if (addr < 0x280) {
        return wave_tables[addr];
    }
    /* Modulation table: 0x280-0x37F */
    if (addr < 0x380) {
        return mod_table[addr - 0x280];
    }
    /* Channel registers: 0x400+ */
    /* VSU registers are generally write-only in hardware, return 0 */
    return 0;
}

void vb_vsu_write8(vb_addr_t addr, uint8_t val) {
    /* Wave tables */
    if (addr < 0x280) {
        wave_tables[addr] = val & 0x3F; /* 6-bit samples */
        return;
    }
    /* Modulation table */
    if (addr < 0x380) {
        mod_table[addr - 0x280] = val;
        return;
    }
    /* Channel registers start at 0x400, spaced 0x40 apart */
    if (addr >= 0x400 && addr < 0x580) {
        int ch = (addr - 0x400) / 0x40;
        int reg = (addr - 0x400) % 0x40;
        if (ch >= VSU_NUM_CHANNELS) return;

        switch (reg) {
        case 0x00: channels[ch].sxint = val; break;
        case 0x04: channels[ch].sxlrv = val; break;
        case 0x08: channels[ch].sxfql = val; break;
        case 0x0C: channels[ch].sxfqh = val & 0x07; break;
        case 0x10: channels[ch].sxev0 = val; break;
        case 0x14: channels[ch].sxev1 = val; break;
        case 0x18: channels[ch].sxram = val; break;
        default: break;
        }
        return;
    }
    /* SSTOP register at 0x580 */
    if (addr == 0x580) {
        sstop = val & 0x01;
        return;
    }
}

void vb_vsu_generate(int16_t *buffer, int num_samples, int sample_rate) {
    (void)sample_rate;
    /* Stub: output silence */
    memset(buffer, 0, num_samples * 2 * sizeof(int16_t));
}
