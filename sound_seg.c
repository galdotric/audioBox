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
    struct sound_seg* next;
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
    if(!seg) exit(1);
    seg->samples = NULL;
    seg->length = 0;
    
    return NULL;
}

void tr_destroy(struct sound_seg* obj) {
    if(obj){
        free(obj->samples);
        free(obj);
    }
    return;
}

// seg1->seg2->seg3

size_t tr_length(struct sound_seg* track) {
    size_t total_length = 0;
    struct sound_seg* curr = track;
    while (curr != NULL) {
        total_length += curr->length;
        curr = curr->next;
    }
    return total_length;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if(pos+len > track->length) exit(1);
    memcpy(dest,track->samples+pos,len*sizeof(int16_t));
    return;
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if(!track || !src) exit(1);
    if(pos+len > track->length){
        int16_t* new_track = realloc(track->samples,(pos+len)*sizeof(int16_t));
        if(!new_track) exit(1);
        track->samples = new_track;
        memset(track->samples + track->length, 0, (len+pos - track->length) * sizeof(int16_t));
        track->length = pos+len;
    }
    memcpy(track->samples+pos,src,len*sizeof(int16_t));
    
    return;
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if(!track ) return false;
    if(pos+len > track->length) return false;
    if(len == 0) return true;
    memmove(track->samples + pos, track->samples + pos + len, (track->length - (pos + len)) * sizeof(int16_t));
    track->length -= len;
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

char* tr_identify(struct sound_seg *target, struct sound_seg *ad) {
    if (!target || !ad || target->length < ad->length) return strdup("");

    size_t target_len = target->length;
    size_t ad_len = ad->length;

    double Rxx0 = autocorrelation(ad);
    double threshold = 0.95 * Rxx0;

    char *result = malloc(1024);  
    if (!result) return NULL;
    result[0] = '\0'; 

    int match_found = 0;

    for (size_t start = 0; start <= target_len - ad_len; start++) {
        double Rxy = cross_correlation(target, ad, start);

        if (Rxy >= threshold) {
            if (match_found) strcat(result, "\n");
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "%zu,%zu", start, start + ad_len - 1);
            strcat(result, buffer);
            match_found = 1;
            start += ad_len - 1; 
        }
    }

    return result;  
}

void tr_insert(const struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track) exit(1);
    if (srcpos + len > src_track->length) exit(1); 
    if (destpos > tr_length(dest_track)) exit(1);

    // Traverse to the insertion point in the destination track
    // seg1->seg2->seg3
    struct sound_seg* dest_prev = NULL;
    struct sound_seg* dest_curr = dest_track;
    size_t current_pos = 0;
    size_t accumulated_length = 0;

    while (dest_curr != NULL && (current_pos + dest_curr->length) <= destpos) {
        current_pos += dest_curr->length;
        dest_prev = dest_curr;
        dest_curr = dest_curr->next;
    }

    // 5->4->3
    // 5->3->1>3
    // 5->3->(new)->1->3
    
    if (dest_curr == NULL) exit(1);
    size_t split_offset = destpos - current_pos; //3
    struct sound_seg* new_segment = malloc(sizeof(struct sound_seg));
    // buiding the new seg
    new_segment->samples = malloc(sizeof(int16_t) * (dest_curr->length - split_offset));
    if (new_segment->samples == NULL) {
        free(new_segment);
        return;
    }
    // Copy the second half of samples to the new segment
    memcpy(new_segment->samples, dest_curr->samples + split_offset, sizeof(int16_t) * (dest_curr->length - split_offset));
    new_segment->length = dest_curr->length - split_offset;
    // resize dest curr
    dest_curr->length = split_offset;
       
    // Link the segments
    new_segment->next = dest_curr->next;
    dest_curr->next = new_segment;


    // Create the new segment from src_track to insert
    // 1 2 3 4 5 6 7 8
    struct sound_seg* new_seg = malloc(sizeof(struct sound_seg));
    if (!new_seg) exit(1);

    new_seg->samples = malloc(len * sizeof(int16_t));  // Allocate memory for samples
    if (!new_seg->samples) exit(1);

    // Copy the portion of samples from src_track to the new segment
    memcpy(new_seg->samples, src_track->samples + srcpos, len * sizeof(int16_t));
    new_seg->length = len;
    new_seg->next = NULL;

    // 5->dest_curr->new_seg->new_segment->3
    // ()

    dest_curr->next = new_seg;
    new_seg->next = new_segment;
    return;
}

// int main() {
//     // Load target (1s) and bee (5s) files into raw sample arrays
//     size_t target_len = get_wav_sample_count("target.wav");
//     int16_t* target_samples = malloc(sizeof(int16_t) * target_len);
//     wav_load("target.wav", target_samples);

//     size_t bee_len = get_wav_sample_count("bee.wav");
//     int16_t* bee_samples = malloc(sizeof(int16_t) * bee_len);
//     wav_load("bee.wav", bee_samples);

//     // Wrap samples in sound_seg structs
//     struct sound_seg* target_seg = malloc(sizeof(struct sound_seg));
//     target_seg->samples = target_samples;
//     target_seg->length = target_len;
//     target_seg->next = NULL;

//     struct sound_seg* bee_seg = malloc(sizeof(struct sound_seg));
//     bee_seg->samples = bee_samples;
//     bee_seg->length = bee_len;
//     bee_seg->next = NULL;

//     // Insert 1s (target_seg) into middle of 5s (bee_seg)
//     size_t insert_pos = bee_len / 2;
//     tr_insert(target_seg, bee_seg, insert_pos, 0, target_len);

//     // Flatten the linked segments into a continuous array to save
//     size_t out_len = tr_length(bee_seg);
//     int16_t* out_samples = malloc(sizeof(int16_t) * out_len);

//     struct sound_seg* curr = bee_seg;
//     size_t index = 0;
//     while (curr) {
//         memcpy(out_samples + index, curr->samples, curr->length * sizeof(int16_t));
//         index += curr->length;
//         curr = curr->next;
//     }

//     // Save to file
//     wav_save("out.wav", out_samples, out_len);

//     // Cleanup
//     free(out_samples);
//     tr_destroy(target_seg);
//     while (bee_seg) {
//         struct sound_seg* next = bee_seg->next;
//         tr_destroy(bee_seg);
//         bee_seg = next;
//     }

//     return 0;
// }

