/*
 * vbrecomp - Virtual Boy static recompilation libraries
 * SPDX-License-Identifier: MIT
 *
 * Basic smoke test for memory bus and CPU state.
 */

#include "vbrecomp/vbrecomp.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-40s ", name)
#define PASS() do { printf("OK\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) { FAIL(msg); return; } } while(0)

static void test_cpu_init(void) {
    TEST("cpu_init");
    vb_cpu_init();
    ASSERT_EQ(vb_cpu.r[0], 0, "r0 should be 0");
    ASSERT_EQ(vb_cpu.sr[VB_SREG_PIR], 0x00005346, "PIR should be 0x5346");
    ASSERT_EQ(vb_cpu.pc, 0xFFFFFFF0, "PC should be reset vector");
    PASS();
}

static void test_wram_rw(void) {
    TEST("wram read/write 8/16/32");

    uint8_t rom_dummy[4] = {0};
    vb_mem_init(rom_dummy, 4);

    /* 8-bit */
    vb_mem_write8(0x05000000, 0xAB);
    ASSERT_EQ(vb_mem_read8(0x05000000), 0xAB, "write8/read8 mismatch");

    /* 16-bit */
    vb_mem_write16(0x05000010, 0x1234);
    ASSERT_EQ(vb_mem_read16(0x05000010), 0x1234, "write16/read16 mismatch");

    /* 32-bit */
    vb_mem_write32(0x05000020, 0xDEADBEEF);
    ASSERT_EQ(vb_mem_read32(0x05000020), 0xDEADBEEF, "write32/read32 mismatch");

    PASS();
}

static void test_wram_mirror(void) {
    TEST("wram mirroring");

    uint8_t rom_dummy[4] = {0};
    vb_mem_init(rom_dummy, 4);

    vb_mem_write8(0x05000042, 0x77);
    /* WRAM is 64KB, so 0x10042 should mirror to 0x0042 */
    ASSERT_EQ(vb_mem_read8(0x05010042), 0x77, "WRAM mirror failed");

    PASS();
}

static void test_rom_read(void) {
    TEST("rom data read");

    uint8_t rom_data[16] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    };
    vb_mem_init(rom_data, 16);

    ASSERT_EQ(vb_mem_read8(0x07000000), 0x01, "ROM byte 0");
    ASSERT_EQ(vb_mem_read8(0x07000003), 0x04, "ROM byte 3");
    ASSERT_EQ(vb_mem_read16(0x07000000), 0x0201, "ROM halfword (little-endian)");
    ASSERT_EQ(vb_mem_read32(0x07000000), 0x04030201, "ROM word (little-endian)");

    PASS();
}

static void test_rom_write_ignored(void) {
    TEST("rom writes ignored");

    uint8_t rom_data[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    vb_mem_init(rom_data, 4);

    vb_mem_write8(0x07000000, 0xFF);
    ASSERT_EQ(vb_mem_read8(0x07000000), 0xAA, "ROM should be read-only");

    PASS();
}

static void test_timer_basic(void) {
    TEST("timer countdown");

    vb_timer_init();

    /* Set reload to 1, slow mode (~100us = 2000 cycles per tick) */
    vb_timer_write8(0x14, 0x01);  /* TLR = 1 */
    vb_timer_write8(0x18, 0x00);  /* THR = 0 */
    /* Enable with interrupt */
    vb_timer_write8(0x10, 0x09);  /* TCR: enable + int */

    /* Counter should start at reload value (1) */
    ASSERT_EQ(vb_timer_read8(0x14), 0x01, "counter should be 1 after start");

    /* Tick 2000 cycles: counter should go 1->0 */
    bool fired = vb_timer_tick(2000);
    ASSERT_EQ(fired, false, "should not fire yet (counter=0, not underflow)");
    ASSERT_EQ(vb_timer_read8(0x14), 0x00, "counter should be 0");

    /* Tick another 2000: counter should underflow, reload, and fire */
    fired = vb_timer_tick(2000);
    ASSERT_EQ(fired, true, "should fire on underflow");

    PASS();
}

static void test_gamepad(void) {
    TEST("gamepad latch");

    vb_gamepad_init();
    vb_gamepad_set_buttons(VB_BTN_A | VB_BTN_STA);

    /* Start a read */
    vb_gamepad_write8(0x28, 0x04); /* SCR: START */

    /* Read back button data */
    uint16_t lo = vb_gamepad_read8(0x2C);
    uint16_t hi = vb_gamepad_read8(0x30);
    uint16_t buttons = lo | (hi << 8);
    ASSERT_EQ(buttons, VB_BTN_A | VB_BTN_STA, "button latch mismatch");

    PASS();
}

int main(void) {
    printf("vbrecomp tests\n");
    printf("==============\n");

    test_cpu_init();
    test_wram_rw();
    test_wram_mirror();
    test_rom_read();
    test_rom_write_ignored();
    test_timer_basic();
    test_gamepad();

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
