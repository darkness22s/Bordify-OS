#include "ata.h"
#include "io.h"
#include "printf.h"
#include "string.h"

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_LBA28_SECTORS 0x10000000u

/* ATA PIO reports completion separately from each data-ready phase.
 * A write is not committed until BSY clears and a subsequent cache flush
 * has completed without ERR/DF. Never report a failed write as success. */
static bool valid_lba28(uint32_t lba,uint8_t sectors) {
    return sectors>0 && lba<ATA_LBA28_SECTORS &&
           (uint32_t)sectors<=ATA_LBA28_SECTORS-lba;
}

static bool present;

static int wait_not_busy(uint32_t timeout) {
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

static bool wait_command_complete(uint32_t timeout) {
    if (wait_not_busy(timeout)<0) return false;
    uint8_t status=inb(ATA_PRIMARY_IO+7);
    return status!=0xff && (status&(ATA_SR_ERR|ATA_SR_DF))==0;
}

static int wait_drq(uint32_t timeout) {
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if (status & ATA_SR_ERR) {
            return -2;
        }
        if (status & ATA_SR_DF) {
            return -3;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -1;
}

bool ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0);
    outb(ATA_PRIMARY_IO + 6, 0xA0);
    io_wait();
    if (wait_not_busy(1000000) < 0) {
        present = false;
        return false;
    }
    outb(ATA_PRIMARY_IO + 2, 0);
    outb(ATA_PRIMARY_IO + 3, 0);
    outb(ATA_PRIMARY_IO + 4, 0);
    outb(ATA_PRIMARY_IO + 5, 0);
    outb(ATA_PRIMARY_IO + 7, 0xEC);
    uint8_t status = inb(ATA_PRIMARY_IO + 7);
    if (status == 0) {
        present = false;
        return false;
    }
    if (wait_not_busy(1000000) < 0) {
        present = false;
        return false;
    }
    if (inb(ATA_PRIMARY_IO + 4) != 0 || inb(ATA_PRIMARY_IO + 5) != 0) {
        present = false;
        return false;
    }
    if (wait_drq(1000000) < 0) {
        present = false;
        return false;
    }
    for (int i = 0; i < 256; i++) {
        (void)inw(ATA_PRIMARY_IO);
    }
    present = true;
    kprintf("[ata] primary master online\n");
    return true;
}

bool ata_present(void) {
    return present;
}

bool ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!present || !buffer || !valid_lba28(lba,count)) {
        return false;
    }
    uint32_t irq = irq_save();
    if (wait_not_busy(2000000) < 0) {
        irq_restore(irq);
        return false;
    }
    outb(ATA_PRIMARY_IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_PRIMARY_CTRL, 0);
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + 7, 0x20);

    uint16_t *out = (uint16_t *)buffer;
    for (uint32_t sector = 0; sector < count; sector++) {
        if (wait_drq(2000000) < 0) {
            irq_restore(irq);
            return false;
        }
        for (int i = 0; i < 256; i++) {
            out[sector * 256 + i] = inw(ATA_PRIMARY_IO);
        }
    }
    irq_restore(irq);
    return true;
}

bool ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!present || !buffer || !valid_lba28(lba,count)) {
        return false;
    }
    uint32_t irq = irq_save();
    if (wait_not_busy(2000000) < 0) {
        irq_restore(irq);
        return false;
    }
    outb(ATA_PRIMARY_IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_PRIMARY_CTRL, 0);
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + 7, 0x30);

    const uint16_t *in = (const uint16_t *)buffer;
    for (uint32_t sector = 0; sector < count; sector++) {
        if (wait_drq(2000000) < 0) {
            irq_restore(irq);
            return false;
        }
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_IO, in[sector * 256 + i]);
        }
    }
    /* The final PIO data block must finish before FLUSH CACHE. */
    if (!wait_command_complete(2000000)) {
        irq_restore(irq);
        return false;
    }
    outb(ATA_PRIMARY_IO + 7, 0xE7);
    bool flushed=wait_command_complete(2000000);
    irq_restore(irq);
    return flushed;
}

bool ata_flush(void) {
    if (!present) return false;
    uint32_t irq=irq_save();
    if (!wait_command_complete(2000000)) {
        irq_restore(irq);
        return false;
    }
    outb(ATA_PRIMARY_IO+7,0xE7);
    bool ok=wait_command_complete(2000000);
    irq_restore(irq);
    return ok;
}
