#include "fat16.h"
#include "ata.h"
#include "heap.h"
#include "printf.h"
#include "string.h"
#include "rtc.h"

#define FAT16_EOC 0xFFF8u
#define FAT16_BAD 0xFFF7u
#define FAT16_MAX_DIR_BYTES 65536u
#define FAT16_MAX_CLUSTER_BUFFER 65536u
#define FAT16_MAX_PATH 192

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t size;
} PACKED raw_dir_entry_t;

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_sectors;
    uint32_t total_sectors;
} fat_bpb_t;

static fat_volume_t volume;
static uint16_t *fat_cache;
static bool mounted;
static uint8_t dir_buffer[FAT16_MAX_DIR_BYTES];
static uint8_t cluster_buffer[FAT16_MAX_CLUSTER_BUFFER];
static uint32_t dir_buffer_size;

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t cluster_lba(uint16_t cluster) {
    return volume.data_lba + (uint32_t)(cluster - 2) * volume.spc;
}

static uint16_t fat_get(uint16_t cluster) {
    if (!fat_cache || cluster >= volume.cluster_count + 2) {
        return 0xFFFF;
    }
    return fat_cache[cluster];
}

static void fat_set(uint16_t cluster, uint16_t value) {
    if (fat_cache && cluster < volume.cluster_count + 2) {
        fat_cache[cluster] = value;
    }
}

static bool fat_sync(void) {
    uint32_t fat_bytes = (uint32_t)volume.fat_sectors * 512u;
    for (uint8_t copy = 0; copy < volume.fat_count; copy++) {
        uint32_t lba = volume.fat_lba + (uint32_t)copy * volume.fat_sectors;
        uint32_t done = 0;
        while (done < fat_bytes) {
            uint32_t remaining = fat_bytes - done;
            uint8_t count = (uint8_t)(remaining >= 128u * 512u ? 128 : (remaining / 512u));
            if (!count) {
                count = 1;
            }
            if (!ata_write_sectors(lba + done / 512u, count, (uint8_t *)fat_cache + done)) {
                return false;
            }
            done += (uint32_t)count * 512u;
        }
    }
    return ata_flush();
}

static void zero_cluster(uint16_t cluster) {
    uint8_t zeros[512];
    kmemset(zeros, 0, sizeof(zeros));
    uint32_t lba = cluster_lba(cluster);
    for (uint32_t i = 0; i < volume.spc; i++) {
        ata_write_sectors(lba + i, 1, zeros);
    }
}

static uint16_t alloc_cluster(void) {
    for (uint32_t cluster = 2; cluster < volume.cluster_count + 2; cluster++) {
        if (fat_get((uint16_t)cluster) == 0) {
            fat_set((uint16_t)cluster, 0xFFFF);
            zero_cluster((uint16_t)cluster);
            return (uint16_t)cluster;
        }
    }
    return 0;
}

static void free_chain(uint16_t cluster) {
    uint32_t guard = 0;
    while (cluster >= 2 && cluster < FAT16_EOC && guard++ < volume.cluster_count) {
        uint16_t next = fat_get(cluster);
        fat_set(cluster, 0);
        if (next >= FAT16_EOC || next < 2) {
            break;
        }
        cluster = next;
    }
}

static bool read_cluster(uint16_t cluster, uint8_t *buffer) {
    uint32_t lba = cluster_lba(cluster);
    for (uint32_t i = 0; i < volume.spc; i++) {
        if (!ata_read_sectors(lba + i, 1, buffer + i * 512u)) {
            return false;
        }
    }
    return true;
}

static bool write_cluster(uint16_t cluster, const uint8_t *buffer) {
    uint32_t lba = cluster_lba(cluster);
    for (uint32_t i = 0; i < volume.spc; i++) {
        if (!ata_write_sectors(lba + i, 1, buffer + i * 512u)) {
            return false;
        }
    }
    return true;
}

static bool load_directory(uint16_t dir_cluster) {
    dir_buffer_size = 0;
    if (dir_cluster == 0) {
        uint32_t bytes = (uint32_t)volume.root_entries * 32u;
        if (bytes > FAT16_MAX_DIR_BYTES) {
            bytes = FAT16_MAX_DIR_BYTES;
        }
        for (uint32_t offset = 0; offset < bytes; offset += 512u) {
            if (!ata_read_sectors(volume.root_lba + offset / 512u, 1, dir_buffer + offset)) {
                return false;
            }
        }
        dir_buffer_size = bytes;
        return true;
    }

    uint16_t cluster = dir_cluster;
    uint32_t guard = 0;
    while (cluster >= 2 && cluster < FAT16_EOC && guard++ < volume.cluster_count) {
        if (dir_buffer_size + volume.spc_bytes > FAT16_MAX_DIR_BYTES) {
            break;
        }
        if (!read_cluster(cluster, dir_buffer + dir_buffer_size)) {
            return false;
        }
        dir_buffer_size += volume.spc_bytes;
        uint16_t next = fat_get(cluster);
        if (next >= FAT16_EOC || next < 2) {
            break;
        }
        cluster = next;
    }
    return true;
}

static uint16_t dir_last_cluster(uint16_t dir_cluster) {
    if (dir_cluster == 0) {
        return 0;
    }
    uint16_t cluster = dir_cluster;
    uint32_t guard = 0;
    while (guard++ < volume.cluster_count) {
        uint16_t next = fat_get(cluster);
        if (next >= FAT16_EOC || next < 2) {
            return cluster;
        }
        cluster = next;
    }
    return cluster;
}

static bool dir_alloc_slot(uint16_t dir_cluster, uint32_t *slot) {
    if (!load_directory(dir_cluster)) {
        return false;
    }
    for (uint32_t offset = 0; offset < dir_buffer_size; offset += 32u) {
        if (dir_buffer[offset] == 0x00 || dir_buffer[offset] == 0xE5) {
            *slot = offset;
            return true;
        }
    }
    if (dir_cluster == 0) {
        return false;
    }
    uint16_t last = dir_last_cluster(dir_cluster);
    uint16_t new_cluster = alloc_cluster();
    if (!new_cluster) {
        return false;
    }
    fat_set(last, new_cluster);
    fat_set(new_cluster, 0xFFFF);
    if (!fat_sync()) {
        return false;
    }
    *slot = dir_buffer_size;
    return true;
}

static bool locate_dir_sector(uint16_t dir_cluster, uint32_t slot, uint32_t *lba, uint32_t *offset) {
    if (dir_cluster == 0) {
        if (slot >= (uint32_t)volume.root_entries * 32u) {
            return false;
        }
        *lba = volume.root_lba + slot / 512u;
        *offset = slot % 512u;
        return true;
    }
    uint32_t cluster_index = slot / volume.spc_bytes;
    uint32_t in_offset = slot % volume.spc_bytes;
    uint16_t cluster = dir_cluster;
    uint32_t guard = 0;
    while (guard++ < volume.cluster_count) {
        if (cluster < 2 || cluster >= FAT16_EOC) {
            return false;
        }
        if (cluster_index == 0) {
            *lba = cluster_lba(cluster) + in_offset / 512u;
            *offset = in_offset % 512u;
            return true;
        }
        cluster = fat_get(cluster);
        cluster_index--;
    }
    return false;
}

static bool dir_write_entry(uint16_t dir_cluster, uint32_t slot, const raw_dir_entry_t *entry) {
    uint32_t lba;
    uint32_t offset;
    if (!locate_dir_sector(dir_cluster, slot, &lba, &offset)) {
        return false;
    }
    uint8_t sector[512];
    if (!ata_read_sectors(lba, 1, sector)) {
        return false;
    }
    kmemcpy(sector + offset, entry, sizeof(raw_dir_entry_t));
    return ata_write_sectors(lba, 1, sector);
}

static bool dir_read_entry(uint16_t dir_cluster, uint32_t slot, raw_dir_entry_t *entry) {
    uint32_t lba;
    uint32_t offset;
    if (!locate_dir_sector(dir_cluster, slot, &lba, &offset)) {
        return false;
    }
    uint8_t sector[512];
    if (!ata_read_sectors(lba, 1, sector)) {
        return false;
    }
    kmemcpy(entry, sector + offset, sizeof(raw_dir_entry_t));
    return true;
}

static void raw_name_string(const raw_dir_entry_t *raw, char *out, size_t out_size) {
    char base[9];
    char ext[4];
    int bi = 0;
    int ei = 0;
    for (int i = 0; i < 8; i++) {
        if (raw->name[i] != ' ' && raw->name[i] != 0) {
            base[bi++] = (char)raw->name[i];
        }
    }
    base[bi] = 0;
    for (int i = 8; i < 11; i++) {
        if (raw->name[i] != ' ' && raw->name[i] != 0) {
            ext[ei++] = (char)raw->name[i];
        }
    }
    ext[ei] = 0;
    if (ei) {
        snprintf(out, out_size, "%s.%s", base, ext);
    } else {
        snprintf(out, out_size, "%s", base);
    }
}

void fat16_name_to_83(const char *name, char out[11]) {
    kmemset(out, ' ', 11);
    if (!name) {
        return;
    }
    char upper[64];
    size_t length = kstrlen(name);
    if (length >= sizeof(upper)) {
        length = sizeof(upper) - 1;
    }
    for (size_t i = 0; i < length; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (c == ' ' || c == '.' || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            if (c == '.') {
                upper[i] = '.';
            } else {
                upper[i] = '_';
            }
        } else {
            upper[i] = c;
        }
    }
    upper[length] = 0;
    char *dot = kstrrchr(upper, '.');
    size_t base_len = dot ? (size_t)(dot - upper) : length;
    if (base_len > 8) {
        base_len = 8;
    }
    for (size_t i = 0; i < base_len; i++) {
        char c = upper[i];
        if (c == '.') {
            c = '_';
        }
        out[i] = c;
    }
    if (dot && dot[1]) {
        for (size_t i = 0; i < 3 && dot[1 + i]; i++) {
            out[8 + i] = dot[1 + i];
        }
    }
}

void fat16_entry_display_name(const fat_entry_t *entry, char *out, size_t out_size) {
    if (!entry || !out || !out_size) {
        return;
    }
    char temp[32];
    kstrncpy(temp, entry->name, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;
    kstr_tolower(temp);
    bool new_word = true;
    for (char *p = temp; *p; p++) {
        if (new_word && *p >= 'a' && *p <= 'z') {
            *p = (char)(*p - 32);
        }
        new_word = (*p == ' ' || *p == '-' || *p == '_');
    }
    kstrncpy(out, temp, out_size - 1);
    out[out_size - 1] = 0;
}

static void entry_from_raw(const raw_dir_entry_t *raw, uint32_t slot, fat_entry_t *out) {
    kmemset(out, 0, sizeof(*out));
    raw_name_string(raw, out->name, sizeof(out->name));
    out->attr = raw->attr;
    out->size = raw->size;
    out->cluster = raw->cluster_low;
    out->slot = slot;
    fat16_entry_display_name(out, out->display, sizeof(out->display));
}

static bool entry_matches(const raw_dir_entry_t *raw, const char name83[11]) {
    return raw->name[0] != 0xE5 && raw->name[0] != 0x00 &&
           (raw->attr & 0x3F) != 0x0F && kmemcmp(raw->name, name83, 11) == 0;
}

static void name83_string(const char name83[11], char *out, size_t out_size) {
    char base[9];
    char ext[4];
    int bi = 0;
    int ei = 0;
    for (int i = 0; i < 8; i++) {
        if (name83[i] != ' ' && name83[i]) {
            base[bi++] = name83[i];
        }
    }
    base[bi] = 0;
    for (int i = 8; i < 11; i++) {
        if (name83[i] != ' ' && name83[i]) {
            ext[ei++] = name83[i];
        }
    }
    ext[ei] = 0;
    if (ei) {
        snprintf(out, out_size, "%s.%s", base, ext);
    } else {
        snprintf(out, out_size, "%s", base);
    }
}

bool fat16_mount(void) {
    mounted = false;
    kmemset(&volume, 0, sizeof(volume));
    if (!ata_present()) {
        kprintf("[fat16] no ATA disk\n");
        return false;
    }
    uint8_t sector[512];
    if (!ata_read_sectors(0, 1, sector)) {
        return false;
    }
    uint32_t part_lba = 0;
    bool has_partition = false;
    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        for (int i = 0; i < 4; i++) {
            uint8_t type = sector[446 + i * 16 + 4];
            if (type == 0x06 || type == 0x04 || type == 0x01 ||
                type == 0x0B || type == 0x0C || type == 0x0E) {
                part_lba = read_u32(&sector[446 + i * 16 + 8]);
                has_partition = true;
                break;
            }
        }
    }
    if (!has_partition) {
        part_lba = 0;
    }
    if (!ata_read_sectors(part_lba, 1, sector)) {
        return false;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }
    fat_bpb_t bpb;
    bpb.bytes_per_sector = read_u16(&sector[11]);
    bpb.sectors_per_cluster = sector[13];
    bpb.reserved_sectors = read_u16(&sector[14]);
    bpb.fat_count = sector[16];
    bpb.root_entries = read_u16(&sector[17]);
    bpb.total_sectors_16 = read_u16(&sector[19]);
    bpb.media = sector[21];
    bpb.fat_sectors = read_u16(&sector[22]);
    bpb.total_sectors = bpb.total_sectors_16 ? bpb.total_sectors_16 : read_u32(&sector[32]);

    if (bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0 ||
        (bpb.sectors_per_cluster & (bpb.sectors_per_cluster - 1)) ||
        bpb.fat_count == 0 || bpb.fat_sectors == 0 || bpb.root_entries == 0) {
        kprintf("[fat16] invalid BPB\n");
        return false;
    }

    volume.partition_lba = part_lba;
    volume.volume_sectors = bpb.total_sectors;
    volume.spc = bpb.sectors_per_cluster;
    volume.spc_bytes = (uint32_t)bpb.sectors_per_cluster * 512u;
    volume.reserved_sectors = bpb.reserved_sectors;
    volume.fat_count = bpb.fat_count;
    volume.fat_sectors = bpb.fat_sectors;
    volume.root_entries = bpb.root_entries;
    volume.root_sectors = ((uint32_t)bpb.root_entries * 32u + 511u) / 512u;
    volume.fat_lba = part_lba + bpb.reserved_sectors;
    volume.root_lba = volume.fat_lba + (uint32_t)bpb.fat_count * bpb.fat_sectors;
    volume.data_lba = volume.root_lba + volume.root_sectors;
    volume.cluster_count = (bpb.total_sectors - bpb.reserved_sectors -
                            (uint32_t)bpb.fat_count * bpb.fat_sectors -
                            volume.root_sectors) / bpb.sectors_per_cluster;

    if (volume.spc_bytes > FAT16_MAX_CLUSTER_BUFFER || volume.cluster_count < 4085) {
        kprintf("[fat16] unsupported volume (spc=%u clusters=%u)\n",
                (uint32_t)volume.spc_bytes, volume.cluster_count);
        return false;
    }

    kstrncpy(volume.label, (const char *)&sector[43], 11);
    volume.label[11] = 0;
    ktrim(volume.label);
    if (!volume.label[0]) {
        kstrcpy(volume.label, "BORDIFY");
    }

    uint32_t fat_bytes = (uint32_t)volume.fat_sectors * 512u;
    fat_cache = (uint16_t *)kmalloc(fat_bytes);
    if (!fat_cache) {
        return false;
    }
    if (!ata_read_sectors(volume.fat_lba, (uint8_t)MIN(volume.fat_sectors, 255), fat_cache)) {
        return false;
    }
    if (volume.fat_sectors > 255) {
        uint32_t remaining = volume.fat_sectors - 255;
        uint32_t cursor = 255;
        while (remaining) {
            uint8_t count = (uint8_t)MIN(remaining, 255u);
            if (!ata_read_sectors(volume.fat_lba + cursor, count,
                                  (uint8_t *)fat_cache + cursor * 512u)) {
                return false;
            }
            cursor += count;
            remaining -= count;
        }
    }

    mounted = true;
    kprintf("[fat16] mounted \"%s\": %u clusters, %u bytes/cluster\n",
            volume.label, volume.cluster_count, volume.spc_bytes);
    return true;
}

bool fat16_mounted(void) {
    return mounted;
}

const fat_volume_t *fat16_volume(void) {
    return &volume;
}

int fat16_list(uint16_t dir_cluster, fat_entry_t *entries, int max_entries) {
    if (!mounted || !entries || max_entries <= 0) {
        return 0;
    }
    if (!load_directory(dir_cluster)) {
        return 0;
    }
    int count = 0;
    for (uint32_t offset = 0; offset + 32u <= dir_buffer_size; offset += 32u) {
        raw_dir_entry_t *raw = (raw_dir_entry_t *)(dir_buffer + offset);
        if (raw->name[0] == 0x00) {
            break;
        }
        if (raw->name[0] == 0xE5) {
            continue;
        }
        if ((raw->attr & 0x3F) == 0x0F || (raw->attr & FAT_ATTR_VOLUME_ID)) {
            continue;
        }
        if (raw->name[0] == '.') {
            continue;
        }
        if (count < max_entries) {
            entry_from_raw(raw, offset, &entries[count]);
            count++;
        }
    }
    for (int i = 1; i < count; i++) {
        fat_entry_t key = entries[i];
        int j = i - 1;
        while (j >= 0) {
            bool key_dir = (key.attr & FAT_ATTR_DIRECTORY) != 0;
            bool other_dir = (entries[j].attr & FAT_ATTR_DIRECTORY) != 0;
            bool before = (key_dir && !other_dir) ||
                          (key_dir == other_dir && kstricmp(key.display, entries[j].display) < 0);
            if (!before) {
                break;
            }
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
    return count;
}

bool fat16_find(uint16_t dir_cluster, const char *name, fat_entry_t *entry) {
    fat_entry_t list[128];
    int count = fat16_list(dir_cluster, list, 128);
    for (int i = 0; i < count; i++) {
        if (kstricmp(list[i].name, name) == 0) {
            if (entry) {
                *entry = list[i];
            }
            return true;
        }
    }
    return false;
}

static bool add_entry(uint16_t dir_cluster, const char name83[11], uint8_t attr,
                      uint16_t cluster, uint32_t size) {
    uint32_t slot;
    if (!dir_alloc_slot(dir_cluster, &slot)) {
        return false;
    }
    raw_dir_entry_t raw;
    kmemset(&raw, 0, sizeof(raw));
    kmemcpy(raw.name, name83, 11);
    raw.attr = attr;
    raw.cluster_low = cluster;
    raw.cluster_high = 0;
    raw.size = size;
    rtc_time_t now;
    rtc_read(&now);
    raw.write_time = (uint16_t)((now.hour << 11) | (now.minute << 5) | (now.second / 2));
    raw.write_date = (uint16_t)(((now.year - 1980) << 9) | (now.month << 5) | now.day);
    return dir_write_entry(dir_cluster, slot, &raw);
}

bool fat16_create_file(uint16_t dir_cluster, const char *name) {
    if (!mounted) {
        return false;
    }
    char name83[11];
    fat16_name_to_83(name, name83);
    if (name83[0] == ' ') {
        return false;
    }
    fat_entry_t existing;
    char trimmed[13];
    name83_string(name83, trimmed, sizeof(trimmed));
    if (fat16_find(dir_cluster, trimmed, &existing)) {
        return false;
    }
    return add_entry(dir_cluster, name83, FAT_ATTR_ARCHIVE, 0, 0);
}

bool fat16_mkdir(uint16_t dir_cluster, const char *name) {
    if (!mounted) {
        return false;
    }
    char name83[11];
    fat16_name_to_83(name, name83);
    if (name83[0] == ' ' || name83[0] == '.') {
        return false;
    }
    char trimmed[13];
    name83_string(name83, trimmed, sizeof(trimmed));
    fat_entry_t existing;
    if (fat16_find(dir_cluster, trimmed, &existing)) {
        return false;
    }
    uint16_t cluster = alloc_cluster();
    if (!cluster) {
        return false;
    }
    uint8_t sector[512];
    kmemset(sector, 0, sizeof(sector));
    raw_dir_entry_t *dot = (raw_dir_entry_t *)sector;
    kmemset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = FAT_ATTR_DIRECTORY;
    dot->cluster_low = cluster;
    raw_dir_entry_t *dotdot = (raw_dir_entry_t *)(sector + 32);
    kmemset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = FAT_ATTR_DIRECTORY;
    dotdot->cluster_low = dir_cluster;
    if (!ata_write_sectors(cluster_lba(cluster), 1, sector)) {
        free_chain(cluster);
        fat_sync();
        return false;
    }
    if (!add_entry(dir_cluster, name83, FAT_ATTR_DIRECTORY, cluster, 0)) {
        free_chain(cluster);
        fat_sync();
        return false;
    }
    fat_sync();
    return true;
}

static bool delete_entry(uint16_t dir_cluster, const fat_entry_t *entry) {
    raw_dir_entry_t raw;
    if (!dir_read_entry(dir_cluster, entry->slot, &raw)) {
        return false;
    }
    raw.name[0] = 0xE5;
    if (!dir_write_entry(dir_cluster, entry->slot, &raw)) {
        return false;
    }
    if (entry->cluster >= 2) {
        free_chain(entry->cluster);
    }
    fat_sync();
    return true;
}

bool fat16_delete(uint16_t dir_cluster, const char *name) {
    if (!mounted) {
        return false;
    }
    fat_entry_t entry;
    if (!fat16_find(dir_cluster, name, &entry)) {
        return false;
    }
    if (entry.attr & FAT_ATTR_DIRECTORY) {
        fat_entry_t children[16];
        int count = fat16_list(entry.cluster, children, 16);
        if (count > 0) {
            return false;
        }
    }
    return delete_entry(dir_cluster, &entry);
}

bool fat16_rename(uint16_t dir_cluster, const char *name, const char *new_name) {
    if (!mounted || !name || !new_name) {
        return false;
    }
    fat_entry_t entry;
    if (!fat16_find(dir_cluster, name, &entry)) {
        return false;
    }
    char name83[11];
    fat16_name_to_83(new_name, name83);
    if (name83[0] == ' ' || name83[0] == '.') {
        return false;
    }
    char trimmed[13];
    name83_string(name83, trimmed, sizeof(trimmed));
    fat_entry_t existing;
    if (fat16_find(dir_cluster, trimmed, &existing)) {
        return false;
    }
    raw_dir_entry_t raw;
    if (!dir_read_entry(dir_cluster, entry.slot, &raw)) {
        return false;
    }
    kmemcpy(raw.name, name83, 11);
    if (!dir_write_entry(dir_cluster, entry.slot, &raw)) {
        return false;
    }
    return fat_sync();
}

static int read_entry_data(const fat_entry_t *entry, void *buffer, uint32_t max_size) {
    if (!entry || !buffer || max_size == 0) {
        return -1;
    }
    uint32_t wanted = MIN(entry->size, max_size);
    if (wanted == 0) {
        ((char *)buffer)[0] = 0;
        return 0;
    }
    uint16_t cluster = entry->cluster;
    uint32_t copied = 0;
    uint32_t guard = 0;
    while (copied < wanted && cluster >= 2 && cluster < FAT16_EOC &&
           guard++ < volume.cluster_count) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return -1;
        }
        uint32_t take = MIN(volume.spc_bytes, wanted - copied);
        kmemcpy((uint8_t *)buffer + copied, cluster_buffer, take);
        copied += take;
        if (copied >= wanted) {
            break;
        }
        cluster = fat_get(cluster);
    }
    if (copied < max_size) {
        ((char *)buffer)[copied] = 0;
    }
    return (int)copied;
}

int fat16_read(uint16_t dir_cluster, const char *name, void *buffer, uint32_t max_size) {
    fat_entry_t entry;
    if (!fat16_find(dir_cluster, name, &entry)) {
        return -1;
    }
    return read_entry_data(&entry, buffer, max_size);
}

static bool write_entry_data(fat_entry_t *entry, uint16_t dir_cluster,
                             const void *buffer, uint32_t size) {
    uint32_t clusters_needed = (size + volume.spc_bytes - 1) / volume.spc_bytes;
    uint16_t first = 0;
    uint16_t prev = 0;
    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t cluster = alloc_cluster();
        if (!cluster) {
            if (first) {
                free_chain(first);
            }
            fat_sync();
            return false;
        }
        if (!first) {
            first = cluster;
        }
        if (prev) {
            fat_set(prev, cluster);
        }
        fat_set(cluster, 0xFFFF);
        prev = cluster;

        uint32_t offset = i * volume.spc_bytes;
        uint32_t take = MIN(volume.spc_bytes, size - offset);
        kmemset(cluster_buffer, 0, volume.spc_bytes);
        kmemcpy(cluster_buffer, (const uint8_t *)buffer + offset, take);
        if (!write_cluster(cluster, cluster_buffer)) {
            free_chain(first);
            fat_sync();
            return false;
        }
    }
    if (clusters_needed == 0) {
        first = 0;
    }
    raw_dir_entry_t raw;
    if (!dir_read_entry(dir_cluster, entry->slot, &raw)) {
        if (first) {
            free_chain(first);
        }
        return false;
    }
    if (raw.cluster_low >= 2 && raw.cluster_low != first) {
        free_chain(raw.cluster_low);
    }
    raw.cluster_low = first;
    raw.cluster_high = 0;
    raw.size = size;
    raw.attr |= FAT_ATTR_ARCHIVE;
    if (!dir_write_entry(dir_cluster, entry->slot, &raw)) {
        if (first) {
            free_chain(first);
        }
        fat_sync();
        return false;
    }
    entry->cluster = first;
    entry->size = size;
    fat_sync();
    return true;
}

int fat16_write(uint16_t dir_cluster, const char *name, const void *buffer, uint32_t size) {
    if (!mounted) {
        return -1;
    }
    fat_entry_t entry;
    if (fat16_find(dir_cluster, name, &entry)) {
        return write_entry_data(&entry, dir_cluster, buffer, size) ? (int)size : -1;
    }
    char name83[11];
    fat16_name_to_83(name, name83);
    if (name83[0] == ' ') {
        return -1;
    }
    if (!add_entry(dir_cluster, name83, FAT_ATTR_ARCHIVE, 0, 0)) {
        return -1;
    }
    if (!fat16_find(dir_cluster, name, &entry)) {
        return -1;
    }
    return write_entry_data(&entry, dir_cluster, buffer, size) ? (int)size : -1;
}

static bool resolve_path(const char *path, uint16_t *parent_cluster, fat_entry_t *entry,
                         char *leaf, size_t leaf_size) {
    if (!path || !*path) {
        return false;
    }
    char copy[FAT16_MAX_PATH];
    kstrncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    for (char *p = copy; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    char *cursor = copy;
    while (*cursor == '/') {
        cursor++;
    }
    uint16_t current = 0;
    while (*cursor) {
        char component[32];
        int n = 0;
        while (*cursor && *cursor != '/' && n < 31) {
            component[n++] = *cursor++;
        }
        component[n] = 0;
        while (*cursor == '/') {
            cursor++;
        }
        ktrim(component);
        if (!component[0]) {
            continue;
        }
        fat_entry_t found;
        if (!fat16_find(current, component, &found)) {
            if (!*cursor && parent_cluster && leaf) {
                *parent_cluster = current;
                kstrncpy(leaf, component, leaf_size - 1);
                leaf[leaf_size - 1] = 0;
                return false;
            }
            return false;
        }
        if (!*cursor) {
            if (parent_cluster) {
                *parent_cluster = current;
            }
            if (entry) {
                *entry = found;
            }
            if (leaf) {
                kstrncpy(leaf, component, leaf_size - 1);
                leaf[leaf_size - 1] = 0;
            }
            return true;
        }
        if (!(found.attr & FAT_ATTR_DIRECTORY)) {
            return false;
        }
        current = found.cluster;
    }
    return false;
}

int fat16_read_path(const char *path, void *buffer, uint32_t max_size) {
    uint16_t parent;
    fat_entry_t entry;
    if (!resolve_path(path, &parent, &entry, NULLPTR, 0)) {
        return -1;
    }
    return read_entry_data(&entry, buffer, max_size);
}

int fat16_write_path(const char *path, const void *buffer, uint32_t size) {
    uint16_t parent = 0;
    char leaf[32];
    leaf[0] = 0;
    fat_entry_t entry;
    bool found = resolve_path(path, &parent, &entry, leaf, sizeof(leaf));
    if (!found) {
        if (!leaf[0]) {
            return -1;
        }
        return fat16_write(parent, leaf, buffer, size);
    }
    return fat16_write(parent, leaf, buffer, size);
}

bool fat16_exists_path(const char *path) {
    uint16_t parent;
    fat_entry_t entry;
    return resolve_path(path, &parent, &entry, NULLPTR, 0);
}

uint32_t fat16_free_clusters(void) {
    if (!mounted || !fat_cache) {
        return 0;
    }
    uint32_t free_count = 0;
    for (uint32_t cluster = 2; cluster < volume.cluster_count + 2; cluster++) {
        if (fat_cache[cluster] == 0) {
            free_count++;
        }
    }
    return free_count;
}
