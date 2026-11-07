#ifndef BORDIFY_ATA_H
#define BORDIFY_ATA_H

#include "types.h"

bool ata_init(void);
bool ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);
bool ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);
bool ata_flush(void);
bool ata_present(void);

#endif
