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

int64_t cross_correlation(struct sound_seg* target, struct sound_seg* ad, size_t k){
    (target->samples == NULL || ad->samples == NULL) ? exit(1) : 0;
    (k +ad->length > target->length) ? exit(1) : 0; //check if ad fits in target

    int64_t R = 0;
    size_t N = ad->length - 1;
    for(size_t n = 0; n < N; n++){
        int32_t t_sample = target->samples[k + n];
        int32_t ad_sample = ad->samples[n];
        R += t_sample * ad_sample;
    }
    return R;
}

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {

    if(ad->length == 0 || target->length < ad->length){
        char* result = malloc(1);
        if(result) result[0] = '\0';
        return result;
    }
    int64_t reference = cross_correlation(ad,ad,0);
    double threshold = 0.95 * (double)reference;

    size_t* starts = malloc(10 * sizeof(size_t));
    size_t capacity = 10, count = 0;
    size_t k = 0;
    while(k + ad->length <= target->length){
        int64_t R = cross_correlation(target, ad, k);
        if((double)R >= threshold){
            if(count == capacity){
                capacity *= 2;
                starts = realloc(starts, capacity * sizeof(size_t));
            }
            starts[count++] = k;
            k += ad->length;
        }else{
            k++;
        }
    }

    if (count == 0) {
        free(starts);
        char* result = malloc(1);
        result[0] = '\0';
        return result;
    }

    size_t total_len = 0;
    for (size_t i = 0; i < count; i++) {
        total_len += snprintf(NULL, 0, "%zu,%zu", starts[i], starts[i] + ad->length - 1);
        if (i < count - 1) total_len += 1;
    }

    char* result = malloc(total_len + 1);
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        pos += snprintf(result + pos, total_len + 1 - pos, "%zu,%zu", starts[i], starts[i] + ad->length - 1);
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
//     size_t size = get_wav_sample_count("test_target.wav");
//     printf("Size of test_target.wav: %zu\n",size);

//     // Test 1: tr_init and tr_length
//     struct sound_seg* track = tr_init();
//     printf("Test 1: tr_init - Length of empty track: %zu (expected 0)\n", tr_length(track));

//     // Test 2: tr_write
//     int16_t data[] = {1, 2, 3, 4, 5};
//     tr_write(track, data, 0, 5);
//     printf("Test 2: tr_write - Length after writing [1,2,3,4,5]: %zu (expected 5)\n", tr_length(track));

//     // Test 3: tr_read
//     int16_t read_buffer[5];
//     tr_read(track, read_buffer, 0, 5);
//     printf("Test 3: tr_read - Data: ");
//     for (int i = 0; i < 5; i++) printf("%d ", read_buffer[i]);
//     printf("(expected 1 2 3 4 5)\n");

//     // Test 4: tr_delete_range
//     tr_delete_range(track, 1, 2); // Remove 2, 3
//     printf("Test 4: tr_delete_range - Length after deleting 2 samples at pos 1: %zu (expected 3)\n", tr_length(track));
//     tr_read(track, read_buffer, 0, 3);
//     printf("        Data: ");
//     for (int i = 0; i < 3; i++) printf("%d ", read_buffer[i]);
//     printf("(expected 1 4 5)\n");

//     // Test 5: wav_load (assuming test_target.wav exists)
//     int16_t wav_buffer[10000];
//     wav_load("test_target.wav", wav_buffer);
//     printf("Test 5: wav_load - First 5 samples from test_target.wav: ");
//     for (int i = 0; i < 5 && i < 8000; i++) printf("%d ", wav_buffer[i]);
//     printf("(check against file)\n");

//     // Test 6: wav_save
//     wav_save("test_output.wav", data, 5);
//     printf("Test 6: wav_save - Saved [1,2,3,4,5] to test_output.wav (check file manually)\n");

//     // Test 7: cross_correlation
//     struct sound_seg* ad = tr_init();
//     int16_t ad_data[] = {2, 3, 4};
//     tr_write(ad, ad_data, 0, 3);
//     int64_t R = cross_correlation(track, ad, 0); // Check [1,4,5] vs [2,3,4]
//     printf("Test 7: cross_correlation - Correlation at pos 0: %ld (expected some value)\n", R);

//     // Test 8: tr_identify
//     struct sound_seg* target = tr_init();
//     int16_t target_data[] = {1, 2, 3, 4, 5, 2, 3, 4, 6, 7};
//     tr_write(target, target_data, 0, 10);
//     char* result = tr_identify(target, ad);
//     printf("Test 8: tr_identify - Ad locations: '%s' (expected :'1,3 and  5,7')\n", result);

//     // Test 9: tr_destroy
//     tr_destroy(track);
//     tr_destroy(ad);
//     tr_destroy(target);
//     free(result);
//     printf("Test 9: tr_destroy - Memory freed (no segfault means success)\n");

//     return 0;
// }

