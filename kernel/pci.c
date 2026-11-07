#include "pci.h"
#include "io.h"

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset & 0xFC);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t old = pci_config_read32(bus, slot, function, offset & 0xFC);
    uint32_t shift = (uint32_t)((offset & 2) * 8);
    uint32_t mask = 0xFFFFu << shift;
    old = (old & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, slot, function, offset & 0xFC, old);
}

bool pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0);
                uint16_t v = (uint16_t)(id & 0xFFFF);
                uint16_t d = (uint16_t)((id >> 16) & 0xFFFF);
                if (v == 0xFFFF || v == 0) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                if (v == vendor && (device == 0xFFFF || d == device)) {
                    if (out) {
                        out->bus = (uint8_t)bus;
                        out->slot = slot;
                        out->function = function;
                        out->vendor_id = v;
                        out->device_id = d;
                        uint32_t class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                        out->class_code = (uint8_t)(class_reg >> 24);
                        out->subclass = (uint8_t)(class_reg >> 16);
                        out->bar0 = pci_config_read32((uint8_t)bus, slot, function, 0x10);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

void pci_enable_device(pci_device_t *device) {
    uint16_t command = pci_config_read16(device->bus, device->slot, device->function, 0x04);
    command |= 0x0007;
    pci_config_write16(device->bus, device->slot, device->function, 0x04, command);
}
