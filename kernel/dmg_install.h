#ifndef BORDIFY_DMG_INSTALL_H
#define BORDIFY_DMG_INSTALL_H
/* Audited-only DMG -> HFS+ root file -> FAT16 extraction -> ARM64 demo.
 * Does NOT install general macOS apps, PKGs, APFS or Cocoa bundles. */
int dmg_install_demo(void);
int dmg_installed_demo_run(void);
#endif
