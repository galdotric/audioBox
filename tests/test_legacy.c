/*
 * The 15 original tests from the old test.c, ported one for one.
 *
 * Each test keeps every check the original made, including the ones that look
 * inside struct sound_seg (tests 1, 7 and 12). Where the original built an
 * `expected` array but never compared it (tests 8, 10, 11, 14), the port now
 * compares it. Test 15 was a manual note in the original and is a real check here.
 * Comments that were wrong in the original are corrected and marked "was:".
 */
#include "harness.h"

/* Test 1: tr_init - basic initialisation */
static void legacy_01_init(void) {
    struct sound_seg* track = tr_init();
    CHECK(track != NULL);
    CHECK(track->samples == NULL);
    CHECK(track->length == 0);
    CHECK(track->next == NULL);
    CHECK(!track->is_parent);
    CHECK(!track->is_child);
    tr_destroy(track);
}

/* Test 2: tr_length - empty track */
static void legacy_02_length_empty(void) {
    struct sound_seg* track = tr_init();
    CHECK_EQ(tr_length(track), 0);
    tr_destroy(track);
}

/* Test 3: tr_length - multi-segment track */
static void legacy_03_length_multisegment(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3, 4, 5};
    tr_write(track, data, 0, 5);
    tr_insert(track, track, 2, 0, 3);
    CHECK_EQ(tr_length(track), 8);
    tr_destroy(track);
}

/* Test 4: tr_write - write to an empty track */
static void legacy_04_write_empty(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(track, data, 0, 3);
    CHECK_EQ(tr_length(track), 3);
    CHECK_TRACK(track, 1, 2, 3);
    tr_destroy(track);
}

/* Test 5: tr_write - write a longer block to an empty track */
static void legacy_05_write_extend(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3, 4, 5};
    tr_write(track, data, 0, 5);
    CHECK_EQ(tr_length(track), 5);
    CHECK_TRACK(track, 1, 2, 3, 4, 5);
    tr_destroy(track);
}

/* Test 6: tr_write - write past the end pads with zeros */
static void legacy_06_write_offset(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3, 4, 5};
    tr_write(track, data, 2, 5);
    CHECK_EQ(tr_length(track), 7);
    CHECK_TRACK(track, 0, 0, 1, 2, 3, 4, 5);
    tr_destroy(track);
}

/* Test 7: tr_insert - insert from another track at the start */
static void legacy_07_insert_other_at_start(void) {
    struct sound_seg* src = tr_init();
    struct sound_seg* dest = tr_init();
    int16_t src_data[] = {1, 2, 3};
    int16_t dest_data[] = {4, 5, 6};
    tr_write(dest, dest_data, 0, 3);
    tr_write(src, src_data, 0, 3);
    tr_insert(src, dest, 0, 1, 2);
    CHECK(src->is_parent);
    CHECK(dest->next != NULL && dest->next->is_child);   /* the shared piece is second */
    CHECK_TRACK(dest, 2, 3, 4, 5, 6);                     /* added */
    tr_destroy(src);
    tr_destroy(dest);
}

/* Test 8: tr_insert - self-insert in the middle */
static void legacy_08_self_insert_middle(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3, 4, 5};
    tr_write(track, data, 0, 5);
    tr_insert(track, track, 2, 0, 3);
    CHECK_EQ(tr_length(track), 8);
    CHECK_TRACK(track, 1, 2, 1, 2, 3, 3, 4, 5);          /* was: {1, 2, 1, 2, 3, 4, 5}, never compared */
    tr_destroy(track);
}

/* Test 9: tr_write after tr_insert */
static void legacy_09_write_after_insert(void) {
    struct sound_seg* track = tr_init();
    int16_t data1[] = {1, 2, 3, 4, 5};
    tr_write(track, data1, 0, 5);
    tr_insert(track, track, 2, 0, 3);                     /* [1, 2, 1, 2, 3, 3, 4, 5] */
    int16_t data2[] = {10, 11, 12, 13, 14, 15, 16, 17};
    tr_write(track, data2, 0, 8);
    CHECK_EQ(tr_length(track), 8);
    /* Added. Positions 2, 3, 4 are shared copies of positions 0, 1, 5, so each
     * pair holds whichever value was written to it last:
     *   0 & 2 -> 12,  1 & 3 -> 13,  4 & 5 -> 15. */
    CHECK_TRACK(track, 12, 13, 12, 13, 15, 15, 16, 17);
    tr_destroy(track);
}

/* Test 10: tr_insert - self-insert at the end */
static void legacy_10_self_insert_end(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(track, data, 0, 3);
    tr_insert(track, track, 3, 0, 3);
    CHECK_EQ(tr_length(track), 6);
    CHECK_TRACK(track, 1, 2, 3, 1, 2, 3);                 /* added */
    tr_destroy(track);
}

/* Test 11: tr_insert - self-insert at the start */
static void legacy_11_self_insert_start(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(track, data, 0, 3);
    tr_insert(track, track, 0, 1, 2);
    CHECK_EQ(tr_length(track), 5);
    CHECK_TRACK(track, 2, 3, 1, 2, 3);                    /* added */
    tr_destroy(track);
}

/* Test 12: tr_check_parent_child - refuses a parent source */
static void legacy_12_check_parent_conflict(void) {
    struct sound_seg* src = tr_init();
    struct sound_seg* dest = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(src, data, 0, 3);
    src->is_parent = true;
    CHECK_EQ(tr_check_parent_child(src, dest, 0, 0, 3), 0);
    tr_destroy(src);
    tr_destroy(dest);
}

/* Test 13: tr_check_parent_child - accepts a clean source */
static void legacy_13_check_valid(void) {
    struct sound_seg* src = tr_init();
    struct sound_seg* dest = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(src, data, 0, 3);
    CHECK_EQ(tr_check_parent_child(src, dest, 0, 0, 3), 1);
    tr_destroy(src);
    tr_destroy(dest);
}

/* Test 14: tr_read across multiple segments */
static void legacy_14_read_across_segments(void) {
    struct sound_seg* track = tr_init();
    int16_t data[] = {1, 2, 3};
    tr_write(track, data, 0, 3);
    tr_insert(track, track, 1, 0, 2);
    CHECK_EQ(tr_length(track), 5);
    CHECK_TRACK(track, 1, 1, 2, 2, 3);                    /* added: the read the test is named after */
    tr_destroy(track);
}

/* Test 15: tr_write with a NULL source must exit(1) */
static void legacy_15_write_null(void) {
    struct sound_seg* track = tr_init();
    CHECK_EXITS(1, tr_write(track, NULL, 0, 3));          /* was a manual note, never ran */
    tr_destroy(track);
}

static const struct test TESTS[] = {
    TEST(legacy_01_init,                  "legacy 01: tr_init basic initialisation"),
    TEST(legacy_02_length_empty,          "legacy 02: tr_length of an empty track"),
    TEST(legacy_03_length_multisegment,   "legacy 03: tr_length of a multi-segment track"),
    TEST(legacy_04_write_empty,           "legacy 04: tr_write to an empty track"),
    TEST(legacy_05_write_extend,          "legacy 05: tr_write extends the track"),
    TEST(legacy_06_write_offset,          "legacy 06: tr_write past the end pads with zeros"),
    TEST(legacy_07_insert_other_at_start, "legacy 07: tr_insert from another track at the start"),
    TEST(legacy_08_self_insert_middle,    "legacy 08: tr_insert self, middle"),
    TEST(legacy_09_write_after_insert,    "legacy 09: tr_write after tr_insert"),
    TEST(legacy_10_self_insert_end,       "legacy 10: tr_insert self, at the end"),
    TEST(legacy_11_self_insert_start,     "legacy 11: tr_insert self, at the start"),
    TEST(legacy_12_check_parent_conflict, "legacy 12: tr_check_parent_child refuses a parent"),
    TEST(legacy_13_check_valid,           "legacy 13: tr_check_parent_child allows a clean source"),
    TEST(legacy_14_read_across_segments,  "legacy 14: tr_read across segments"),
    TEST(legacy_15_write_null,            "legacy 15: tr_write with a NULL source exits"),
};

int main(void) {
    printf("Legacy tests (ported from the original test.c)\n"
           "==============================================\n");
    return run_tests(TESTS, N_TESTS(TESTS));
}
