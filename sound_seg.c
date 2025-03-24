#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef struct data_block {
    int16_t* samples;    // Raw audio samples
    size_t length;       // Number of samples in this block
    size_t ref_count;    // Number of segments referencing this block
} data_block_t;

typedef struct segment {
    data_block_t* block;      // Pointer to the shared data block
    size_t offset;            // Offset within the block where this segment starts
    size_t length;            // Number of samples in this segment
    struct segment* next;      // Next segment in the track
    struct segment* parent;   // Pointer to the parent segment (NULL if not a child)
    size_t child_count;       // Number of child segments
} segment_t;

typedef struct sound_seg {
    segment_t* head;          // Head of the segment linked list
    size_t total_length;      // Total number of samples in the track
} sound_seg_t;

// [Previous helper functions like create_data_block, free_data_block, create_segment remain unchanged]

// [WAV handling functions like waveheader, wav_load, get_wav_sample_count, wav_save remain unchanged]

// [Track functions like tr_init, tr_destroy, tr_length, tr_read, tr_write remain unchanged since they already use sound_seg_t]

// Delete range function (unchanged since it uses sound_seg_t)
bool tr_delete_range(sound_seg_t* track, size_t pos, size_t len) {
    if (!track || pos >= track->total_length || len == 0) return true;
    size_t end_pos = pos + len;
    if (end_pos > track->total_length) end_pos = track->total_length;
    len = end_pos - pos;

    segment_t* current = track->head;
    size_t current_pos = 0;
    while (current && current_pos < end_pos) {
        if (current_pos + current->length > pos && current->child_count > 0) {
            return false; // REQ 3.2
        }
        current_pos += current->length;
        current = current->next;
    }
    return true; // Placeholder
}

// Updated cross_correlation to work with sound_seg_t
double cross_correlation(sound_seg_t* target, sound_seg_t* ad, size_t k) {
    double correlation = 0.0;
    if (!target || !ad || !target->head || !ad->head || k + ad->total_length > target->total_length) {
        return 0.0;
    }

    // Iterate through ad's segments
    segment_t* ad_seg = ad->head;
    size_t ad_pos = 0;
    size_t i = 0;

    // Iterate through target's segments starting at position k
    segment_t* target_seg = target->head;
    size_t target_pos = 0;
    while (target_seg && target_pos + target_seg->length <= k) {
        target_pos += target_seg->length;
        target_seg = target_seg->next;
    }

    while (ad_seg && i < ad->total_length) {
        size_t ad_samples = ad_seg->length;
        size_t target_offset = (k + i) - target_pos;
        while (target_seg && target_offset >= target_seg->length) {
            target_pos += target_seg->length;
            target_seg = target_seg->next;
            target_offset = (k + i) - target_pos;
        }
        if (!target_seg) break;

        size_t samples_to_process = ad_samples;
        if (i + samples_to_process > ad->total_length) {
            samples_to_process = ad->total_length - i;
        }
        if (target_offset + samples_to_process > target_seg->length) {
            samples_to_process = target_seg->length - target_offset;
        }

        for (size_t j = 0; j < samples_to_process; j++) {
            correlation += (double)(target_seg->block->samples[target_seg->offset + target_offset + j]) *
                          (double)(ad_seg->block->samples[ad_seg->offset + j]);
        }
        i += samples_to_process;
        ad_pos += ad_seg->length;
        ad_seg = ad_seg->next;
    }
    return correlation;
}

// Updated autocorrelation to work with sound_seg_t
double autocorrelation(sound_seg_t* ad) {
    double correlation = 0.0;
    if (!ad || !ad->head) return 0.0;

    segment_t* seg = ad->head;
    size_t i = 0;
    while (seg && i < ad->total_length) {
        size_t samples_to_process = seg->length;
        if (i + samples_to_process > ad->total_length) {
            samples_to_process = ad->total_length - i;
        }
        for (size_t j = 0; j < samples_to_process; j++) {
            int16_t sample = seg->block->samples[seg->offset + j];
            correlation += (double)sample * (double)sample;
        }
        i += samples_to_process;
        seg = seg->next;
    }
    return correlation;
}

// Updated tr_identify to work with sound_seg_t
char* tr_identify(sound_seg_t* target, sound_seg_t* ad) {
    if (!ad || ad->total_length == 0 || !target || target->total_length < ad->total_length) {
        char* result = malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    double ad_autocorr = autocorrelation(ad);
    double threshold = 0.95 * ad_autocorr;
    size_t* starts = malloc(10 * sizeof(size_t));
    size_t capacity = 10, count = 0;
    size_t k = 0;
    while (k <= target->total_length - ad->total_length) {
        double corr = cross_correlation(target, ad, k);
        if (corr >= threshold) {
            if (count == capacity) {
                capacity *= 2;
                size_t* temp = realloc(starts, capacity * sizeof(size_t));
                if (!temp) exit(1);
                starts = temp;
            }
            starts[count++] = k;
            k += ad->total_length;
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
        size_t end = starts[i] + ad->total_length - 1;
        total_len += snprintf(NULL, 0, "%zu,%zu", starts[i], end);
        if (i < count - 1) total_len += 1;
    }
    char* result = malloc(total_len + 1);
    if (!result) exit(1);
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        size_t end = starts[i] + ad->total_length - 1;
        pos += snprintf(result + pos, total_len + 1 - pos, "%zu,%zu", starts[i], end);
        if (i < count - 1) result[pos++] = '\n';
    }
    result[total_len] = '\0';
    free(starts);
    return result;
}

// Updated tr_insert to work with sound_seg_t
void tr_insert(const sound_seg_t* src_track, sound_seg_t* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0 || srcpos + len > src_track->total_length ||
        destpos > dest_track->total_length) {
        return; // ASM 7.1 ensures srcpos+len in range, ASM 7.2 for destpos
    }

    // Step 1: Extract segments from src_track for [srcpos, srcpos+len)
    segment_t* src_current = src_track->head;
    size_t src_pos = 0;
    segment_t* new_segments_head = NULL;
    segment_t* new_segments_tail = NULL;
    size_t remaining = len;

    while (src_current && remaining > 0) {
        if (src_pos + src_current->length > srcpos) {
            size_t start = (src_pos < srcpos) ? srcpos - src_pos : 0;
            size_t copy_len = src_current->length - start;
            if (copy_len > remaining) copy_len = remaining;

            segment_t* new_seg = create_segment(src_current->block,
                                                src_current->offset + start,
                                                copy_len, src_current);
            if (!new_seg) return; // Memory allocation failed

            if (!new_segments_head) {
                new_segments_head = new_seg;
                new_segments_tail = new_seg;
            } else {
                new_segments_tail->next = new_seg;
                new_segments_tail = new_seg;
            }
            remaining -= copy_len;
        }
        src_pos += src_current->length;
        src_current = src_current->next;
    }

    // Step 2: Insert into dest_track at destpos
    if (destpos == dest_track->total_length) {
        // Append to the end
        segment_t* current = dest_track->head;
        if (!current) {
            dest_track->head = new_segments_head;
        } else {
            while (current->next) current = current->next;
            current->next = new_segments_head;
        }
        dest_track->total_length += len;
    } else {
        // Insert at destpos, splitting if necessary
        segment_t* prev = NULL;
        segment_t* current = dest_track->head;
        size_t current_pos = 0;

        while (current && current_pos + current->length <= destpos) {
            current_pos += current->length;
            prev = current;
            current = current->next;
        }

        if (!current) {
            prev->next = new_segments_head;
            dest_track->total_length += len;
            return;
        }

        if (current_pos < destpos) {
            size_t split_point = destpos - current_pos;
            segment_t* first = create_segment(current->block, current->offset, split_point,
                                             current->parent);
            segment_t* second = create_segment(current->block, current->offset + split_point,
                                              current->length - split_point, current->parent);
            if (!first || !second) {
                free(first);
                free(second);
                return;
            }
            second->next = current->next;
            first->next = new_segments_head;
            new_segments_tail->next = second;

            if (current->parent) current->parent->child_count++;
            if (prev) {
                prev->next = first;
            } else {
                dest_track->head = first;
            }
            free_data_block(current->block);
            free(current);
        } else {
            new_segments_tail->next = current;
            if (prev) {
                prev->next = new_segments_head;
            } else {
                dest_track->head = new_segments_head;
            }
        }
        dest_track->total_length += len;
    }
}