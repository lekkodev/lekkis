
#ifndef counter_h
#define counter_h

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NUM_COUNTERS 48


/*
    This is probably bad -- okay, so for the good, it fits within a cache line on everything reasonable for it to run on.
 
    There are probably 50 better versions online that can be swapped in.
 
 */
typedef struct {
    uint8_t key[24];        // 24-byte code (key)
    uint32_t timestamp;     // 2-byte timestamp - do we want to offset that?
    uint16_t counters[NUM_COUNTERS]; // 48 counters (2 bytes each = 96 bytes)
    uint8_t occupied;       // 1 byte flag to indicate if the bucket is occupied - do we need this or should we just zero init?
    uint8_t padding[3];     // Padding to ensure the struct is 128 bytes
} Bucket;

typedef struct {
    Bucket* buckets;
    size_t num_buckets;
} HashTable;

// TODO replace with murmur3 or something not trash
static size_t hash_function(const uint8_t* key, size_t key_size, size_t num_buckets) {
    size_t hash = 5381;
    for (size_t i = 0; i < key_size; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash % num_buckets;
}

HashTable* hash_table_init(size_t num_buckets) {
    HashTable* table = (HashTable*)malloc(sizeof(HashTable));
    table->num_buckets = num_buckets;
    // allign the alloc to a cache line
    table->buckets = (Bucket*)aligned_alloc(128, num_buckets * sizeof(Bucket));
    return table;
}

Bucket* hash_table_find_or_create(HashTable* table, const uint8_t* key, uint16_t timestamp) {
    size_t index = hash_function(key, 24, table->num_buckets);
    size_t original_index = index;

    while (1) {
        Bucket* bucket = &table->buckets[index];

        if (bucket->occupied) {
            if (memcmp(bucket->key, key, 24) == 0) {
                return bucket;
            }
        } else {
            memcpy(bucket->key, key, 24);
            bucket->timestamp = timestamp;
            memset(bucket->counters, 0, sizeof(bucket->counters));
            bucket->occupied = 1;
            return bucket;
        }
        index = (index + 1) % table->num_buckets;
        if (index == original_index) {
            fprintf(stderr, "Hash table is full!\n");
            exit(EXIT_FAILURE);
        }
    }
}

void hash_table_update(HashTable* table, const uint8_t* key, uint16_t event_time) {
    Bucket* bucket = hash_table_find_or_create(table, key, event_time);
    uint16_t offset = (event_time - bucket->timestamp) % NUM_COUNTERS;
    bucket->counters[offset]++;
}

void hash_table_print(HashTable* table) {
    for (size_t i = 0; i < table->num_buckets; i++) {
        Bucket* bucket = &table->buckets[i];
        if (bucket->occupied) {
            printf("Bucket %zu: Key = ", i);
            for (int j = 0; j < 24; j++) {
                printf("%02x", bucket->key[j]);
            }
            printf(", Timestamp = %u, Counters = [", bucket->timestamp);
            for (int j = 0; j < NUM_COUNTERS; j++) {
                printf("%u", bucket->counters[j]);
                if (j < NUM_COUNTERS - 1) {
                    printf(", ");
                }
            }
            printf("]\n");
        }
    }
}

void hash_table_free(HashTable* table) {
    free(table->buckets);
    free(table);
}

#endif /* counter_h */
