#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Define struct sound_seg only here, not in sound_seg.c
struct sound_seg {
    int16_t* samples;
    size_t length;
    struct sound_seg* next;
    bool is_parent;
    bool is_child;
};

// Function declarations matching sound_seg.c exactly
struct sound_seg* tr_init();
void tr_destroy(struct sound_seg* obj);
size_t tr_length(struct sound_seg* track);
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len);
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len);
int tr_check_parent_child(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len);
void wav_load(const char* filename, int16_t* dest);
size_t get_wav_sample_count(char* filename);
void wav_save(const char* fname, int16_t* src, size_t len);

// Test function declaration
void test_sound_seg_operations(void);

#endif // SOUND_SEG_H