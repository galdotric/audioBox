#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
//#include "sound_seg.h"
#include <assert.h>



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
    assert(seg != NULL);
    seg->samples = NULL;
    seg->length = 0;
    seg->next = NULL;  // Initialize next pointer
    return seg;
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

    printf("dest_curr: %p\n", dest_curr);
    printf("dest_prev: %p\n", dest_prev);
    printf("current_pos: %zu\n", current_pos);

    while (dest_curr != NULL && (current_pos + dest_curr->length) <= destpos && dest_prev != NULL) {
        current_pos += dest_curr->length;
        dest_prev = dest_curr;
        dest_curr = dest_curr->next;
    }

    //print all nesesary pointers
    printf("after while loop\n");
    printf("dest_curr: %p\n", dest_curr);
    printf("dest_prev: %p\n", dest_prev);
    printf("current_pos: %zu\n", current_pos);

    // 5->4->3
    // 5->3->1>3
    // 5->3->(new)->1->3
    
    //if (dest_curr == NULL) exit(1);
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
    //print the sample values
    printf("dest_track: ");
    struct sound_seg* current = dest_track;
    while (current != NULL) {
        for (size_t i = 0; i < current->length; i++) {
            printf("%d ", current->samples[i]);
        }
        current = current->next;
    }
    printf("\nlength: %zu\n", tr_length(dest_track));
    return;
}



// void test_tr_insert() {
//     // 1. Load the source WAV file (target.wav)
//     struct sound_seg* src_track = malloc(sizeof(struct sound_seg));
//     assert(src_track != NULL);
//     src_track->length = get_wav_sample_count("target.wav");
//     src_track->samples = malloc(src_track->length * sizeof(int16_t));
//     assert(src_track->samples != NULL);
//     src_track->next = NULL;
//     wav_load("target.wav", src_track->samples);
    
//     // Print source file info
//     FILE* src_file = fopen("target.wav", "rb");
//     waveheader src_header;
//     fread(&src_header, sizeof(waveheader), 1, src_file);
//     fclose(src_file);
//     printf("Source file (target.wav):\n");
//     printf("  Sample Rate: %u Hz\n", src_header.sampleRate);
//     printf("  Channels: %u\n", src_header.numChannels);
//     printf("  Bits per Sample: %u\n", src_header.bitsPerSample);
//     printf("  Sample Count: %zu\n", src_track->length);
//     printf("  Duration: %.3f seconds\n", (float)src_track->length / src_header.sampleRate);

//     // 2. Load the destination WAV file (bee.wav)
//     struct sound_seg* dest_track = malloc(sizeof(struct sound_seg));
//     assert(dest_track != NULL);
//     dest_track->length = get_wav_sample_count("bee.wav");
//     dest_track->samples = malloc(dest_track->length * sizeof(int16_t));
//     assert(dest_track->samples != NULL);
//     dest_track->next = NULL;
//     wav_load("bee.wav", dest_track->samples);
    
//     // Print destination file info
//     FILE* dest_file = fopen("bee.wav", "rb");
//     waveheader dest_header;
//     fread(&dest_header, sizeof(waveheader), 1, dest_file);
//     fclose(dest_file);
//     printf("Destination file (bee.wav):\n");
//     printf("  Sample Rate: %u Hz\n", dest_header.sampleRate);
//     printf("  Channels: %u\n", dest_header.numChannels);
//     printf("  Bits per Sample: %u\n", dest_header.bitsPerSample);
//     printf("  Sample Count: %zu\n", dest_track->length);
//     printf("  Duration: %.3f seconds\n", (float)dest_track->length / dest_header.sampleRate);

//     // 3. Test insertion (insert full target.wav at 2 seconds into bee.wav)
//     size_t destpos = dest_header.sampleRate * 2;  // 2 seconds in samples (16000)
//     size_t srcpos = 0;                            // Start of source
//     size_t len = src_track->length;               // Full length of source (10000)
//     if (destpos > dest_track->length) {
//         printf("Error: Insertion position (%zu) exceeds destination length (%zu)\n", destpos, dest_track->length);
//         goto cleanup;
//     }
//     tr_insert(src_track, dest_track, destpos, srcpos, len);

//     // 4. Verify the total length
//     size_t total_length = tr_length(dest_track);
//     printf("Resulting total sample count: %zu\n", total_length);
//     printf("Resulting duration: %.3f seconds\n", (float)total_length / dest_header.sampleRate);

//     // 5. Save the result
//     int16_t* output_buffer = malloc(total_length * sizeof(int16_t));
//     assert(output_buffer != NULL);
    
//     struct sound_seg* current = dest_track;
//     size_t offset = 0;
//     while (current != NULL) {
//         memcpy(output_buffer + offset, current->samples, current->length * sizeof(int16_t));
//         offset += current->length;
//         current = current->next;
//     }
//     wav_save("output.wav", output_buffer, total_length);

//     // 6. Clean up
// cleanup:
//     free(output_buffer);
//     tr_destroy(src_track);
    
//     current = dest_track;
//     while (current != NULL) {
//         struct sound_seg* next = current->next;
//         tr_destroy(current);
//         current = next;
//     }

//     printf("tr_insert test completed\n");
// }


// int main(void) {
//     test_sound_seg_operations();
//     return 0;
// }