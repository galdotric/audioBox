#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
// #include "sound_seg.h"
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
    bool is_parent;
    bool is_child;
};

int min(int a, int b) {
    return (a < b) ? a : b;
}

int max(int a, int b) {
    return (a > b) ? a : b;
}

void print_track_data(struct sound_seg* track,char* name){
    printf("%s: ",name);
    while(track){
        for(int i=0;i<track->length;i++)
            printf("%d ",*(track->samples+i));
        track = track->next;
    }printf("\n");
}



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
    seg->is_parent = false;
    seg->is_child = false;
    return seg;
}

void tr_destroy(struct sound_seg* obj) {
    struct sound_seg* curr = obj;
    while (curr != NULL) {
        struct sound_seg* next = curr->next;
        // ASM 0.3: Called at program end, so free regardless of parent status
        // If not ASM 0.3, we’d check curr->is_parent && curr->is_child here
        if (!curr->is_child) {  // Only free samples if not a child (not shared)
            free(curr->samples);
        }
        free(curr);
        curr = next;
    }
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


// Read samples from the track across segments
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track) exit(1);
    if (pos + len > tr_length(track)) {  // Use total length
        printf("Read failed: Position %zu + length %zu exceeds track length %zu\n", pos, len, tr_length(track));
        exit(1);
    }

    struct sound_seg* curr = track;
    size_t current_pos = 0;
    size_t dest_offset = 0;

    // Traverse to the starting position
    while (curr != NULL && current_pos + curr->length <= pos) {
        current_pos += curr->length;
        curr = curr->next;
    }

    // Read across segments
    while (curr != NULL && dest_offset < len) {
        size_t seg_start = pos - current_pos;  // Offset within current segment
        size_t seg_len = min(curr->length - seg_start, len - dest_offset);  // Length to copy from this segment
        memcpy(dest + dest_offset, curr->samples + seg_start, seg_len * sizeof(int16_t));
        dest_offset += seg_len;
        current_pos += curr->length;
        curr = curr->next;
    }
}

// Delete a range of samples from the track, respecting REQ 3.2
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track) return false;
    size_t total_len = tr_length(track);
    if (pos + len > total_len) return false;
    if (len == 0) return true;

    // Check if any segment in the range is a parent with children
    struct sound_seg* curr = track;
    size_t current_pos = 0;
    while (curr != NULL && current_pos < pos + len) {
        if (current_pos + curr->length > pos) {  // Segment overlaps with range
            if (curr->is_parent) {  // Assuming is_parent implies it has children
                printf("Deletion failed: Range contains a parent segment with children\n");
                return false;
            }
        }
        current_pos += curr->length;
        curr = curr->next;
    }

    // Find the starting segment
    curr = track;
    struct sound_seg* prev = NULL;
    current_pos = 0;
    while (curr != NULL && current_pos + curr->length <= pos) {
        current_pos += curr->length;
        prev = curr;
        curr = curr->next;
    }

    // Handle deletion across segments
    size_t remaining_len = len;
    size_t start_offset = pos - current_pos;

    while (curr != NULL && remaining_len > 0) {
        if (start_offset > 0 || remaining_len < curr->length) {
            // Partial segment deletion
            size_t delete_len = min(remaining_len, curr->length - start_offset);
            memmove(curr->samples + start_offset, curr->samples + start_offset + delete_len,
                    (curr->length - start_offset - delete_len) * sizeof(int16_t));
            curr->length -= delete_len;
            remaining_len -= delete_len;
            start_offset = 0;  // Subsequent segments start at 0
        } else {
            // Full segment deletion
            struct sound_seg* next = curr->next;
            if (prev) {
                prev->next = next;
            } else {
                *track = *next;  // Update head if deleting first segment
                free(next->samples);  // Assuming next isn’t a child
                free(next);
            }
            free(curr->samples);  // Safe if not a parent (checked above)
            free(curr);
            curr = next;
            remaining_len -= curr->length;
        }
        if (curr) {
            current_pos += curr->length;
            prev = curr;
            curr = curr->next;
        }
    }

    return true;
}



void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    printf("Track writing\n");
    if(!track || !src) exit(1);
    int total_track_length = tr_length(track);
    print_track_data(track,"track");
    // int16_t modify_data[] = {42};
    // tr_write(s0, modify_data, 6, 1);
    if(pos+len > total_track_length){
        // need to increse the track size
        // get to the last segment
        struct sound_seg* temp = track;
        int temp_size = 0;
        while(temp->next){
            temp_size += temp->length;
            temp = temp->next;
        }
        // here temp is the last segment which need to be overwritten so nicrease its size 
        int16_t* new_track = realloc(temp->samples,(temp->length+pos+len-total_track_length)*sizeof(int16_t));
        if(!new_track) exit(1);
        temp->samples = new_track;
        memset(temp->samples + temp->length, 0, (len+pos - total_track_length) * sizeof(int16_t));
        temp->length = temp->length+pos+len-total_track_length;
        print_track_data(track,"after extention");
    }
    // now need to overwrite each segment in the linked list
    struct sound_seg* temp = track;
    int starting_index_temp = 0;
    int starting_index_src = 0;
    int len_to_be_copied = 0;
    int total_len_remaining = len;
    // first go to the segment where the index is
    int current_len = 0;
    while(current_len+temp->length <= pos){
        current_len += temp->length;
        temp = temp->next;
    }
    print_track_data(temp,"temp");
    starting_index_temp = pos - current_len;
    starting_index_src = 0;
    while(temp){
        // find the stating position in temp
        // find the len to be copied
        // find the stating of src (here src is continuous)
        // memcpy(temp->samples+starting index,src+strting index of src,len to be copied)
        len_to_be_copied = min(temp->length - starting_index_temp,total_len_remaining) ; 
        memcpy(temp->samples+starting_index_temp,src+starting_index_src,len_to_be_copied*sizeof(int16_t));
        total_len_remaining -= len_to_be_copied;
        starting_index_temp = 0;
        starting_index_src += len_to_be_copied;
        temp = temp->next;
    }
    print_track_data(track,"after writing");
    // memcpy(track->samples+pos,src,len*sizeof(int16_t));
    
    return;
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





// // Insertion conditions
// // 1. src_track and dest_track must not be NULL.
// // 2. srcpos + len must not exceed the length of src_track.
// // 3. destpos must not exceed the length of dest_track.
// int input_invalid(struct sound_seg* src_track, struct sound_seg* dest_track, 
//                             size_t destpos, size_t srcpos, size_t len) {
//     if (!src_track || !dest_track || srcpos +len > src_track->length || destpos > tr_length(dest_track)) {
//         return 1;
//     }
//     return 0;
// }

// Helper function to get the segment containing a position and check its parent/child status
static int is_segment_shared(struct sound_seg* track, size_t pos, int* is_parent, int* is_child) {
    struct sound_seg* curr = track;
    size_t current_pos = 0;

    while (curr != NULL && current_pos <= pos) {
        if (current_pos + curr->length > pos) {
            *is_parent = curr->is_parent;
            *is_child = curr->is_child;
            return 1;  // Found the segment
        }
        current_pos += curr->length;
        curr = curr->next;
    }
    return 0;  // Position not found
}

// Helper function to get samples adjacent to destpos
static void get_adjacent_samples(struct sound_seg* track, size_t destpos, 
                                int16_t** before, size_t* before_len, 
                                int16_t** after, size_t* after_len) {
    *before = NULL;
    *before_len = 0;
    *after = NULL;
    *after_len = 0;

    struct sound_seg* curr = track;
    size_t current_pos = 0;

    while (curr != NULL && current_pos < destpos) {
        current_pos += curr->length;
        if (current_pos >= destpos) {
            size_t offset = destpos - (current_pos - curr->length);
            if (offset > 0) {
                *before = curr->samples + offset - 1;  // Sample before destpos
                *before_len = 1;
            }
            if (offset < curr->length) {
                *after = curr->samples + offset;       // Sample after destpos
                *after_len = 1;
            }
        }
        curr = curr->next;
    }
}

// Check: Parent samples and adjacent destpos samples shall not be parent or child
int tr_check_parent_child(struct sound_seg* src_track, struct sound_seg* dest_track, 
                          size_t destpos, size_t srcpos, size_t len) {
    // Basic input validation
    if (!src_track || !dest_track || srcpos + len > src_track->length || destpos > tr_length(dest_track)) {
        return 0;  // Not allowed due to invalid input
    }

    // Check parent samples (src_track from srcpos to srcpos + len)
    struct sound_seg* curr = src_track;
    size_t current_pos = 0;
    while (curr != NULL && current_pos < srcpos + len) {
        if (current_pos + curr->length > srcpos) {
            // Segment overlaps with the range to be inserted
            if (curr->is_parent || curr->is_child) {
                return 0;  // Parent samples are already shared
            }
        }
        current_pos += curr->length;
        curr = curr->next;
    }

    // Check adjacent samples
    int16_t* adj_before = NULL;
    int16_t* adj_after = NULL;
    size_t before_len = 0;
    size_t after_len = 0;
    get_adjacent_samples(dest_track, destpos, &adj_before, &before_len, &adj_after, &after_len);

    if (adj_before != NULL) {
        int is_parent = 0, is_child = 0;
        if (is_segment_shared(dest_track, destpos - 1, &is_parent, &is_child) && (is_parent || is_child)) {
            return 0;  // Sample before destpos is shared
        }
    }

    if (adj_after != NULL) {
        int is_parent = 0, is_child = 0;
        if (is_segment_shared(dest_track, destpos, &is_parent, &is_child) && (is_parent || is_child)) {
            return 0;  // Sample after destpos is shared
        }
    }

    return 1;  // Insertion allowed
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    // Check if the insertion conditions are met
    if (!src_track || !dest_track) {
        printf("Insertion failed: Null track pointer\n");
        exit(1);
    }
    if (srcpos + len > tr_length(src_track)) {  // Use total length
        printf("Insertion failed: Source position out of bounds\n");
        printf("Source position: %zu, Length: %zu, Total track length: %zu\n", srcpos, len, tr_length(src_track));
        exit(1);
    }
    if (destpos > tr_length(dest_track)) {
        printf("Insertion failed: Destination position out of bounds\n");
        exit(1);
    }

    // Check parent-child conditions
    if (!tr_check_parent_child(src_track, dest_track, destpos, srcpos, len)) {
        printf("Insertion failed: Parent samples or adjacent samples are already parent or child\n");
        return;
    }


    

    if (destpos == 0) {
        // Insert at the start of the dest track
        struct sound_seg* new_seg = malloc(sizeof(struct sound_seg));
        if (!new_seg) exit(1);
        
        // Share the same data portion from src_track (no copying)
        new_seg->samples = src_track->samples + srcpos;
        new_seg->length = len;
        new_seg->next = dest_track;
        
        // Since we can't change the head pointer, modify the existing head
        if (dest_track->length == 0 && dest_track->next == NULL) {
            // Empty track case
            dest_track->samples = new_seg->samples;
            dest_track->length = new_seg->length;
            dest_track->next = new_seg->next;
            free(new_seg);
        } else {
            // Swap contents while maintaining shared data
            struct sound_seg temp = *dest_track;  // Store original head contents
            dest_track->samples = new_seg->samples;
            dest_track->length = new_seg->length;
            dest_track->next = new_seg;
            *new_seg = temp;  // Move original contents to new_seg
        }
        return;
    }

    // Traverse to the insertion point in the destination track
    struct sound_seg* dest_prev = NULL;
    struct sound_seg* dest_curr = dest_track;
    size_t current_pos = 0;

    while (dest_curr != NULL && (current_pos + dest_curr->length) <= destpos) {
        current_pos += dest_curr->length;
        dest_prev = dest_curr;
        dest_curr = dest_curr->next;
    }

    struct sound_seg* segment_2 = malloc(sizeof(struct sound_seg));
    if (!segment_2) exit(1);
    
    segment_2->samples = NULL;
    segment_2->next = NULL;
    segment_2->length = 0;

    size_t split_offset = destpos - current_pos;
    if (split_offset != 0) {
        // Split the current segment, maintaining shared data
        segment_2->samples = dest_curr->samples + split_offset;
        segment_2->length = dest_curr->length - split_offset;
        segment_2->next = dest_curr->next;
        dest_curr->length = split_offset;
        dest_curr->next = segment_2;
    } else if (split_offset == 0) {
        dest_prev->next = segment_2;
        dest_curr = dest_prev;
    }

    // Create new segment with shared data from src_track
    struct sound_seg* new_seg = malloc(sizeof(struct sound_seg));
    if (!new_seg) exit(1);

    // Share the same data portion (no copying)
    new_seg->samples = src_track->samples + srcpos;
    new_seg->length = len;
    new_seg->next = segment_2;

    dest_curr->next = new_seg;

    //set thef flags
    src_track->is_parent = true;
    dest_track->is_child = true;
    
    return;
}



// void test_sound_seg_operations(void) {
   
//     // Test 7: Self-insertion and shared backing store
//     struct sound_seg* s0 = tr_init();
//     int16_t initial_data[] = {8, 19, 0, -12, -15, 7};
//     tr_write(s0, initial_data, 0, 6);
//     print_track_data(s0,"s0 initial:");
//     tr_insert(s0, s0, 0, 3, 1);
//     print_track_data(s0,"s0 final:");
//     struct sound_seg* s1 = tr_init();
//     int16_t initial_data1[] = {1,2,3,4,5,6,7};
//     //test parent child restriction
//     tr_write(s1, initial_data1, 0, 7);
//     printf("s1 length: %zu\n",tr_length(s1));
//     printf("s0 length: %zu\n",tr_length(s0));
//     //verify the length
//     print_track_data(s1,"s1");
//     print_track_data(s0,"s0");

    
    
    


// }
// void test_random_operations() {
//     struct sound_seg* s0 = tr_init();
//     int16_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
//     tr_write(s0, data, 0, 10);
//     print_track_data(s0, "s0 initial");

//     // Random insertion
//     tr_insert(s0, s0, rand() % 5, rand() % 5, 3);
//     print_track_data(s0, "s0 after self-insert");

//     // Random read
//     int16_t buffer[5];
//     size_t read_pos = rand() % tr_length(s0);
//     size_t read_len = min(5, tr_length(s0) - read_pos);
//     tr_read(s0, buffer, read_pos, read_len);
//     printf("Read from %zu, len %zu: ", read_pos, read_len);
//     for (size_t i = 0; i < read_len; i++) printf("%d ", buffer[i]);
//     printf("\n");

//     // Random delete
//     size_t del_pos = rand() % tr_length(s0);
//     size_t del_len = min(3, tr_length(s0) - del_pos);
//     if (!tr_delete_range(s0, del_pos, del_len)) {
//         printf("Delete failed at pos %zu, len %zu\n", del_pos, del_len);
//     } else {
//         print_track_data(s0, "s0 after delete");
//     }

//     // Test parent protection
//     struct sound_seg* s1 = tr_init();
//     int16_t data1[] = {11, 12, 13};
//     tr_write(s1, data1, 0, 3);
//     tr_insert(s0, s1, 0, 0, 2);  // s0 becomes parent
//     print_track_data(s1, "s1 with s0 data");
//     if (!tr_delete_range(s0, 0, 2)) {
//         printf("Delete failed: s0 is a parent\n");
//     }

//     //tr_destroy(s0);
//     tr_destroy(s1);
// }

// int main() {
//     test_random_operations();
//     return 0;
// }