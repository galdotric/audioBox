#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

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
    free(obj->samples);
    free(obj);
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

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad){

    if(ad->length == 0 || target->length < ad->length){
        char* result = malloc(1);
        result[0] = '\0';
    }

    size_t buffer_size = 128;
    char* result = malloc(buffer_size);
    (!result)? exit(1) : 0;

    size_t offset = 0;

    for(size_t i = 0; i <= target->length; i++){
        bool match = 1;

        for (size_t j = 0; j < ad->length; j++) {
            if (target->samples[i + j] != ad->samples[j]) {
                match = false;
                break;
            }
        }
        if(match){
            size_t start = i;
            size_t end = i + ad->length-1;
            
            if(offset +30 >= buffer_size){
                buffer_size *= 2;
                char *temp = realloc(result, buffer_size);
                if(!temp){
                    free(result);
                    return NULL;
                }
                result = temp;
            }

            int written = snprintf(result + offset, buffer_size - offset, "%zu,%zu\n", start, end);
            offset += written;
            i += ad->length -1;
        }
    }
    // remove trailing newline if exists
    result[offset] = '\0';
    
    
    return result;
}

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
    struct sound_seg* dest_track, size_t destpos,
    size_t srcpos, size_t len) {
    return;
}

// int main() {
//     struct sound_seg* target = tr_init();
//     struct sound_seg* ad = tr_init();

//     int16_t target_samples[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5};
//     int16_t ad_samples[] = {1, 2, 3, 4, 5};
    
//     target->samples = malloc(sizeof(target_samples));
//     ad->samples = malloc(sizeof(ad_samples));
    
//     memcpy(target->samples, target_samples, sizeof(target_samples));
//     memcpy(ad->samples, ad_samples, sizeof(ad_samples));
    
//     target->length = sizeof(target_samples) / sizeof(int16_t);
//     ad->length = sizeof(ad_samples) / sizeof(int16_t);

//     char* result = tr_identify(target, ad);

//     if (result != NULL) {
//         printf("Identified positions: \n%s", result);
//         free(result);
//     } else {
//         printf("No matches found.\n");
//     }

//     // Cleanup
//     tr_destroy(target);
//     tr_destroy(ad);

//     return 0;
// }
