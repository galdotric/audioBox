#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
//#include "sound_seg.h"



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


struct sound_seg {
    int16_t* samples;
    size_t length;
};


// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){
    FILE* file = fopen(filename, "rb");

    waveheader header;
    fread(&header, sizeof(waveheader),1,file);

    size_t sample_count = header.subchunk2Size/sizeof(int16_t);
    fread(dest, sizeof(int16_t),sample_count,file);

    fclose(file);

    return;
}

// Get the number of samples in the WAV file
size_t get_wav_sample_count(char* filename){
    FILE* file = fopen(filename, "rb");
    waveheader header;
    fread(&header, sizeof(waveheader), 1, file);

    // Number of samples = subchunk2Size / blockAlign
    return header.subchunk2Size / header.blockAlign;
}


// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    FILE* file = fopen(fname, "wb");

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
    header.byteRate = header.sampleRate * header.numChannels * header.bitsPerSample/8;
    header.blockAlign = header.numChannels * header.bitsPerSample/8;
    header.chunkSize = 36 + len * sizeof(int16_t);
    header.subchunk2Size = len * sizeof(int16_t);

    fwrite(&header, sizeof(waveheader), 1, file);
    fwrite(src, sizeof(int16_t), len, file);

    fclose(file);

    return;
}

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    struct sound_seg* seg = malloc(sizeof(struct sound_seg));
    (seg == NULL) ? exit(1) : 0;
    seg->samples = NULL;
    seg->length = 0;

    return seg;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* obj) {
    if(obj){
        free(obj->samples);
        free(obj);
    }
    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    return seg -> length;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    (pos + len > track->length) ? exit(1) : 0;
    memcpy(dest, track->samples + pos, len * sizeof(int16_t));
    return;
}

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    size_t new_len = pos + len;

    if(new_len > track->length){
        int16_t* new_samples = realloc(track->samples, new_len * sizeof(int16_t));
        (new_samples == NULL) ? exit(1) : 0;

        //zero out block of memory to overwrite
        for(size_t i = track->length; i < new_len; i++){
            new_samples[i] = 0;
        }

        track->samples = new_samples;
        track->length = new_len;
    }

    memcpy(track->samples + pos, src, len * sizeof(int16_t));
    
    return;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (pos + len > track->length) {
        return false;
    }

    if(len == 0){
        return true;
    }

    memmove(track -> samples + pos,
        track -> samples + pos +len,
        (track -> length - pos -len)*sizeof(int16_t));
    track -> length -= len;
    return true;
}

double cross_correlation(struct sound_seg *target, struct sound_seg *ad, size_t k) {
    double correlation = 0.0;
    for (size_t i = 0; i < ad->length; i++) {
        correlation += (double)target->samples[k + i] * ad->samples[i];
    }
    return correlation;
}

double autocorrelation(struct sound_seg *ad) {
    double correlation = 0.0;
    for (size_t i = 0; i < ad->length; i++) {
        correlation += (double)ad->samples[i] * ad->samples[i];
    }
    return correlation;
}

char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (ad->length == 0 || target->length < ad->length) {
        char* result = malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    double ad_autocorr = autocorrelation(ad);
    double threshold = 0.95 * ad_autocorr;
    size_t* starts = malloc(10 * sizeof(size_t));
    size_t capacity = 10, count = 0;
    size_t k = 0;
    while (k <= target->length - ad->length) {
        double corr = cross_correlation(target, ad, k);
        if (corr >= threshold) {
            if (count == capacity) {
                capacity *= 2;
                size_t* temp = realloc(starts, capacity * sizeof(size_t));
                if (!temp) exit(1);
                starts = temp;
            }
            starts[count++] = k;
            k += ad->length;
        } else {
            k++;
        }
    }
    if (count == 0) {
        free(starts);
        char* result = malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    size_t total_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t end = starts[i] + ad->length - 1;
        total_len += snprintf(NULL, 0, "%zu,%zu", starts[i], end);
        if (i < count - 1) total_len += 1;
    }
    char* result = malloc(total_len + 1);
    if (!result) exit(1);
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        size_t end = starts[i] + ad->length - 1;
        pos += snprintf(result + pos, total_len + 1 - pos, "%zu,%zu", starts[i], end);
        if (i < count - 1) result[pos++] = '\n';
    }
    result[total_len] = '\0';
    free(starts);
    return result;
}



// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
    struct sound_seg* dest_track, size_t destpos,
    size_t srcpos, size_t len) {
    return;
}

// int main() {
//     size_t target_sample_count = get_wav_sample_count("target.wav");
//     size_t ad_sample_count = get_wav_sample_count("ad.wav");
//     int16_t* target_buffer = malloc(target_sample_count * sizeof(int16_t));
//     int16_t* ad_buffer = malloc(ad_sample_count * sizeof(int16_t));
//     if (!target_buffer || !ad_buffer) {
//         perror("Failed to allocate buffers");
//         exit(1);
//     }
//     wav_load("target.wav", target_buffer);
//     wav_load("ad.wav", ad_buffer);
//     struct sound_seg* target_seg = tr_init();
//     struct sound_seg* ad_seg = tr_init();
//     tr_write(target_seg, target_buffer, 0, target_sample_count);
//     tr_write(ad_seg, ad_buffer, 0, ad_sample_count);
//     char* result = tr_identify(target_seg, ad_seg);
//     printf("%s\n", result);
//     free(target_buffer);
//     free(ad_buffer);
//     free(result);
//     tr_destroy(target_seg);
//     tr_destroy(ad_seg);
//     return 0;

// }
