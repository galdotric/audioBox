#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A track is a linked list of segments. The pointer returned by tr_init is
 * the first segment and stays the track's handle for its whole life.
 *
 * A segment owns its samples unless is_child is set, in which case the
 * samples belong to another segment (the parent) and are shared with it.
 */
struct sound_seg {
    int16_t* samples;
    size_t length;
    struct sound_seg* next;
    bool is_parent;   /* some of these samples are shared by a child */
    bool is_child;    /* these samples are borrowed, not owned */
};

/* Tracks */
struct sound_seg* tr_init(void);
void tr_destroy(struct sound_seg* track);
size_t tr_length(struct sound_seg* track);
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len);
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len);
int tr_check_parent_child(struct sound_seg* src_track, struct sound_seg* dest_track,
                          size_t destpos, size_t srcpos, size_t len);
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);

/* WAV files (PCM, mono, 16-bit; saved at 8000 Hz) */
void wav_load(const char* filename, int16_t* dest);
size_t get_wav_sample_count(const char* filename);
void wav_save(const char* filename, int16_t* src, size_t len);

#endif /* SOUND_SEG_H */
