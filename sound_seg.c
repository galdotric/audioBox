#include "sound_seg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== */
/* WAV files                                                             */
/* ===================================================================== */

typedef struct {
    /* RIFF chunk descriptor */
    char chunkID[4];
    uint32_t chunkSize;
    char format[4];
    /* fmt sub-chunk */
    char subchunk1ID[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    /* data sub-chunk */
    char subchunk2ID[4];
    uint32_t subchunk2Size;
} waveheader;

/* Load the samples of a WAV file into dest. dest must hold
 * get_wav_sample_count(filename) samples. A missing file leaves dest unchanged. */
void wav_load(const char* filename, int16_t* dest) {
    FILE* file = fopen(filename, "rb");
    if (!file) return;

    waveheader header;
    if (fread(&header, sizeof header, 1, file) == 1) {
        size_t count = header.subchunk2Size / sizeof(int16_t);
        size_t got = fread(dest, sizeof(int16_t), count, file);
        (void)got;   /* a short file simply loads fewer samples */
    }
    fclose(file);
}

/* Number of samples in a WAV file, or 0 if it can't be read. */
size_t get_wav_sample_count(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return 0;

    waveheader header;
    size_t count = 0;
    if (fread(&header, sizeof header, 1, file) == 1 && header.blockAlign != 0)
        count = header.subchunk2Size / header.blockAlign;
    fclose(file);
    return count;
}

/* Write len samples as a PCM, mono, 16-bit, 8000 Hz WAV file. */
void wav_save(const char* filename, int16_t* src, size_t len) {
    FILE* file = fopen(filename, "wb");
    if (!file) return;

    waveheader header;
    memcpy(header.chunkID, "RIFF", 4);
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1ID, "fmt ", 4);
    memcpy(header.subchunk2ID, "data", 4);
    header.subchunk1Size = 16;
    header.audioFormat = 1;
    header.numChannels = 1;
    header.sampleRate = 8000;
    header.bitsPerSample = 16;
    header.byteRate = header.sampleRate * header.numChannels * header.bitsPerSample / 8;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.subchunk2Size = (uint32_t)(len * sizeof(int16_t));
    header.chunkSize = 36 + header.subchunk2Size;

    fwrite(&header, sizeof header, 1, file);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
}

/* ===================================================================== */
/* Segment helpers                                                       */
/* ===================================================================== */

static size_t min_size(size_t a, size_t b) {
    return a < b ? a : b;
}

static struct sound_seg* seg_new(int16_t* samples, size_t length, bool is_parent,
                                 bool is_child, struct sound_seg* next) {
    struct sound_seg* seg = malloc(sizeof *seg);
    if (!seg) exit(1);
    seg->samples = samples;
    seg->length = length;
    seg->next = next;
    seg->is_parent = is_parent;
    seg->is_child = is_child;
    return seg;
}

static bool seg_is_shared(const struct sound_seg* seg) {
    return seg->is_parent || seg->is_child;
}

/* Free a segment's samples if it owns them. */
static void seg_free_samples(struct sound_seg* seg) {
    if (!seg->is_child) free(seg->samples);
    seg->samples = NULL;
}

/* The segment holding sample `pos` of the track, and the offset of that
 * sample inside it. Empty segments are skipped. NULL if pos is past the end. */
static struct sound_seg* seg_at(struct sound_seg* track, size_t pos, size_t* offset) {
    struct sound_seg* seg = track;
    while (seg && pos >= seg->length) {
        pos -= seg->length;
        seg = seg->next;
    }
    *offset = pos;
    return seg;
}

/* Make `pos` a segment boundary by splitting the segment that contains it.
 *
 * A segment that shares its samples (parent or child) is split without
 * copying: the new tail points into the same buffer and does not own it, so
 * every other segment that points into that buffer stays valid. An ordinary
 * segment is split by copying its tail into a new buffer. */
static void split_at(struct sound_seg* track, size_t pos) {
    size_t offset;
    struct sound_seg* seg = seg_at(track, pos, &offset);
    if (!seg || offset == 0) return;

    size_t tail_len = seg->length - offset;
    struct sound_seg* tail;
    if (seg_is_shared(seg)) {
        tail = seg_new(seg->samples + offset, tail_len, seg->is_parent, true, seg->next);
    } else {
        int16_t* copy = malloc(tail_len * sizeof(int16_t));
        if (!copy) exit(1);
        memcpy(copy, seg->samples + offset, tail_len * sizeof(int16_t));
        tail = seg_new(copy, tail_len, false, false, seg->next);
    }
    seg->length = offset;
    seg->next = tail;
}

/* Is any segment overlapping samples [pos, pos + len) shared? Only segments
 * matching `parent_only` are considered when it is set. */
static bool range_has_shared(struct sound_seg* track, size_t pos, size_t len, bool parent_only) {
    size_t start = 0;
    for (struct sound_seg* seg = track; seg && start < pos + len; seg = seg->next) {
        size_t end = start + seg->length;
        if (seg->length > 0 && end > pos) {
            if (parent_only ? seg->is_parent : seg_is_shared(seg)) return true;
        }
        start = end;
    }
    return false;
}

/* ===================================================================== */
/* Tracks                                                                */
/* ===================================================================== */

struct sound_seg* tr_init(void) {
    struct sound_seg* seg = malloc(sizeof *seg);
    assert(seg != NULL);
    seg->samples = NULL;
    seg->length = 0;
    seg->next = NULL;
    seg->is_parent = false;
    seg->is_child = false;
    return seg;
}

/* Free every segment of the track. Borrowed (child) samples are left to their
 * owner. Destroy every track that borrows from a track before or after it; the
 * borrowing track must just not be read once its source is gone. */
void tr_destroy(struct sound_seg* track) {
    while (track) {
        struct sound_seg* next = track->next;
        seg_free_samples(track);
        free(track);
        track = next;
    }
}

size_t tr_length(struct sound_seg* track) {
    size_t total = 0;
    for (struct sound_seg* seg = track; seg; seg = seg->next) total += seg->length;
    return total;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track) exit(1);
    size_t total = tr_length(track);
    if (pos > total || len > total - pos) {
        printf("Read failed: Position %zu + length %zu exceeds track length %zu\n", pos, len, total);
        exit(1);
    }

    size_t offset;
    struct sound_seg* seg = seg_at(track, pos, &offset);
    size_t done = 0;
    while (done < len) {
        size_t n = min_size(seg->length - offset, len - done);
        memcpy(dest + done, seg->samples + offset, n * sizeof(int16_t));
        done += n;
        offset = 0;              /* every later segment is read from its start */
        seg = seg->next;
    }
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src) exit(1);
    if (len == 0) return;

    /* Grow the track first if the write goes past its end. */
    size_t total = tr_length(track);
    if (pos + len > total) {
        size_t extra = pos + len - total;
        struct sound_seg* last = track;
        while (last->next) last = last->next;

        if (!seg_is_shared(last)) {
            /* Only the owner uses this buffer, so it can be moved. */
            int16_t* grown = realloc(last->samples, (last->length + extra) * sizeof(int16_t));
            if (!grown) exit(1);
            memset(grown + last->length, 0, extra * sizeof(int16_t));
            last->samples = grown;
            last->length += extra;
        } else {
            /* Other segments point into this buffer: add a new segment instead. */
            int16_t* fresh = calloc(extra, sizeof(int16_t));
            if (!fresh) exit(1);
            last->next = seg_new(fresh, extra, false, false, NULL);
        }
    }

    size_t offset;
    struct sound_seg* seg = seg_at(track, pos, &offset);
    size_t done = 0;
    while (done < len) {
        size_t n = min_size(seg->length - offset, len - done);
        memcpy(seg->samples + offset, src + done, n * sizeof(int16_t));
        done += n;
        offset = 0;
        seg = seg->next;
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track) return false;
    size_t total = tr_length(track);
    if (pos > total || len > total - pos) return false;
    if (len == 0) return true;

    if (range_has_shared(track, pos, len, true)) {
        printf("Deletion failed: Range contains a parent segment with children\n");
        return false;
    }

    /* After these two splits the range is made of whole segments. */
    split_at(track, pos);
    split_at(track, pos + len);

    /* Walk to position pos, then unlink whole segments until len samples are gone. */
    size_t remaining = len;
    size_t start = 0;
    struct sound_seg* prev = NULL;
    struct sound_seg* seg = track;
    while (seg && remaining > 0) {
        struct sound_seg* next = seg->next;

        if (start < pos || seg->length == 0) {
            start += seg->length;
            prev = seg;
            seg = next;
            continue;
        }

        remaining -= seg->length;   /* the splits make this a whole segment of the range */
        if (seg != track) {
            prev->next = next;
            seg_free_samples(seg);
            free(seg);
            seg = next;
        } else {
            /* The first segment is the caller's handle and must stay, so the
             * next segment's contents are moved into it instead. */
            seg_free_samples(track);
            if (next) {
                *track = *next;
                free(next);
            } else {
                track->length = 0;
                track->is_parent = false;
                track->is_child = false;
            }
        }
    }
    return true;
}

/* 1 if inserting src[srcpos, srcpos + len) into dest at destpos is allowed:
 *   - the source samples must not already be shared (parent or child), and
 *   - the dest samples on either side of destpos must not be shared. */
int tr_check_parent_child(struct sound_seg* src_track, struct sound_seg* dest_track,
                          size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track) return 0;
    size_t src_len = tr_length(src_track);
    size_t dest_len = tr_length(dest_track);
    if (srcpos > src_len || len > src_len - srcpos || destpos > dest_len) return 0;

    if (range_has_shared(src_track, srcpos, len, false)) return 0;
    if (destpos > 0 && range_has_shared(dest_track, destpos - 1, 1, false)) return 0;
    if (destpos < dest_len && range_has_shared(dest_track, destpos, 1, false)) return 0;
    return 1;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track) {
        printf("Insertion failed: Null track pointer\n");
        exit(1);
    }
    size_t src_len = tr_length(src_track);
    if (srcpos > src_len || len > src_len - srcpos) {
        printf("Insertion failed: Source position out of bounds\n");
        printf("Source position: %zu, Length: %zu, Total track length: %zu\n", srcpos, len, src_len);
        exit(1);
    }
    if (destpos > tr_length(dest_track)) {
        printf("Insertion failed: Destination position out of bounds\n");
        exit(1);
    }
    if (len == 0) return;
    if (!tr_check_parent_child(src_track, dest_track, destpos, srcpos, len)) {
        printf("Insertion failed: Parent samples or adjacent samples are already parent or child\n");
        return;
    }

    /* 1. Make destpos a segment boundary. Splitting never moves samples to a
     *    different position, so srcpos is still right for a self-insert. */
    split_at(dest_track, destpos);

    /* 2. Build a chain of child segments that point into the source samples,
     *    one per source segment the range touches. */
    struct sound_seg* chain = NULL;
    struct sound_seg* chain_end = NULL;
    size_t offset;
    struct sound_seg* seg = seg_at(src_track, srcpos, &offset);
    for (size_t done = 0; done < len; seg = seg->next, offset = 0) {
        size_t n = min_size(seg->length - offset, len - done);
        if (n == 0) continue;
        struct sound_seg* child = seg_new(seg->samples + offset, n, false, true, NULL);
        if (chain_end) chain_end->next = child; else chain = child;
        chain_end = child;
        seg->is_parent = true;
        done += n;
    }

    /* 3. Link the chain in at destpos. */
    if (destpos == 0) {
        /* The first segment is the caller's handle, so it stays first: its
         * contents move to a new segment that follows the chain. */
        if (dest_track->length > 0) {
            chain_end->next = seg_new(dest_track->samples, dest_track->length,
                                      dest_track->is_parent, dest_track->is_child,
                                      dest_track->next);
            dest_track->samples = NULL;
            dest_track->length = 0;
            dest_track->is_parent = false;
            dest_track->is_child = false;
        } else {
            chain_end->next = dest_track->next;
        }
        dest_track->next = chain;
    } else {
        struct sound_seg* before = dest_track;
        size_t end = before->length;
        while (end < destpos) {
            before = before->next;
            end += before->length;
        }
        chain_end->next = before->next;
        before->next = chain;
    }
}

/* ===================================================================== */
/* Ad detection                                                          */
/* ===================================================================== */

static int16_t* tr_flatten(struct sound_seg* track, size_t* len) {
    *len = tr_length(track);
    int16_t* buf = malloc((*len ? *len : 1) * sizeof(int16_t));
    if (!buf) exit(1);
    tr_read(track, buf, 0, *len);
    return buf;
}

/* Find where `ad` occurs in `target`. A match is an offset k where the
 * cross-correlation of target[k...] with ad is at least 95% of ad's
 * correlation with itself. Matches don't overlap.
 *
 * Returns a new string (caller frees) with one "start,end" line per match,
 * inclusive, or "" if there are none. */
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad) return strdup("");

    size_t t_len, a_len;
    int16_t* t = tr_flatten(target, &t_len);
    int16_t* a = tr_flatten(ad, &a_len);

    size_t cap = 64, used = 0;
    char* result = malloc(cap);
    if (!result) exit(1);
    result[0] = '\0';

    if (a_len > 0 && a_len <= t_len) {
        double self = 0.0;
        for (size_t i = 0; i < a_len; i++) self += (double)a[i] * a[i];
        double threshold = 0.95 * self;

        for (size_t k = 0; k + a_len <= t_len; k++) {
            double cross = 0.0;
            for (size_t i = 0; i < a_len; i++) cross += (double)t[k + i] * a[i];
            if (cross < threshold) continue;

            char line[48];
            int n = snprintf(line, sizeof line, "%s%zu,%zu", used ? "\n" : "", k, k + a_len - 1);
            while (used + (size_t)n + 1 > cap) {
                result = realloc(result, cap *= 2);
                if (!result) exit(1);
            }
            memcpy(result + used, line, (size_t)n + 1);
            used += (size_t)n;
            k += a_len - 1;     /* skip past this match */
        }
    }

    free(t);
    free(a);
    return result;
}
