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


typedef struct sample_node {
    int16_t value;
    struct sample_node* next;
} sample_node;

struct sound_seg {
    sample_node* head;
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
    if (seg == NULL) exit(1);
    seg->head = NULL;
    seg->length = 0;
    return seg;
}


// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* obj) {
    if (obj) {
        sample_node* current = obj->head;
        while (current) {
            sample_node* tmp = current;
            current = current->next;
            free(tmp);
        }
        free(obj);
    }
}


// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    return seg -> length;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest) return;

    sample_node* curr = track->head;
    size_t i = 0;

    while (i < pos && curr) {
        curr = curr->next;
        i++;
    }

    for (i = 0; i < len && curr; i++) {
        dest[i] = curr->value;
        curr = curr->next;
    }
}


// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;

    size_t i = 0;
    sample_node* curr = track->head;
    sample_node* prev = NULL;

    // Traverse to pos
    for (i = 0; i < pos && curr; i++) {
        prev = curr;
        curr = curr->next;
    }

    // Add/overwrite len samples
    for (i = 0; i < len; i++) {
        if (curr) {
            curr->value = src[i];
            prev = curr;
            curr = curr->next;
        } else {
            sample_node* new_node = malloc(sizeof(sample_node));
            new_node->value = src[i];
            new_node->next = NULL;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            prev = new_node;
            track->length++;
        }
    }
}


// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos + len > track->length) {
        return false;
    }

    if (len == 0) {
        return true;
    }

    sample_node* curr = track->head;
    sample_node* prev = NULL;
    size_t i = 0;

    // Traverse to position pos
    while (i < pos && curr) {
        prev = curr;
        curr = curr->next;
        i++;
    }

    // Delete len nodes
    for (i = 0; i < len && curr; i++) {
        sample_node* temp = curr;
        curr = curr->next;
        free(temp);
        track->length--;
    }

    // Reconnect the list
    if (prev) {
        prev->next = curr;
    } else {
        track->head = curr;
    }

    return true;
}

// Compute cross-correlation between target and ad at offset k
double cross_correlation(struct sound_seg* target, struct sound_seg* ad, size_t k) {
    double correlation = 0.0;
    if (!target || !ad || k + ad->length > target->length) {
        return correlation;
    }

    sample_node* target_curr = target->head;
    sample_node* ad_curr = ad->head;

    // Move target_curr to position k
    for (size_t i = 0; i < k && target_curr; i++) {
        target_curr = target_curr->next;
    }

    // Compute correlation
    for (size_t i = 0; i < ad->length && target_curr && ad_curr; i++) {
        correlation += (double)target_curr->value * ad_curr->value;
        target_curr = target_curr->next;
        ad_curr = ad_curr->next;
    }

    return correlation;
}

// Compute autocorrelation of ad
double autocorrelation(struct sound_seg* ad) {
    double correlation = 0.0;
    if (!ad) {
        return correlation;
    }

    sample_node* curr = ad->head;
    for (size_t i = 0; i < ad->length && curr; i++) {
        correlation += (double)curr->value * curr->value;
        curr = curr->next;
    }

    return correlation;
}

// Identify matches of ad within target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad || target->length < ad->length) {
        return strdup("");
    }

    size_t target_len = target->length;
    size_t ad_len = ad->length;

    double Rxx0 = autocorrelation(ad);
    double threshold = 0.95 * Rxx0;

    char* result = malloc(1024);
    if (!result) {
        return NULL;
    }
    result[0] = '\0';

    int match_found = 0;

    for (size_t start = 0; start <= target_len - ad_len; start++) {
        double Rxy = cross_correlation(target, ad, start);

        if (Rxy >= threshold) {
            if (match_found) {
                strcat(result, "\n");
            }
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "%zu,%zu", start, start + ad_len - 1);
            strcat(result, buffer);
            match_found = 1;
            start += ad_len - 1; // Skip ahead to avoid overlapping matches
        }
    }

    return result;
}



// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
               struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0) return;

    // Step 1: Extract [srcpos, srcpos + len) from src_track
    sample_node* src_curr = src_track->head;
    for (size_t i = 0; i < srcpos && src_curr; i++) {
        src_curr = src_curr->next;
    }

    // Copy nodes to a temporary linked list
    sample_node* copy_head = NULL;
    sample_node* copy_tail = NULL;
    for (size_t i = 0; i < len && src_curr; i++) {
        sample_node* new_node = malloc(sizeof(sample_node));
        new_node->value = src_curr->value;
        new_node->next = NULL;

        if (!copy_head) copy_head = new_node;
        else copy_tail->next = new_node;

        copy_tail = new_node;
        src_curr = src_curr->next;
    }

    // Step 2: Splice into dest_track at destpos
    sample_node* dest_curr = dest_track->head;
    sample_node* prev = NULL;

    for (size_t i = 0; i < destpos && dest_curr; i++) {
        prev = dest_curr;
        dest_curr = dest_curr->next;
    }

    if (prev) {
        prev->next = copy_head;
    } else {
        dest_track->head = copy_head;
    }

    if (copy_tail) {
        copy_tail->next = dest_curr;
    }

    dest_track->length += len;
}



// int main() {
//     // Initialize sound_seg objects
//     struct sound_seg* target_track = tr_init();
//     struct sound_seg* bee_track = tr_init();

//     // Load the WAV files
//     size_t target_sample_count = get_wav_sample_count("target.wav"); // Should be ~8000
//     size_t bee_sample_count = get_wav_sample_count("bee.wav");      // Should be ~40000

//     int16_t* target_data = malloc(target_sample_count * sizeof(int16_t));
//     int16_t* bee_data = malloc(bee_sample_count * sizeof(int16_t));
//     if (!target_data || !bee_data) {
//         printf("Memory allocation failed\n");
//         return 1;
//     }

//     wav_load("target.wav", target_data);
//     wav_load("bee.wav", bee_data);

//     // Write data into sound_seg structures
//     tr_write(target_track, target_data, 0, target_sample_count);
//     tr_write(bee_track, bee_data, 0, bee_sample_count);

//     // Insert 0.5 seconds (4000 samples) of bee.wav into target.wav at 0.5 seconds (4000 samples)
//     size_t insert_pos = 4000;      // Halfway into target.wav (0.5s)
//     size_t src_pos = 0;            // Start of bee.wav
//     size_t len = 4000;             // 0.5 seconds of bee.wav
//     if (insert_pos > target_sample_count) {
//         insert_pos = target_sample_count; // Cap at end of target.wav if too large
//     }
//     if (src_pos + len > bee_sample_count) {
//         len = bee_sample_count - src_pos; // Cap len to bee.wav’s length
//     }

//     printf("Inserting %zu samples from bee.wav (pos %zu) into target.wav at pos %zu\n", 
//            len, src_pos, insert_pos);
//     tr_insert(bee_track, target_track, insert_pos, src_pos, len);

//     // Read the modified target_track
//     int16_t* result_data = malloc(target_track->length * sizeof(int16_t));
//     if (!result_data) {
//         printf("Memory allocation failed\n");
//         return 1;
//     }
//     tr_read(target_track, result_data, 0, target_track->length);

//     // Save the result
//     wav_save("output.wav", result_data, target_track->length);
//     printf("Result saved to output.wav (length: %zu samples, %.2f seconds)\n", 
//            target_track->length, (float)target_track->length / 8000.0);

//     // Clean up
//     free(target_data);
//     free(bee_data);
//     free(result_data);
//     tr_destroy(target_track);
//     tr_destroy(bee_track);

//     return 0;
// }
