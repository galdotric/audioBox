/*
 * audiobox: a small command-line program built on the sound_seg library.
 *
 *   audiobox                                  run the demo (writes to out/ next to the program)
 *   audiobox remove-ads <in> <ad> <out>       cut every copy of <ad> out of <in>
 *
 * All files are 16-bit mono WAV. Output is saved at 8000 Hz.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "sound_seg.h"

#define RATE 8000
#define PI 3.14159265358979323846

/* ===================================================================== */
/* Helpers                                                               */
/* ===================================================================== */

static struct sound_seg* load_track(const char* path) {
    size_t n = get_wav_sample_count(path);
    if (n == 0) {
        fprintf(stderr, "audiobox: can't read %s (missing, empty or not a WAV file)\n", path);
        exit(1);
    }
    int16_t* buf = malloc(n * sizeof(int16_t));
    wav_load(path, buf);
    struct sound_seg* track = tr_init();
    tr_write(track, buf, 0, n);
    free(buf);
    return track;
}

static void save_track(struct sound_seg* track, const char* path) {
    size_t n = tr_length(track);
    int16_t* buf = malloc((n ? n : 1) * sizeof(int16_t));
    tr_read(track, buf, 0, n);
    wav_save(path, buf, n);
    free(buf);
    if (n > 0 && get_wav_sample_count(path) != n) {
        fprintf(stderr, "audiobox: can't write %s\n", path);
        exit(1);
    }
}

static double seconds(size_t samples) {
    return (double)samples / RATE;
}

/* Find every copy of `ad` in `track` and delete it. Returns how many were cut. */
static int remove_ads(struct sound_seg* track, struct sound_seg* ad) {
    char* found = tr_identify(track, ad);

    /* Collect the matches first: deleting shifts everything after it, so the
     * cuts are made from the last match back to the first. */
    size_t starts[256], ends[256];
    int count = 0;
    for (char* line = strtok(found, "\n"); line && count < 256; line = strtok(NULL, "\n")) {
        if (sscanf(line, "%zu,%zu", &starts[count], &ends[count]) == 2) count++;
    }
    free(found);

    for (int i = 0; i < count; i++)
        printf("  found ad at %6.2f s - %6.2f s\n", seconds(starts[i]), seconds(ends[i] + 1));

    for (int i = count - 1; i >= 0; i--)
        tr_delete_range(track, starts[i], ends[i] - starts[i] + 1);
    return count;
}

/* ===================================================================== */
/* remove-ads                                                            */
/* ===================================================================== */

static int cmd_remove_ads(const char* in_path, const char* ad_path, const char* out_path) {
    struct sound_seg* track = load_track(in_path);
    struct sound_seg* ad = load_track(ad_path);

    printf("%s: %.2f s, ad %s: %.2f s\n", in_path, seconds(tr_length(track)),
           ad_path, seconds(tr_length(ad)));
    int cut = remove_ads(track, ad);
    save_track(track, out_path);
    printf("Removed %d ad%s. Saved %s (%.2f s).\n", cut, cut == 1 ? "" : "s",
           out_path, seconds(tr_length(track)));

    tr_destroy(ad);
    tr_destroy(track);
    return 0;
}

/* ===================================================================== */
/* Demo                                                                  */
/* ===================================================================== */

/* A gentle 6-second melody: one note every half second. */
static struct sound_seg* make_show(void) {
    static const double notes[] = {262, 294, 330, 349, 392, 440, 494, 523, 494, 440, 392, 349};
    size_t note_len = RATE / 2;
    size_t n = note_len * 12;
    int16_t* buf = malloc(n * sizeof(int16_t));
    for (size_t i = 0; i < n; i++)
        buf[i] = (int16_t)(4000 * sin(2 * PI * notes[i / note_len] * i / RATE));
    struct sound_seg* t = tr_init();
    tr_write(t, buf, 0, n);
    free(buf);
    return t;
}

/* A loud one-second "jingle": a rising chirp from 600 Hz to 2400 Hz. */
static struct sound_seg* make_ad(void) {
    size_t n = RATE;
    int16_t* buf = malloc(n * sizeof(int16_t));
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / RATE;
        buf[i] = (int16_t)(12000 * sin(2 * PI * (600 * t + 900 * t * t)));
    }
    struct sound_seg* t = tr_init();
    tr_write(t, buf, 0, n);
    free(buf);
    return t;
}

static int same_samples(struct sound_seg* a, struct sound_seg* b) {
    size_t n = tr_length(a);
    if (n != tr_length(b)) return 0;
    int16_t* x = malloc(n * sizeof(int16_t));
    int16_t* y = malloc(n * sizeof(int16_t));
    tr_read(a, x, 0, n);
    tr_read(b, y, 0, n);
    int same = memcmp(x, y, n * sizeof(int16_t)) == 0;
    free(x);
    free(y);
    return same;
}

/* The demo writes to out/ in the same folder as the program, so it works
 * whichever directory it is started from. */
static void out_path(char* dest, size_t size, const char* dir, const char* name) {
    snprintf(dest, size, "%s/%s", dir, name);
}

static int cmd_demo(const char* program) {
    char dir[1024], p1[1100], p2[1100], p3[1100], p4[1100];
    const char* slash = strrchr(program, '/');
    if (slash)
        snprintf(dir, sizeof dir, "%.*s/out", (int)(slash - program), program);
    else
        snprintf(dir, sizeof dir, "out");
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "audiobox: can't create %s\n", dir);
        return 1;
    }
    out_path(p1, sizeof p1, dir, "1_show.wav");
    out_path(p2, sizeof p2, dir, "2_ad.wav");
    out_path(p3, sizeof p3, dir, "3_with_ads.wav");
    out_path(p4, sizeof p4, dir, "4_ads_removed.wav");

    printf("1. Making a 6 s melody and a 1 s ad jingle\n");
    struct sound_seg* show = make_show();
    struct sound_seg* ad = make_ad();
    save_track(show, p1);
    save_track(ad, p2);

    printf("2. Inserting the ad at 2 s and 4 s (shared, not copied)\n");
    struct sound_seg* broadcast = make_show();
    /* A source can be shared only once, so the second insert uses a second
     * track holding the same jingle. */
    struct sound_seg* ad_again = make_ad();
    tr_insert(ad, broadcast, 2 * RATE, 0, tr_length(ad));
    tr_insert(ad_again, broadcast, 5 * RATE, 0, tr_length(ad_again));   /* 4 s + the first ad */
    save_track(broadcast, p3);
    printf("   broadcast is now %.2f s\n", seconds(tr_length(broadcast)));

    printf("3. Loading 3_with_ads.wav back and cutting the ads out\n");
    struct sound_seg* loaded = load_track(p3);
    int cut = remove_ads(loaded, ad);
    save_track(loaded, p4);

    int ok = cut == 2 && same_samples(loaded, show);
    printf("4. Removed %d ads. Result is %.2f s and %s the original melody.\n",
           cut, seconds(tr_length(loaded)), ok ? "matches" : "does NOT match");
    printf("\nListen to the files in %s/:\n"
           "  1_show.wav  2_ad.wav  3_with_ads.wav  4_ads_removed.wav\n", dir);

    tr_destroy(loaded);
    tr_destroy(broadcast);
    tr_destroy(ad_again);
    tr_destroy(ad);
    tr_destroy(show);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 1) return cmd_demo(argv[0]);
    if (argc == 5 && strcmp(argv[1], "remove-ads") == 0)
        return cmd_remove_ads(argv[2], argv[3], argv[4]);

    fprintf(stderr, "Usage:\n"
                    "  %s                              run the demo\n"
                    "  %s remove-ads <in> <ad> <out>   cut every copy of <ad> out of <in>\n",
            argv[0], argv[0]);
    return 1;
}
