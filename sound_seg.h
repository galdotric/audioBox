#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct{
    // RIFF Chunk Descriptor
    char chunkID[4];
    uint32_t chunkSize;    
    char format[4];        

    // fmt Sub-chunk
    char subchunk1ID[4];   
    uint32_t subchunk1Size; 
    uint16_t audioFormat;   
    uint16_t numChannels;   
    uint32_t sampleRate;    
    uint32_t byteRate;      
    uint16_t blockAlign;    
    uint16_t bitsPerSample; 

    // data Sub-chunk
    char subchunk2ID[4];    
    uint32_t subchunk2Size; 
}waveheader;


// Sound segment structure
struct sound_seg {
    int16_t* samples;
    size_t length;
};

// Function prototypes

// Load a WAV file into a buffer
void wav_load(const char* filename, int16_t* dest);

// Get the number of samples in the WAV file
size_t get_wav_sample_count(char* filename);

// Create and write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len);

// Initialize a new sound segment object
struct sound_seg* tr_init();

// Destroy a sound segment object and free all allocated memory
void tr_destroy(struct sound_seg* obj);

// Return the length of the segment
size_t tr_length(struct sound_seg* seg);

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len);

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);

// Perform cross-correlation between target and ad
int64_t cross_correlation(struct sound_seg* target, struct sound_seg* ad, size_t k);

// Identify segments of target matching ad
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);

// // Insert a portion of src_track into dest_track at position destpos
// void tr_insert(struct sound_seg* src_track,
//                struct sound_seg* dest_track, size_t destpos,
//                size_t srcpos, size_t len);

#endif // SOUND_SEG_H
