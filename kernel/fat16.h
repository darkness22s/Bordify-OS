#ifndef BORDIFY_FAT16_H
#define BORDIFY_FAT16_H

#include "types.h"

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20

typedef struct {
    char name[13];
    char display[32];
    uint8_t attr;
    uint32_t size;
    uint16_t cluster;
    uint32_t slot;
} fat_entry_t;

typedef struct {
    bool mounted;
    uint32_t partition_lba;
    uint32_t reserved_sectors;
    uint32_t volume_sectors;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t cluster_count;
    uint32_t spc;
    uint32_t spc_bytes;
    uint16_t root_entries;
    uint32_t root_sectors;
    uint16_t fat_sectors;
    uint8_t fat_count;
    char label[12];
} fat_volume_t;

bool fat16_mount(void);
bool fat16_mounted(void);
const fat_volume_t *fat16_volume(void);
int fat16_list(uint16_t dir_cluster, fat_entry_t *entries, int max_entries);
bool fat16_find(uint16_t dir_cluster, const char *name, fat_entry_t *entry);
bool fat16_mkdir(uint16_t dir_cluster, const char *name);
bool fat16_create_file(uint16_t dir_cluster, const char *name);
bool fat16_delete(uint16_t dir_cluster, const char *name);
bool fat16_rename(uint16_t dir_cluster, const char *name, const char *new_name);
int fat16_read(uint16_t dir_cluster, const char *name, void *buffer, uint32_t max_size);
int fat16_read_range(uint16_t dir_cluster, const char *name,
                     uint32_t offset, void *buffer, uint32_t max_size);
int fat16_write(uint16_t dir_cluster, const char *name, const void *buffer, uint32_t size);
int fat16_read_path(const char *path, void *buffer, uint32_t max_size);
int fat16_write_path(const char *path, const void *buffer, uint32_t size);
bool fat16_exists_path(const char *path);
uint32_t fat16_free_clusters(void);
void fat16_name_to_83(const char *name, char out[11]);
void fat16_entry_display_name(const fat_entry_t *entry, char *out, size_t out_size);

#endif
