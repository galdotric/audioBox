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
size_t get_wav_sample_count(const char* filename){
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
    if (!seg) {
        perror("Failed to allocate sound_seg");
        exit(1);
    }
    seg->samples = NULL;
    seg->length = 0;
    seg->next = NULL;
    return seg;
}

// Load a WAV file into a sound_seg
struct sound_seg* load_wav_to_seg(const char* filename) {
    size_t sample_count = get_wav_sample_count(filename);
    int16_t* buffer = malloc(sample_count * sizeof(int16_t));
    if (!buffer) {
        perror("Failed to allocate buffer for WAV samples");
        exit(1);
    }
    wav_load(filename, buffer);
    struct sound_seg* seg = tr_init();
    seg->samples = buffer;
    seg->length = sample_count;
    seg->next = NULL;
    return seg;
}

size_t tr_length(struct sound_seg* track) {
    size_t total_length = 0;
    struct sound_seg* curr = track;
    while (curr != NULL) {
        total_length += curr->length;
        curr = curr->next;
    }
    return total_length;
}

// Flatten a track into a single buffer for saving
int16_t* flatten_track(struct sound_seg* track, size_t* total_len) {
    size_t len = tr_length(track);
    int16_t* buffer = malloc(len * sizeof(int16_t));
    if (!buffer) {
        perror("Failed to allocate buffer for flattening");
        exit(1);
    }
    size_t offset = 0;
    struct sound_seg* current = track;
    while (current) {
        memcpy(buffer + offset, current->samples, current->length * sizeof(int16_t));
        offset += current->length;
        current = current->next;
    }
    *total_len = len;
    return buffer;
}

// Destroy a sound_seg and all linked segments
void tr_destroy(struct sound_seg* obj) {
    while (obj) {
        struct sound_seg* next = obj->next;
        free(obj->samples);
        free(obj);
        obj = next;
    }
}




void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest) exit(1);

    // Check if the requested range exceeds the total track length
    size_t total_length = tr_length(track);
    if (pos + len > total_length) exit(1);

    size_t current_pos = 0;
    struct sound_seg* current_seg = track;
    
    // Traverse to the segment containing pos
    while (current_seg != NULL && current_pos + current_seg->length <= pos) {
        current_pos += current_seg->length;
        current_seg = current_seg->next;
    }

    size_t offset = pos - current_pos;  // Offset within the current segment
    size_t remaining = len;
    int16_t* dest_ptr = dest;

    // Read samples across segments
    while (remaining > 0 && current_seg != NULL) {
        size_t to_copy = current_seg->length - offset;
        if (to_copy > remaining) to_copy = remaining;
        memcpy(dest_ptr, current_seg->samples + offset, to_copy * sizeof(int16_t));
        dest_ptr += to_copy;
        remaining -= to_copy;
        offset = 0;  // Start from the beginning of the next segment
        current_seg = current_seg->next;
    }

    // This should not happen due to the initial length check, but included for robustness
    if (remaining > 0) exit(1);
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src) exit(1);

    size_t current_pos = 0;
    struct sound_seg* current_seg = track;
    struct sound_seg* prev_seg = NULL;

    // Traverse to the segment containing pos
    while (current_seg != NULL && current_pos + current_seg->length <= pos) {
        current_pos += current_seg->length;
        prev_seg = current_seg;
        current_seg = current_seg->next;
    }

    size_t offset = pos - current_pos;  // Offset within the current segment
    size_t remaining = len;
    int16_t* src_ptr = src;

    while (remaining > 0) {
        if (current_seg == NULL) {
            // Add a new segment at the end
            struct sound_seg* new_seg = malloc(sizeof(struct sound_seg));
            if (!new_seg) exit(1);
            new_seg->length = remaining;  // Size to fit remaining samples
            new_seg->samples = malloc(new_seg->length * sizeof(int16_t));
            if (!new_seg->samples) exit(1);
            new_seg->next = NULL;
            if (prev_seg) prev_seg->next = new_seg;
            else track = new_seg;  // Update head if track was empty
            current_seg = new_seg;
            offset = 0;
        }

        size_t to_write = current_seg->length - offset;
        if (to_write > remaining) to_write = remaining;

        // If writing beyond the segment's current length, extend it
        if (offset + to_write > current_seg->length) {
            size_t new_length = offset + to_write;
            int16_t* new_samples = realloc(current_seg->samples, new_length * sizeof(int16_t));
            if (!new_samples) exit(1);
            // Zero out any new space between old length and offset
            if (current_seg->length < offset) {
                memset(new_samples + current_seg->length, 0, 
                       (offset - current_seg->length) * sizeof(int16_t));
            }
            current_seg->samples = new_samples;
            current_seg->length = new_length;
        }

        memcpy(current_seg->samples + offset, src_ptr, to_write * sizeof(int16_t));
        src_ptr += to_write;
        remaining -= to_write;
        offset = 0;  // Start from the beginning of the next segment
        prev_seg = current_seg;
        current_seg = current_seg->next;
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0) return len == 0;  // Success if len is 0, fail if track is NULL
    size_t total_length = tr_length(track);
    if (pos + len > total_length) return false;

    size_t start_pos = pos;
    size_t end_pos = pos + len - 1;
    size_t current_pos = 0;
    struct sound_seg* prev = NULL;
    struct sound_seg* current = track;

    // Find the start segment (S1) and offset
    while (current != NULL && current_pos + current->length <= start_pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }
    struct sound_seg* s1 = current;
    size_t o1 = start_pos - current_pos;

    // If start_pos is beyond all segments, should be caught by total_length check
    if (!s1) return false;

    // Find the end segment (S2) and offset
    while (current != NULL && current_pos + current->length <= end_pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }
    struct sound_seg* s2 = current;
    size_t o2 = end_pos - current_pos;

    if (s1 == s2) {
        // Deletion within a single segment
        size_t delete_count = o2 - o1 + 1;
        if (o1 == 0 && o2 == s1->length - 1) {
            // Remove entire segment
            if (prev) prev->next = s1->next;
            else track = s1->next;  // Update head
            free(s1->samples);
            free(s1);
        } else {
            // Shift samples within the segment
            memmove(s1->samples + o1, s1->samples + o2 + 1, 
                    (s1->length - o2 - 1) * sizeof(int16_t));
            s1->length -= delete_count;
        }
    } else {
        // Deletion across multiple segments
        struct sound_seg* to_free = s1->next;

        // Adjust or remove s1
        if (o1 > 0) {
            s1->length = o1;
            s1->next = s2;
        } else {
            if (prev) prev->next = s2;
            else track = s2;
            free(s1->samples);
            free(s1);
        }

        // Adjust or remove s2
        if (s2 && o2 < s2->length - 1) {
            size_t new_length = s2->length - (o2 + 1);
            memmove(s2->samples, s2->samples + o2 + 1, new_length * sizeof(int16_t));
            s2->length = new_length;
        } else if (s2) {
            struct sound_seg* s2_next = s2->next;
            if (o1 > 0) s1->next = s2_next;
            else if (prev) prev->next = s2_next;
            else track = s2_next;
            free(s2->samples);
            free(s2);
            s2 = NULL;
        }

        // Free intermediate segments
        while (to_free != s2) {
            struct sound_seg* next = to_free->next;
            free(to_free->samples);
            free(to_free);
            to_free = next;
        }
    }
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
//     // --- Ad Identification Test ---
//     printf("Testing ad identification with ad.wav and target.wav\n");
//     struct sound_seg* ad_seg = load_wav_to_seg("ad.wav");
//     struct sound_seg* target_seg = load_wav_to_seg("target.wav");
//     printf("ad.wav sample count: %zu\n", ad_seg->length);
//     printf("target.wav sample count: %zu\n", target_seg->length);

//     char* ad_positions = tr_identify(target_seg, ad_seg);
//     printf("Ad occurrences:\n%s\n", ad_positions);
//     free(ad_positions); // tr_identify allocates the result string

//     // --- Insertion Test ---
//     printf("\nTesting insertion with target.wav and bee.wav\n");
//     struct sound_seg* bee_seg = load_wav_to_seg("bee.wav");
//     printf("bee.wav sample count: %zu\n", bee_seg->length);

//     // Insert 1 second (8000 samples) of bee.wav into target.wav at position 4000
//     size_t destpos = 4000; // Middle of target.wav (assuming 8000 samples total)
//     size_t srcpos = 0;     // Start of bee.wav
//     size_t len = 8000;     // 1 second at 8000 Hz
//     if (srcpos + len > bee_seg->length) {
//         printf("Warning: Requested length exceeds bee.wav; adjusting len\n");
//         len = bee_seg->length - srcpos;
//     }
//     tr_insert(bee_seg, target_seg, destpos, srcpos, len);
//     printf("Inserted %zu samples from bee.wav into target.wav at position %zu\n", len, destpos);

//     // Save the modified target_seg
//     size_t total_len;
//     int16_t* flattened = flatten_track(target_seg, &total_len);
//     wav_save("inserted.wav", flattened, total_len);
//     printf("Saved modified track as inserted.wav with %zu samples\n", total_len);
//     free(flattened);

//     // --- Additional Tests (Read, Write, Delete) ---
//     // Reload target.wav for further modifications
//     struct sound_seg* target_seg2 = load_wav_to_seg("target.wav");

//     // Test tr_read: Read 2000 samples starting at position 1000
//     printf("\nTesting tr_read on target.wav\n");
//     int16_t read_buffer[2000];
//     tr_read(target_seg2, read_buffer, 1000, 2000);
//     printf("Read 2000 samples from position 1000\n");

//     size_t samples_read = 2000; // Define and initialize samples_read

//     // Test tr_write: Write the read samples back at position 0
//     printf("Testing tr_write on target.wav\n");
//     tr_write(target_seg2, read_buffer, 0, samples_read);
//     printf("Wrote %zu samples to position 0\n", samples_read);

//     // Test tr_delete_range: Delete 2000 samples starting at position 2000
//     printf("Testing tr_delete_range on target.wav\n");
//     if (tr_delete_range(target_seg2, 2000, 5000)) {
//         size_t total_len2;
//         int16_t* flattened2 = flatten_track(target_seg2, &total_len2);
//         wav_save("deleted.wav", flattened2, total_len2);
//         printf("Deleted 2000 samples from position 2000; saved as deleted.wav with %zu samples\n", total_len2);
//         free(flattened2);
//     } else {
//         printf("Error: tr_delete_range failed\n");
//     }

//     // --- Cleanup ---
//     tr_destroy(ad_seg);
//     tr_destroy(target_seg);
//     tr_destroy(bee_seg);
//     tr_destroy(target_seg2);

//     printf("\nAll tests completed.\n");
//     return 0;
// }
