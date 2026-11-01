/*
 * Black-box tests for the sound_seg library.
 *
 * These tests use only the public functions. They check results through
 * tr_read, tr_length, return values and exit codes, never by looking inside
 * struct sound_seg, so they stay valid however the library is implemented.
 *
 * Segment layouts mentioned in comments ([1 2][9][3 4]) explain WHY a test is
 * interesting (which edge it hits). They are not checked.
 */
#include "harness.h"

/* Defined in sound_seg.c. Declared here so the test does not depend on the header. */
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);

static const char* tmp_wav(void) {
    static char path[64];
    snprintf(path, sizeof path, "/tmp/sound_seg_test_%d.wav", (int)getpid());
    return path;
}

/* ===================================================================== */
/* tr_init / tr_length                                                   */
/* ===================================================================== */

static void init_is_empty(void) {
    struct sound_seg* t = tr_init();
    CHECK(t != NULL);
    CHECK_EMPTY(t);
    tr_destroy(t);
}

static void destroy_null_is_safe(void) {
    tr_destroy(NULL);
}

/* ===================================================================== */
/* tr_write / tr_read                                                    */
/* ===================================================================== */

static void write_then_read(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    CHECK_TRACK(t, 1, 2, 3);
    tr_destroy(t);
}

static void write_overwrites_middle(void) {
    struct sound_seg* t = TRACK(1, 2, 3, 4, 5);
    tr_write(t, (int16_t[]){8, 9}, 2, 2);
    CHECK_TRACK(t, 1, 2, 8, 9, 5);
    tr_destroy(t);
}

static void write_appends_at_end(void) {
    struct sound_seg* t = TRACK(1, 2);
    tr_write(t, (int16_t[]){3, 4}, 2, 2);
    CHECK_TRACK(t, 1, 2, 3, 4);
    tr_destroy(t);
}

static void write_overlapping_end_extends(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_write(t, (int16_t[]){8, 9, 10}, 2, 3);
    CHECK_TRACK(t, 1, 2, 8, 9, 10);
    tr_destroy(t);
}

static void write_past_end_fills_gap_with_zeros(void) {
    struct sound_seg* t = tr_init();
    tr_write(t, (int16_t[]){1, 2}, 3, 2);
    CHECK_TRACK(t, 0, 0, 0, 1, 2);
    tr_destroy(t);
}

/* B11 */
static void write_zero_samples_to_empty_track(void) {
    struct sound_seg* t = tr_init();
    tr_write(t, (int16_t[]){1}, 0, 0);
    CHECK_EMPTY(t);
    tr_destroy(t);
}

static void write_zero_samples_changes_nothing(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_write(t, (int16_t[]){9}, 1, 0);
    CHECK_TRACK(t, 1, 2, 3);
    tr_destroy(t);
}

static void write_null_source_exits(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    CHECK_EXITS(1, tr_write(t, NULL, 0, 3));
    tr_destroy(t);
}

static void read_partial_range(void) {
    struct sound_seg* t = TRACK(1, 2, 3, 4, 5);
    int16_t out[3];
    tr_read(t, out, 1, 3);
    CHECK_EQ(out[0], 2);
    CHECK_EQ(out[1], 3);
    CHECK_EQ(out[2], 4);
    tr_destroy(t);
}

static void read_zero_samples(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    int16_t out[1] = {42};
    tr_read(t, out, 3, 0);
    CHECK_EQ(out[0], 42);
    tr_destroy(t);
}

static void read_past_end_exits(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    int16_t out[4];
    CHECK_EXITS(1, tr_read(t, out, 1, 3));
    tr_destroy(t);
}

/* ===================================================================== */
/* tr_insert from another track                                          */
/* ===================================================================== */

static void insert_into_middle(void) {
    struct sound_seg* src = TRACK(10, 20, 30);
    struct sound_seg* dest = TRACK(1, 2, 3, 4);
    tr_insert(src, dest, 2, 1, 2);
    CHECK_TRACK(dest, 1, 2, 20, 30, 3, 4);       /* B1: read spans 3 segments */
    CHECK_TRACK(src, 10, 20, 30);
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_at_start(void) {
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* dest = TRACK(4, 5, 6);
    tr_insert(src, dest, 0, 1, 2);
    CHECK_TRACK(dest, 2, 3, 4, 5, 6);
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_at_end(void) {
    struct sound_seg* src = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1, 2, 3);
    tr_insert(src, dest, 3, 0, 2);
    CHECK_TRACK(dest, 1, 2, 3, 7, 8);
    tr_destroy(dest);
    tr_destroy(src);
}

/* B7 */
static void insert_into_empty_track(void) {
    struct sound_seg* src = TRACK(7, 8, 9);
    struct sound_seg* dest = tr_init();
    tr_insert(src, dest, 0, 0, 3);
    CHECK_TRACK(dest, 7, 8, 9);
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_zero_samples_changes_nothing(void) {
    struct sound_seg* src = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1, 2);
    tr_insert(src, dest, 1, 0, 0);
    CHECK_TRACK(dest, 1, 2);
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_shares_source_to_dest(void) {
    struct sound_seg* src = TRACK(10, 20, 30);
    struct sound_seg* dest = TRACK(1, 2, 3, 4);
    tr_insert(src, dest, 2, 1, 2);
    tr_write(src, (int16_t[]){99}, 1, 1);
    CHECK_TRACK(dest, 1, 2, 99, 30, 3, 4);
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_shares_dest_to_source(void) {
    struct sound_seg* src = TRACK(10, 20, 30);
    struct sound_seg* dest = TRACK(1, 2, 3, 4);
    tr_insert(src, dest, 2, 1, 2);
    tr_write(dest, (int16_t[]){77, 88}, 2, 2);
    CHECK_TRACK(src, 10, 77, 88);
    tr_destroy(dest);
    tr_destroy(src);
}

/* B6: dest is [1][9][2 3] and the insert goes at the very end. */
static void insert_at_end_of_multisegment_track(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* src = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1, 2, 3);
    tr_insert(nine, dest, 1, 0, 1);
    CHECK_TRACK(dest, 1, 9, 2, 3);
    tr_insert(src, dest, 4, 0, 2);
    CHECK_TRACK(dest, 1, 9, 2, 3, 7, 8);
    tr_destroy(dest);
    tr_destroy(src);
    tr_destroy(nine);
}

/* B5: after deleting the inserted piece, dest is [1 2 3][4 5 6] and the
 * next insert lands exactly on that boundary. */
static void insert_at_boundary_keeps_tail(void) {
    struct sound_seg* a = TRACK(10, 20);
    struct sound_seg* b = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1, 2, 3, 4, 5, 6);
    tr_insert(a, dest, 3, 0, 2);
    CHECK(tr_delete_range(dest, 3, 2));
    CHECK_TRACK(dest, 1, 2, 3, 4, 5, 6);
    tr_insert(b, dest, 3, 0, 2);
    CHECK_TRACK(dest, 1, 2, 3, 7, 8, 4, 5, 6);
    tr_destroy(dest);
    tr_destroy(b);
    tr_destroy(a);
}

/* B8: src is [1][9][2 3]. The range [2 3] is not in src's first segment. */
static void insert_from_later_source_segment(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* dest = TRACK(5, 6);
    tr_insert(nine, src, 1, 0, 1);
    CHECK_TRACK(src, 1, 9, 2, 3);
    tr_insert(src, dest, 0, 2, 2);
    CHECK_TRACK(dest, 2, 3, 5, 6);
    tr_write(src, (int16_t[]){40}, 2, 1);           /* still shared */
    CHECK_TRACK(dest, 40, 3, 5, 6);
    tr_destroy(dest);
    tr_destroy(src);
    tr_destroy(nine);
}

/* B8: src is [1 2 3][4 5 6]. The range 3 4 5 crosses the boundary. */
static void insert_range_spanning_source_segments(void) {
    struct sound_seg* tmp = TRACK(0);
    struct sound_seg* src = TRACK(1, 2, 3, 4, 5, 6);
    struct sound_seg* dest = TRACK(100, 200);
    tr_insert(tmp, src, 3, 0, 1);
    CHECK(tr_delete_range(src, 3, 1));
    CHECK_TRACK(src, 1, 2, 3, 4, 5, 6);
    tr_insert(src, dest, 1, 2, 3);
    CHECK_TRACK(dest, 100, 3, 4, 5, 200);
    tr_destroy(dest);
    tr_destroy(src);
    tr_destroy(tmp);
}

static void insert_from_shared_source_is_refused(void) {
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* d1 = TRACK(7);
    struct sound_seg* d2 = TRACK(8);
    tr_insert(src, d1, 1, 0, 2);
    CHECK_TRACK(d1, 7, 1, 2);
    tr_insert(src, d2, 1, 0, 2);                    /* src samples already shared */
    CHECK_TRACK(d2, 8);
    tr_destroy(d1);
    tr_destroy(d2);
    tr_destroy(src);
}

static void insert_next_to_shared_samples_is_refused(void) {
    struct sound_seg* a = TRACK(10, 20);
    struct sound_seg* b = TRACK(30);
    struct sound_seg* dest = TRACK(1, 2, 3);
    tr_insert(a, dest, 1, 0, 2);                    /* [1][10 20][2 3] */
    tr_insert(b, dest, 2, 0, 1);                    /* between 10 and 20 */
    CHECK_TRACK(dest, 1, 10, 20, 2, 3);
    tr_destroy(dest);
    tr_destroy(b);
    tr_destroy(a);
}

static void insert_source_out_of_range_exits(void) {
    struct sound_seg* src = TRACK(1, 2);
    struct sound_seg* dest = TRACK(3);
    CHECK_EXITS(1, tr_insert(src, dest, 0, 1, 2));
    tr_destroy(dest);
    tr_destroy(src);
}

static void insert_dest_out_of_range_exits(void) {
    struct sound_seg* src = TRACK(1, 2);
    struct sound_seg* dest = TRACK(3);
    CHECK_EXITS(1, tr_insert(src, dest, 2, 0, 1));
    tr_destroy(dest);
    tr_destroy(src);
}

/* ===================================================================== */
/* tr_insert into the same track                                         */
/* ===================================================================== */

static void self_insert_middle(void) {
    struct sound_seg* t = TRACK(1, 2, 3, 4, 5);
    tr_insert(t, t, 2, 0, 3);
    CHECK_TRACK(t, 1, 2, 1, 2, 3, 3, 4, 5);
    tr_destroy(t);
}

static void self_insert_at_end(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_insert(t, t, 3, 0, 3);
    CHECK_TRACK(t, 1, 2, 3, 1, 2, 3);
    tr_destroy(t);
}

static void self_insert_at_start(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_insert(t, t, 0, 1, 2);
    CHECK_TRACK(t, 2, 3, 1, 2, 3);
    tr_destroy(t);
}

static void self_insert_source_straddles_insert_point(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_insert(t, t, 1, 0, 2);
    CHECK_TRACK(t, 1, 1, 2, 2, 3);
    tr_destroy(t);
}

static void self_insert_shares_samples(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_insert(t, t, 3, 0, 2);                       /* 1 2 3 1 2 */
    tr_write(t, (int16_t[]){50}, 0, 1);
    CHECK_TRACK(t, 50, 2, 3, 50, 2);
    tr_destroy(t);
}

/* B9 */
static void self_insert_protects_source(void) {
    struct sound_seg* t = TRACK(1, 2, 3, 4, 5);
    tr_insert(t, t, 5, 0, 2);                       /* 1 2 3 4 5 1 2 */
    CHECK(!tr_delete_range(t, 0, 2));
    CHECK_TRACK(t, 1, 2, 3, 4, 5, 1, 2);
    tr_destroy(t);
}

/* B8: t is [1][9][2 3 4]. The source is in a later segment. */
static void self_insert_from_later_segment(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* t = TRACK(1, 2, 3, 4);
    tr_insert(nine, t, 1, 0, 1);
    CHECK_TRACK(t, 1, 9, 2, 3, 4);
    tr_insert(t, t, 0, 3, 1);
    CHECK_TRACK(t, 3, 1, 9, 2, 3, 4);
    tr_destroy(t);
    tr_destroy(nine);
}

/* ===================================================================== */
/* tr_check_parent_child                                                 */
/* ===================================================================== */

static void check_allows_clean_insert(void) {
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* dest = tr_init();
    CHECK_EQ(tr_check_parent_child(src, dest, 0, 0, 3), 1);
    tr_destroy(dest);
    tr_destroy(src);
}

static void check_refuses_shared_source(void) {
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* d1 = TRACK(7);
    struct sound_seg* d2 = TRACK(8);
    tr_insert(src, d1, 0, 0, 3);
    CHECK_EQ(tr_check_parent_child(src, d2, 0, 0, 3), 0);
    tr_destroy(d1);
    tr_destroy(d2);
    tr_destroy(src);
}

/* B12: src is [1][9][2 3], the range 2 3 is free to share. */
static void check_uses_whole_source_length(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* dest = TRACK(5);
    tr_insert(nine, src, 1, 0, 1);
    CHECK_EQ(tr_check_parent_child(src, dest, 0, 2, 2), 1);
    tr_destroy(dest);
    tr_destroy(src);
    tr_destroy(nine);
}

/* ===================================================================== */
/* tr_delete_range                                                       */
/* ===================================================================== */

static void delete_middle(void) {
    struct sound_seg* t = TRACK(1, 2, 3, 4, 5);
    CHECK(tr_delete_range(t, 1, 3));
    CHECK_TRACK(t, 1, 5);
    tr_destroy(t);
}

static void delete_everything_then_reuse(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    CHECK(tr_delete_range(t, 0, 3));
    CHECK_EMPTY(t);
    tr_write(t, (int16_t[]){4, 5}, 0, 2);
    CHECK_TRACK(t, 4, 5);
    tr_destroy(t);
}

static void delete_zero_samples(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    CHECK(tr_delete_range(t, 1, 0));
    CHECK_TRACK(t, 1, 2, 3);
    tr_destroy(t);
}

static void delete_past_end_is_refused(void) {
    struct sound_seg* t = TRACK(1, 2, 3);
    CHECK(!tr_delete_range(t, 2, 2));
    CHECK_TRACK(t, 1, 2, 3);
    tr_destroy(t);
}

/* B3: t is [1 2 3][9]. Deletes the whole first segment. */
static void delete_whole_first_segment(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* t = TRACK(1, 2, 3);
    tr_insert(nine, t, 3, 0, 1);
    CHECK(tr_delete_range(t, 0, 3));
    CHECK_TRACK(t, 9);
    tr_destroy(t);
    tr_destroy(nine);
}

/* B2, B4: t is [1 2][9][3 4]. Deletes the whole inserted segment. */
static void delete_whole_inserted_segment(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* t = TRACK(1, 2, 3, 4);
    tr_insert(nine, t, 2, 0, 1);
    CHECK(tr_delete_range(t, 2, 1));
    CHECK_TRACK(t, 1, 2, 3, 4);
    CHECK_TRACK(nine, 9);
    tr_destroy(t);
    tr_destroy(nine);
}

/* B2: t is [1 2][9][3 4]. The range covers parts of all three segments. */
static void delete_across_segments(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* t = TRACK(1, 2, 3, 4);
    tr_insert(nine, t, 2, 0, 1);
    CHECK(tr_delete_range(t, 1, 3));
    CHECK_TRACK(t, 1, 4);
    tr_destroy(t);
    tr_destroy(nine);
}

/* B4: deleting part of the inserted piece must not touch the source. */
static void delete_part_of_inserted_piece(void) {
    struct sound_seg* src = TRACK(10, 20, 30, 40);
    struct sound_seg* dest = TRACK(1, 2);
    tr_insert(src, dest, 2, 0, 4);
    CHECK(tr_delete_range(dest, 3, 2));
    CHECK_TRACK(dest, 1, 2, 10, 40);
    CHECK_TRACK(src, 10, 20, 30, 40);
    tr_write(src, (int16_t[]){11}, 0, 1);          /* what is left is still shared */
    CHECK_TRACK(dest, 1, 2, 11, 40);
    tr_destroy(dest);
    tr_destroy(src);
}

static void delete_shared_source_is_refused(void) {
    struct sound_seg* src = TRACK(1, 2, 3);
    struct sound_seg* dest = TRACK(7);
    tr_insert(src, dest, 1, 0, 3);
    CHECK(!tr_delete_range(src, 0, 1));
    CHECK_TRACK(src, 1, 2, 3);
    CHECK_TRACK(dest, 7, 1, 2, 3);
    tr_destroy(dest);
    tr_destroy(src);
}

/* ===================================================================== */
/* Writing over and past shared segments                                  */
/* ===================================================================== */

static void write_across_segments(void) {
    struct sound_seg* nine = TRACK(9);
    struct sound_seg* t = TRACK(1, 2, 3, 4);
    tr_insert(nine, t, 2, 0, 1);                    /* [1 2][9][3 4] */
    tr_write(t, (int16_t[]){5, 6, 7, 8, 0}, 0, 5);
    CHECK_TRACK(t, 5, 6, 7, 8, 0);
    CHECK_TRACK(nine, 7);                           /* written through the share */
    tr_destroy(t);
    tr_destroy(nine);
}

/* B10: dest ends in an inserted (borrowed) segment. */
static void write_extends_track_ending_in_inserted_piece(void) {
    struct sound_seg* src = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1, 2);
    tr_insert(src, dest, 2, 0, 2);
    tr_write(dest, (int16_t[]){5, 6}, 4, 2);
    CHECK_TRACK(dest, 1, 2, 7, 8, 5, 6);
    CHECK_TRACK(src, 7, 8);
    tr_destroy(dest);
    tr_destroy(src);
}

/* B10: growing the source must not move memory the copy still uses. */
static void write_extends_shared_source(void) {
    struct sound_seg* src = TRACK(7, 8);
    struct sound_seg* dest = TRACK(1);
    tr_insert(src, dest, 1, 0, 2);
    int16_t big[1000];
    for (int i = 0; i < 1000; i++) big[i] = (int16_t)(i + 100);
    tr_write(src, big, 0, 1000);
    CHECK_EQ(tr_length(src), 1000);
    CHECK_TRACK(dest, 1, 100, 101);
    tr_destroy(dest);
    tr_destroy(src);
}

/* ===================================================================== */
/* tr_destroy with shared tracks                                         */
/* ===================================================================== */

static void destroy_source_before_copy(void) {
    struct sound_seg* src = TRACK(1, 2);
    struct sound_seg* dest = TRACK(3);
    tr_insert(src, dest, 1, 0, 2);
    tr_destroy(dest);   /* order 1 */
    tr_destroy(src);
    src = TRACK(1, 2);
    dest = TRACK(3);
    tr_insert(src, dest, 1, 0, 2);
    tr_destroy(src);    /* order 2: the copy's struct is still freed safely */
    /* dest must not be read now, but destroying it must not double free */
    tr_destroy(dest);
}

/* ===================================================================== */
/* tr_identify                                                           */
/* ===================================================================== */

static void identify_one_match(void) {
    struct sound_seg* target = TRACK(0, 0, 5, -5, 5, 0, 0);
    struct sound_seg* ad = TRACK(5, -5, 5);
    char* r = tr_identify(target, ad);
    CHECK(strcmp(r, "2,4") == 0);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
}

static void identify_two_matches(void) {
    struct sound_seg* target = TRACK(5, -5, 0, 0, 5, -5, 0);
    struct sound_seg* ad = TRACK(5, -5);
    char* r = tr_identify(target, ad);
    CHECK(strcmp(r, "0,1\n4,5") == 0);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
}

static void identify_no_match(void) {
    struct sound_seg* target = TRACK(1, 1, 1, 1);
    struct sound_seg* ad = TRACK(100, -100);
    char* r = tr_identify(target, ad);
    CHECK(strcmp(r, "") == 0);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
}

static void identify_ad_longer_than_target(void) {
    struct sound_seg* target = TRACK(5);
    struct sound_seg* ad = TRACK(5, 5);
    char* r = tr_identify(target, ad);
    CHECK(strcmp(r, "") == 0);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
}

/* B13: target is [0 0][5 -5][0]. The match is not in the first segment. */
static void identify_in_multisegment_target(void) {
    struct sound_seg* piece = TRACK(5, -5);
    struct sound_seg* target = TRACK(0, 0, 0);
    struct sound_seg* ad = TRACK(5, -5);
    tr_insert(piece, target, 2, 0, 2);
    char* r = tr_identify(target, ad);
    CHECK(strcmp(r, "2,3") == 0);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
    tr_destroy(piece);
}

/* B13: 400 matches produce far more than 1024 characters. */
static void identify_many_matches(void) {
    int16_t data[800];
    for (int i = 0; i < 800; i++) data[i] = (i % 2) ? -5 : 5;
    struct sound_seg* target = tr_init();
    tr_write(target, data, 0, 800);
    struct sound_seg* ad = TRACK(5, -5);
    char* r = tr_identify(target, ad);
    int lines = 1;
    for (char* p = r; *p; p++) lines += (*p == '\n');
    CHECK_EQ(lines, 400);
    CHECK(strstr(r, "798,799") != NULL);
    free(r);
    tr_destroy(target);
    tr_destroy(ad);
}

/* ===================================================================== */
/* WAV files                                                             */
/* ===================================================================== */

static void wav_round_trip(void) {
    int16_t data[] = {0, 1000, -1000, 32767, -32768};
    wav_save(tmp_wav(), data, 5);
    CHECK_EQ(get_wav_sample_count((char*)tmp_wav()), 5);
    int16_t back[5] = {0};
    wav_load(tmp_wav(), back);
    CHECK(memcmp(back, data, sizeof data) == 0);
    remove(tmp_wav());
}

static void wav_header_is_8khz_mono_16bit(void) {
    int16_t data[] = {1, 2, 3};
    wav_save(tmp_wav(), data, 3);
    FILE* f = fopen(tmp_wav(), "rb");
    unsigned char h[44];
    CHECK_EQ(fread(h, 1, 44, f), 44);
    fseek(f, 0, SEEK_END);
    CHECK_EQ(ftell(f), 44 + 3 * 2);
    fclose(f);
    remove(tmp_wav());
    CHECK(memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVEfmt ", 8) == 0);
    CHECK(memcmp(h + 36, "data", 4) == 0);
    CHECK_EQ(h[22] | h[23] << 8, 1);                          /* channels */
    CHECK_EQ(h[24] | h[25] << 8 | h[26] << 16, 8000);         /* sample rate */
    CHECK_EQ(h[34] | h[35] << 8, 16);                         /* bits per sample */
}

/* B14 */
static void wav_missing_file_is_handled(void) {
    CHECK_EQ(get_wav_sample_count("/nonexistent/missing.wav"), 0);
    int16_t buf[2] = {7, 7};
    wav_load("/nonexistent/missing.wav", buf);
    CHECK_EQ(buf[0], 7);
}

/* ===================================================================== */

static const struct test TESTS[] = {
    TEST(init_is_empty,                         "init: new track is empty"),
    TEST(destroy_null_is_safe,                  "destroy: NULL is ignored"),

    TEST(write_then_read,                       "write: then read back"),
    TEST(write_overwrites_middle,               "write: overwrite the middle"),
    TEST(write_appends_at_end,                  "write: append at the end"),
    TEST(write_overlapping_end_extends,         "write: overlapping the end extends the track"),
    TEST(write_past_end_fills_gap_with_zeros,   "write: past the end fills the gap with zeros"),
    TEST(write_zero_samples_to_empty_track,     "write: 0 samples to an empty track            [B11]"),
    TEST(write_zero_samples_changes_nothing,    "write: 0 samples changes nothing"),
    TEST(write_null_source_exits,               "write: NULL source exits with 1"),
    TEST(write_across_segments,                 "write: across segments, through a share"),
    TEST(write_extends_track_ending_in_inserted_piece,
                                                "write: extend a track ending in a shared piece [B10]"),
    TEST(write_extends_shared_source,           "write: extend a source that is shared          [B10]"),

    TEST(read_partial_range,                    "read: a range in the middle"),
    TEST(read_zero_samples,                     "read: 0 samples"),
    TEST(read_past_end_exits,                   "read: past the end exits with 1"),

    TEST(insert_into_middle,                    "insert: into the middle                        [B1]"),
    TEST(insert_at_start,                       "insert: at the start"),
    TEST(insert_at_end,                         "insert: at the end"),
    TEST(insert_into_empty_track,               "insert: into an empty track                    [B7]"),
    TEST(insert_zero_samples_changes_nothing,   "insert: 0 samples changes nothing"),
    TEST(insert_shares_source_to_dest,          "insert: writing the source changes the copy"),
    TEST(insert_shares_dest_to_source,          "insert: writing the copy changes the source"),
    TEST(insert_at_end_of_multisegment_track,   "insert: at the end of a multi-segment track    [B6]"),
    TEST(insert_at_boundary_keeps_tail,         "insert: on a segment boundary keeps the tail   [B5]"),
    TEST(insert_from_later_source_segment,      "insert: source range in a later segment        [B8]"),
    TEST(insert_range_spanning_source_segments, "insert: source range spanning two segments     [B8]"),
    TEST(insert_from_shared_source_is_refused,  "insert: a source that is already shared is refused"),
    TEST(insert_next_to_shared_samples_is_refused,
                                                "insert: next to shared samples is refused"),
    TEST(insert_source_out_of_range_exits,      "insert: source range past the end exits with 1"),
    TEST(insert_dest_out_of_range_exits,        "insert: position past the end exits with 1"),

    TEST(self_insert_middle,                    "self-insert: into the middle"),
    TEST(self_insert_at_end,                    "self-insert: at the end"),
    TEST(self_insert_at_start,                  "self-insert: at the start"),
    TEST(self_insert_source_straddles_insert_point,
                                                "self-insert: source range around the insert point"),
    TEST(self_insert_shares_samples,            "self-insert: the copy is shared"),
    TEST(self_insert_protects_source,           "self-insert: the source can't be deleted       [B9]"),
    TEST(self_insert_from_later_segment,        "self-insert: source in a later segment         [B8]"),

    TEST(check_allows_clean_insert,             "check: allows a clean insert"),
    TEST(check_refuses_shared_source,           "check: refuses a shared source"),
    TEST(check_uses_whole_source_length,        "check: uses the whole source length            [B12]"),

    TEST(delete_middle,                         "delete: from the middle"),
    TEST(delete_everything_then_reuse,          "delete: everything, then write again"),
    TEST(delete_zero_samples,                   "delete: 0 samples"),
    TEST(delete_past_end_is_refused,            "delete: past the end returns false"),
    TEST(delete_whole_first_segment,            "delete: the whole first segment                [B3]"),
    TEST(delete_whole_inserted_segment,         "delete: a whole inserted segment               [B2 B4]"),
    TEST(delete_across_segments,                "delete: across three segments                  [B2]"),
    TEST(delete_part_of_inserted_piece,         "delete: part of an inserted piece              [B4]"),
    TEST(delete_shared_source_is_refused,       "delete: a shared source returns false"),

    TEST(destroy_source_before_copy,            "destroy: either order"),

    TEST(identify_one_match,                    "identify: one match"),
    TEST(identify_two_matches,                  "identify: two matches"),
    TEST(identify_no_match,                     "identify: no match"),
    TEST(identify_ad_longer_than_target,        "identify: ad longer than target"),
    TEST(identify_in_multisegment_target,       "identify: match outside the first segment      [B13]"),
    TEST(identify_many_matches,                 "identify: 400 matches                          [B13]"),

    TEST(wav_round_trip,                        "wav: save then load"),
    TEST(wav_header_is_8khz_mono_16bit,         "wav: header is 8 kHz mono 16-bit"),
    TEST(wav_missing_file_is_handled,           "wav: missing file                              [B14]"),
};

int main(void) {
    printf("Black-box tests\n===============\n");
    return run_tests(TESTS, N_TESTS(TESTS));
}
