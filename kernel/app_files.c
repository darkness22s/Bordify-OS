#include "gui.h"
#include "gfx.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "input.h"
#include "fat16.h"
#include "pit.h"
#include "string.h"
#include "printf.h"
#include "exec_inspect.h"
#include "package_inspect.h"
#include "udif_blkx.h"
#include "process.h"

#define FILES_MAX_ENTRIES 128
#define FILES_VIEWER_SIZE 14336
#define FILES_MAX_DEPTH 16

typedef enum {
    PROMPT_NONE = 0,
    PROMPT_FOLDER,
    PROMPT_FILE,
    PROMPT_RENAME,
    PROMPT_DELETE,
} prompt_mode_t;

typedef struct {
    gui_window_t *window;
    uint16_t cluster;
    uint16_t stack[FILES_MAX_DEPTH];
    int depth;
    char path[160];
    fat_entry_t entries[FILES_MAX_ENTRIES];
    int count;
    int selected;
    int scroll;
    int last_click_index;
    uint32_t last_click_tick;
    char status[96];
    uint32_t status_tick;
    prompt_mode_t prompt;
    char prompt_text[32];
    char prompt_target[13];
    bool viewing;
    char viewer_name[32];
    char viewer_text[FILES_VIEWER_SIZE];
    int viewer_scroll;
} files_state_t;

typedef struct {
    int x0;
    int y0;
    int w;
    int h;
    int toolbar_h;
    int sidebar_w;
    int list_x;
    int list_y;
    int list_w;
    int list_bottom;
    int row_h;
    int up_x;
    int up_y;
    int up_w;
    int folder_x;
    int file_x;
    int rename_x;
    int delete_x;
    int action_y;
    int action_h;
} files_layout_t;

static files_state_t fs;

static void files_refresh(void);

static void layout(gui_window_t *window, files_layout_t *l) {
    l->x0 = gui_content_x(window);
    l->y0 = gui_content_y(window);
    l->w = gui_content_width(window);
    l->h = gui_content_height(window);
    l->toolbar_h = 52;
    l->sidebar_w = 168;
    l->list_x = l->x0 + l->sidebar_w + 14;
    l->list_w = l->w - l->sidebar_w - 28;
    l->list_y = l->y0 + l->toolbar_h + 46;
    l->list_bottom = l->y0 + l->h - 44;
    l->row_h = 31;
    l->action_h = 30;
    l->action_y = l->y0 + 11;
    l->delete_x = l->x0 + l->w - 14 - 58;
    l->rename_x = l->delete_x - 8 - 72;
    l->file_x = l->rename_x - 8 - 68;
    l->folder_x = l->file_x - 8 - 84;
    l->up_w = 38;
    l->up_x = l->folder_x - 9 - l->up_w;
    l->up_y = l->action_y;
}

static void set_status(const char *text) {
    kstrncpy(fs.status, text, sizeof(fs.status) - 1);
    fs.status[sizeof(fs.status) - 1] = 0;
    fs.status_tick = pit_ticks();
}

static void prompt_reset(void) {
    fs.prompt = PROMPT_NONE;
    fs.prompt_text[0] = 0;
    fs.prompt_target[0] = 0;
}

static void open_prompt(prompt_mode_t mode) {
    fs.prompt = mode;
    fs.prompt_text[0] = 0;
    if (mode == PROMPT_FOLDER) {
        kstrcpy(fs.prompt_text, "");
    }
}

static void open_rename_prompt(void) {
    if (fs.selected < 0 || fs.selected >= fs.count) {
        set_status("Nothing is selected");
        return;
    }
    kstrncpy(fs.prompt_target, fs.entries[fs.selected].name,
             sizeof(fs.prompt_target) - 1);
    fs.prompt_target[sizeof(fs.prompt_target) - 1] = 0;
    kstrncpy(fs.prompt_text, fs.entries[fs.selected].name,
             sizeof(fs.prompt_text) - 1);
    fs.prompt_text[sizeof(fs.prompt_text) - 1] = 0;
    fs.prompt = PROMPT_RENAME;
}

static void files_set_root(void) {
    fs.depth = 0;
    fs.stack[0] = 0;
    fs.cluster = 0;
    kstrcpy(fs.path, "/");
    fs.selected = 0;
    fs.scroll = 0;
    files_refresh();
}

static bool files_go_named_in_root(const char *name) {
    fat_entry_t entry;
    if (!fat16_find(0, name, &entry) || !(entry.attr & FAT_ATTR_DIRECTORY)) {
        return false;
    }
    fs.depth = 1;
    fs.stack[0] = 0;
    fs.stack[1] = entry.cluster;
    fs.cluster = entry.cluster;
    snprintf(fs.path, sizeof(fs.path), "/%s", entry.name);
    fs.selected = 0;
    fs.scroll = 0;
    files_refresh();
    return true;
}

static void files_up(void) {
    if (fs.depth <= 0) {
        return;
    }
    fs.depth--;
    fs.cluster = fs.stack[fs.depth];
    char *slash = kstrrchr(fs.path, '/');
    if (slash && slash != fs.path) {
        *slash = 0;
    } else {
        kstrcpy(fs.path, "/");
    }
    fs.selected = 0;
    fs.scroll = 0;
    files_refresh();
}

static void files_enter_dir(const fat_entry_t *entry) {
    if (fs.depth + 1 >= FILES_MAX_DEPTH) {
        set_status("Too many nested folders");
        return;
    }
    fs.depth++;
    fs.stack[fs.depth] = entry->cluster;
    fs.cluster = entry->cluster;
    size_t len = kstrlen(fs.path);
    if (len == 0 || fs.path[len - 1] != '/') {
        if (len + 1 < sizeof(fs.path)) {
            fs.path[len++] = '/';
            fs.path[len] = 0;
        }
    }
    if (len + kstrlen(entry->name) < sizeof(fs.path)) {
        kstrcat(fs.path, entry->name);
    }
    fs.selected = 0;
    fs.scroll = 0;
    files_refresh();
}

/* Read-only UDIF sector preview: no filesystem mount, writes or installers. */
typedef struct {
    uint16_t cluster;
    const char *name;
    uint32_t size;
} dmg_file_reader_t;

static int files_read_dmg_bytes(void *context, uint64_t at,
                                void *dst, size_t length) {
    dmg_file_reader_t *file = (dmg_file_reader_t *)context;
    if (at > file->size || length > file->size - (uint32_t)at ||
        length > UINT32_MAX) return -1;
    return fat16_read_range(file->cluster, file->name,
                            (uint32_t)at, dst, (uint32_t)length);
}

static void files_inspect_dmg_blocks(const fat_entry_t *entry,
                                     const package_info_t *package) {
    if (!package->valid || !package->metadata_length ||
        package->metadata_length > 32768 ||
        package->metadata_offset > UINT32_MAX)
        return;
    static char xml[32768];
    static udif_block_map_t blocks;
    int count = fat16_read_range(fs.cluster, entry->name,
                                 (uint32_t)package->metadata_offset,
                                 xml, (uint32_t)package->metadata_length);
    if (count != (int)package->metadata_length) return;
    if (!udif_blkx_from_xml(xml, (size_t)count, package->data_offset,
                            entry->size, &blocks)) return;

    dmg_file_reader_t file = {fs.cluster, entry->name, entry->size};
    uint8_t sector[UDIF_SECTOR_SIZE];
    udif_io_result_t result =
        udif_blkx_read_sector(&blocks, blocks.sector_start,
                              files_read_dmg_bytes, &file, sector);
    const char *status = result == UDIF_IO_OK ? "Read-only sector access OK" :
                         result == UDIF_IO_UNSUPPORTED ? "Compressed sector: unsupported" :
                         "Sector read failed";
    size_t used = kstrlen(fs.viewer_text);
    if (used < sizeof(fs.viewer_text)) {
        snprintf(fs.viewer_text + used, sizeof(fs.viewer_text) - used,
                 "\n\nFirst blkx partition: %u mapped runs\n"
                 "Contains compressed blocks: %s\n"
                 "First sector: %s\n"
                 "Filesystem mounting and installation remain unsupported.",
                 blocks.runs_count, blocks.has_compressed ? "Yes" : "No",
                 status);
    }
}

static void files_open_selected(void) {
    if (fs.selected < 0 || fs.selected >= fs.count) {
        return;
    }
    fat_entry_t *entry = &fs.entries[fs.selected];
    if (entry->attr & FAT_ATTR_DIRECTORY) {
        files_enter_dir(entry);
        return;
    }
    /* Native ELF32 apps are launched from /APPS in isolated ring 3.
     * PE/Mach-O remain inspection-only until their runtimes are available. */
    if (kstricmp(fs.path, "/APPS") == 0 &&
        kends_with(entry->name, ".ELF")) {
        int code = process_run_elf32("APPS", entry->name);
        set_status(code == 0 ? "Native app exited successfully" :
                               "Native app failed; inspect serial log");
        return;
    }
    /* Only the audited Win32 demonstration is executable so far;
       arbitrary EXEs stay inspection-only until process limits improve. */
    if (kstricmp(fs.path, "/APPS") == 0 &&
        kstricmp(entry->name, "WINHELLO.EXE") == 0) {
        int code = process_run_win32_pe("APPS", entry->name);
        set_status(code == 0 ? "Win32 console demo exited successfully" :
                               "Win32 demo failed; inspect serial log");
        return;
    }
    int bytes = fat16_read(fs.cluster, entry->name, fs.viewer_text, sizeof(fs.viewer_text) - 1);
    if (bytes < 0) {
        set_status("Could not read file");
        return;
    }
    fs.viewer_text[bytes] = 0;
    /*
     * Installer/format inspection only. Never jump into untrusted code or
     * accidentally render raw installer data as a string.
     *
     * UDIF has its header at EOF; a 512-byte ranged FAT16 read avoids
     * bringing a potentially very large disk image into kernel memory.
     */
    package_info_t package;
    size_t name_len = kstrlen(entry->name);
    bool is_dmg = name_len >= 4 &&
                  kstricmp(entry->name + name_len - 4, ".DMG") == 0;
    bool identified = false;
    if (is_dmg) {
        uint8_t trailer[512];
        int tail_bytes = entry->size >= 512 ?
            fat16_read_range(fs.cluster, entry->name, entry->size - 512,
                             trailer, sizeof(trailer)) : -1;
        bool recognized = tail_bytes == 512 &&
            package_inspect_dmg_trailer(trailer, sizeof(trailer),
                                        entry->size, &package);
        if (recognized) {
            snprintf(fs.viewer_text, sizeof(fs.viewer_text),
                     "Apple disk image: %s\n"
                     "Type: %s\n"
                     "Trailer: %s\n"
                     "Image size: %u bytes\n"
                     "Virtual disk: %u MiB\n\n"
                     "%s\n\n"
                     "Bordify cannot mount or install DMG images yet.",
                     entry->display, package_format_name(package.format),
                     package.valid ? "Recognized" : "Malformed",
                     entry->size,
                     (uint32_t)(package.virtual_disk_bytes / (1024u * 1024u)),
                     package_compatibility_note(package.format));
            files_inspect_dmg_blocks(entry, &package);
        } else {
            snprintf(fs.viewer_text, sizeof(fs.viewer_text),
                     "Cannot open %s as a DMG.\n"
                     "A complete UDIF image must have a valid 512-byte\n"
                     "koly trailer at the end of the file.\n\n"
                     "The image may be truncated or use another format.",
                     entry->display);
        }
        identified = true;
    }
    if (!identified && package_inspect_prefix(fs.viewer_text, (size_t)bytes,
                                               entry->size, &package)) {
        snprintf(fs.viewer_text, sizeof(fs.viewer_text),
                 "Installer/container: %s\n"
                 "Type: %s\n"
                 "Header: %s\n"
                 "File size: %u bytes\n\n"
                 "%s\n\n"
                 "Bordify can inspect this container but cannot install it yet.",
                 entry->display, package_format_name(package.format),
                 package.valid ? "Recognized" : "Invalid or incomplete",
                 entry->size, package_compatibility_note(package.format));
        identified = true;
    }
    if (!identified) {
        exec_info_t executable;
        if (exec_inspect(fs.viewer_text, (size_t)bytes, &executable)) {
            snprintf(fs.viewer_text, sizeof(fs.viewer_text),
                     "Executable file: %s\n"
                     "Format: %s\n"
                     "CPU: %s\n"
                     "Header: %s\n"
                     "File size: %u bytes\n\n"
                     "%s\n\n"
                     "Bordify does not execute foreign binaries yet.\n"
                     "64-bit apps need a 64-bit process/runtime environment.",
                     entry->display,
                     exec_format_name(executable.format),
                     exec_arch_name(executable.arch),
                     executable.valid ? "Recognized" : "Invalid or truncated",
                     entry->size, exec_compatibility_note(&executable));
        }
    }
    kstrncpy(fs.viewer_name, entry->name, sizeof(fs.viewer_name) - 1);
    fs.viewer_name[sizeof(fs.viewer_name) - 1] = 0;
    fs.viewer_scroll = 0;
    fs.viewing = true;
}

static void files_refresh(void) {
    fs.count = fat16_list(fs.cluster, fs.entries, FILES_MAX_ENTRIES);
    if (fs.selected >= fs.count) {
        fs.selected = fs.count ? fs.count - 1 : 0;
    }
    if (fs.selected < 0) {
        fs.selected = 0;
    }
    int visible_rows = 1;
    if (fs.window) {
        files_layout_t l;
        layout(fs.window, &l);
        visible_rows = (l.list_bottom - l.list_y) / l.row_h;
    }
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    if (fs.selected < fs.scroll) {
        fs.scroll = fs.selected;
    }
    if (fs.selected >= fs.scroll + visible_rows) {
        fs.scroll = fs.selected - visible_rows + 1;
    }
    if (fs.scroll < 0) {
        fs.scroll = 0;
    }
}

static void confirm_prompt(void) {
    if (fs.prompt == PROMPT_FOLDER || fs.prompt == PROMPT_FILE) {
        if (!fs.prompt_text[0]) {
            set_status("Please enter a name");
            return;
        }
        bool ok;
        if (fs.prompt == PROMPT_FOLDER) {
            ok = fat16_mkdir(fs.cluster, fs.prompt_text);
            if (ok) {
                set_status("Folder created");
            } else {
                set_status("Could not create folder");
            }
        } else {
            ok = fat16_create_file(fs.cluster, fs.prompt_text);
            if (ok) {
                set_status("File created");
            } else {
                set_status("Could not create file");
            }
        }
        prompt_reset();
        files_refresh();
    } else if (fs.prompt == PROMPT_RENAME) {
        if (!fs.prompt_text[0]) {
            set_status("Please enter a name");
            return;
        }
        if (fat16_rename(fs.cluster, fs.prompt_target, fs.prompt_text)) {
            set_status("Renamed");
        } else {
            set_status("Could not rename item");
        }
        prompt_reset();
        files_refresh();
    } else if (fs.prompt == PROMPT_DELETE) {
        if (fs.selected >= 0 && fs.selected < fs.count) {
            char name[13];
            kstrncpy(name, fs.entries[fs.selected].name, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            if (fat16_delete(fs.cluster, name)) {
                set_status("Deleted");
            } else {
                set_status("Folder is not empty");
            }
            prompt_reset();
            files_refresh();
        }
    }
}

static void draw_wrapped_text(const char *text, int x, int y, int width, int height,
                              int scroll, const font_t *font, uint32_t color) {
    int line_height = font->line_height + 3;
    int line_index = 0;
    int draw_y = y;
    char line[192];
    int line_len = 0;
    const char *p = text;
    int max_width = width;

    while (*p) {
        if (*p == '\r') {
            p++;
            continue;
        }
        if (*p == '\n') {
            line[line_len] = 0;
            if (line_index >= scroll && !(line_len == 0 && line_index == 0)) {
                if (draw_y + line_height > y + height) {
                    break;
                }
                gfx_text_clip(font, x, draw_y, line, max_width, color);
                draw_y += line_height;
            }
            line_index++;
            line_len = 0;
            p++;
            continue;
        }
        char word[96];
        int word_len = 0;
        while (*p && *p != ' ' && *p != '\n' && *p != '\r' && word_len < 95) {
            word[word_len++] = *p++;
        }
        word[word_len] = 0;
        char candidate[192];
        kstrncpy(candidate, line, sizeof(candidate) - 1);
        candidate[sizeof(candidate) - 1] = 0;
        if (line_len && line_len + word_len + 2 < (int)sizeof(candidate)) {
            candidate[line_len++] = ' ';
            candidate[line_len] = 0;
            kstrcat(candidate, word);
        } else {
            kstrcpy(candidate, word);
        }
        if (line_len && gfx_text_width(font, candidate) > max_width) {
            line[line_len] = 0;
            if (line_index >= scroll) {
                if (draw_y + line_height > y + height) {
                    break;
                }
                gfx_text_clip(font, x, draw_y, line, max_width, color);
                draw_y += line_height;
            }
            line_index++;
            kstrncpy(line, word, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
            line_len = word_len;
            if (word_len == 0) {
                p++;
            }
        } else {
            kstrncpy(line, candidate, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
            line_len = (int)kstrlen(line);
            if (line_len == 0 && word_len == 0) {
                p++;
            }
        }
        while (*p == ' ') {
            p++;
        }
    }
    if (line_len) {
        line[line_len] = 0;
        if (line_index >= scroll && draw_y + line_height <= y + height) {
            gfx_text_clip(font, x, draw_y, line, max_width, color);
        }
    }
}

static void draw_toolbar(gui_window_t *window, files_layout_t *l) {
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    gfx_fill_rect(l->x0, l->y0, l->w, l->toolbar_h, settings_panel());
    gfx_hline(l->x0, l->x0 + l->w - 1, l->y0 + l->toolbar_h - 1, settings_line());
    icon_folder(l->x0 + 14, l->y0 + 11, 30, 0x9DCBF4, settings_strong());
    gfx_text_clip(&font_ui_bold, l->x0 + 42, l->y0 + 17, fs.path, l->up_x - l->x0 - 56,
                  settings_ink());

    bool hover_up = ui_point_in(l->up_x, l->up_y, l->up_w, l->action_h, mouse.x, mouse.y);
    ui_small_button(l->up_x, l->up_y, l->up_w, l->action_h, "Up", hover_up);
    bool hover_folder = ui_point_in(l->folder_x, l->action_y, 84, l->action_h, mouse.x, mouse.y);
    bool hover_file = ui_point_in(l->file_x, l->action_y, 68, l->action_h, mouse.x, mouse.y);
    bool hover_rename = ui_point_in(l->rename_x, l->action_y, 72, l->action_h, mouse.x, mouse.y);
    bool hover_delete = ui_point_in(l->delete_x, l->action_y, 58, l->action_h, mouse.x, mouse.y);
    ui_small_button(l->folder_x, l->action_y, 84, l->action_h, "New folder", hover_folder);
    ui_small_button(l->file_x, l->action_y, 68, l->action_h, "New file", hover_file);
    ui_small_button(l->rename_x, l->action_y, 72, l->action_h, "Rename", hover_rename);
    ui_small_button(l->delete_x, l->action_y, 58, l->action_h, "Delete", hover_delete);
}

static void draw_sidebar(gui_window_t *window, files_layout_t *l) {
    UNUSED(window);
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    int sx = l->x0;
    int sy = l->y0 + l->toolbar_h;
    int sh = l->h - l->toolbar_h;
    gfx_fill_rect(sx, sy, l->sidebar_w, sh, settings_panel_alt());
    gfx_vline(sx + l->sidebar_w, sy, sy + sh - 1, settings_line());
    gfx_text(&font_ui_small, sx + 18, sy + 16, "LOCATIONS", settings_muted());

    const char *labels[5] = { "Home", "Documents", "Web", "System", "Apps" };
    for (int i = 0; i < 5; i++) {
        int iy = sy + 42 + i * 38;
        int ix = sx + 10;
        bool active = (i == 0 && fs.depth == 0) ||
                      (i == 1 && kstricmp(fs.path, "/DOCS") == 0) ||
                      (i == 2 && kstricmp(fs.path, "/WEB") == 0) ||
                      (i == 3 && kstricmp(fs.path, "/SYSTEM") == 0) ||
                      (i == 4 && kstricmp(fs.path, "/APPS") == 0);
        bool hover = ui_point_in(ix, iy, l->sidebar_w - 20, 32, mouse.x, mouse.y);
        if (active || hover) {
            gfx_rounded_fill(ix, iy, l->sidebar_w - 20, 32, 7,
                             active ? settings_accent_soft() : settings_panel());
        }
        icon_folder(ix + 4, iy + 3, 18, 0x9DCBF4, settings_strong());
        gfx_text(&font_ui, ix + 32, iy + 6, labels[i],
                 active ? settings_accent() : settings_ink());
    }

    int footer_y = sy + sh - 54;
    gfx_hline(sx + 16, sx + l->sidebar_w - 16, footer_y, settings_line());
    gfx_text(&font_ui_small, sx + 18, footer_y + 12, "FAT16 volume", settings_muted());
    if (fat16_mounted()) {
        char info[48];
        const fat_volume_t *vol = fat16_volume();
        snprintf(info, sizeof(info), "%u KB clusters", vol->spc_bytes / 1024);
        gfx_text(&font_ui_small, sx + 18, footer_y + 27, info, settings_muted());
    }
}

static void draw_list(gui_window_t *window, files_layout_t *l) {
    UNUSED(window);
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    int header_y = l->y0 + l->toolbar_h + 15;
    gfx_text(&font_ui_small, l->list_x, header_y, "NAME", settings_muted());
    gfx_text(&font_ui_small, l->list_x + l->list_w - 74, header_y, "SIZE", settings_muted());
    gfx_hline(l->list_x, l->list_x + l->list_w - 1, header_y + 20, settings_line());

    int visible = (l->list_bottom - l->list_y) / l->row_h;
    if (visible < 1) {
        visible = 1;
    }
    for (int i = 0; i < visible; i++) {
        int index = fs.scroll + i;
        if (index >= fs.count) {
            break;
        }
        fat_entry_t *entry = &fs.entries[index];
        int ry = l->list_y + i * l->row_h;
        bool hover = ui_point_in(l->list_x, ry, l->list_w, l->row_h - 2, mouse.x, mouse.y);
        if (index == fs.selected) {
            gfx_rounded_fill(l->list_x - 4, ry - 1, l->list_w + 8, l->row_h - 1, 7,
                             settings_accent_soft());
        } else if (hover) {
            gfx_rounded_fill(l->list_x - 4, ry - 1, l->list_w + 8, l->row_h - 1, 7,
                             settings_panel_alt());
        }
        if (entry->attr & FAT_ATTR_DIRECTORY) {
            icon_folder(l->list_x + 5, ry + 2, 20, 0x9DCBF4, settings_strong());
        } else {
            icon_document(l->list_x + 5, ry + 2, 20, settings_panel(),
                          settings_strong(), settings_accent());
        }
        uint32_t color = (index == fs.selected) ? settings_accent() : settings_ink();
        gfx_text_clip(&font_ui, l->list_x + 34, ry + 3, entry->display,
                      l->list_w - 120, color);
        char size_text[32];
        if (entry->attr & FAT_ATTR_DIRECTORY) {
            kstrcpy(size_text, "Folder");
        } else {
            ui_format_size(entry->size, size_text, sizeof(size_text));
        }
        int tw = gfx_text_width(&font_ui_small, size_text);
        gfx_text(&font_ui_small, l->list_x + l->list_w - tw, ry + 6, size_text, settings_muted());
    }

    if (fs.count == 0) {
        gfx_text_center(&font_ui, l->list_x + l->list_w / 2, l->list_y + 60,
                        "This folder is empty", settings_muted());
    }

    int status_y = l->y0 + l->h - 38;
    gfx_hline(l->x0, l->x0 + l->w - 1, status_y, settings_line());
    char status[128];
    if (fs.status[0] && (pit_ticks() - fs.status_tick) < 300) {
        kstrncpy(status, fs.status, sizeof(status) - 1);
        status[sizeof(status) - 1] = 0;
    } else {
        snprintf(status, sizeof(status), "%d items  |  %u clusters free  |  %s",
                 fs.count, fat16_free_clusters(), fs.path);
    }
    gfx_text_clip(&font_ui_small, l->x0 + 16, status_y + 11, status, l->w - 32, settings_muted());
}

static void prompt_layout(files_layout_t *l, int *mx, int *my, int *mw, int *mh,
                          int *field_x, int *field_y, int *field_w, int *field_h,
                          int *cancel_x, int *action_x, int *button_y) {
    *mw = MIN(390, l->w - 80);
    *mh = 192;
    *mx = l->x0 + (l->w - *mw) / 2;
    *my = l->y0 + (l->h - *mh) / 2;
    *field_x = *mx + 24;
    *field_y = *my + 76;
    *field_w = *mw - 48;
    *field_h = 38;
    *button_y = *my + *mh - 48;
    *cancel_x = *mx + *mw - 24 - 86 - 8 - 76;
    *action_x = *mx + *mw - 24 - 86;
}

static void draw_prompt(gui_window_t *window, files_layout_t *l) {
    UNUSED(window);
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    int mx, my, mw, mh, fx, fy, fw, fh, cancel_x, action_x, button_y;
    prompt_layout(l, &mx, &my, &mw, &mh, &fx, &fy, &fw, &fh, &cancel_x, &action_x, &button_y);
    gfx_rounded_fill(mx + 4, my + 6, mw, mh, 12, settings_shadow());
    gfx_rounded_fill(mx, my, mw, mh, 12, settings_panel());
    gfx_rounded_border(mx, my, mw, mh, 12, 1, settings_strong());

    const char *title = fs.prompt == PROMPT_FOLDER ? "New folder" :
                        fs.prompt == PROMPT_FILE ? "New file" :
                        fs.prompt == PROMPT_RENAME ? "Rename item" : "Delete item";
    gfx_text(&font_title, mx + 24, my + 22, title, settings_ink());

    if (fs.prompt == PROMPT_DELETE) {
        char message[128];
        if (fs.selected >= 0 && fs.selected < fs.count) {
            snprintf(message, sizeof(message), "Delete \"%s\"? This cannot be undone.",
                     fs.entries[fs.selected].display);
        } else {
            kstrcpy(message, "Nothing is selected.");
        }
        gfx_text_clip(&font_ui, mx + 24, my + 88, message, mw - 48, settings_muted());
    } else {
        gfx_text(&font_ui_small, mx + 24, my + 60,
                 fs.prompt == PROMPT_RENAME ? "NEW NAME" : "NAME", settings_muted());
        ui_field(fx, fy, fw, fh, fs.prompt_text, true,
                 fs.prompt_text[0] == 0);
    }

    bool hover_cancel = ui_point_in(cancel_x, button_y, 76, 32, mouse.x, mouse.y);
    bool hover_action = ui_point_in(action_x, button_y, 86, 32, mouse.x, mouse.y);
    ui_small_button(cancel_x, button_y, 76, 32, "Cancel", hover_cancel);
    ui_button(action_x, button_y, 86, 32,
              fs.prompt == PROMPT_DELETE ? "Delete" :
              fs.prompt == PROMPT_RENAME ? "Rename" : "Create", true, hover_action);
}

static void draw_viewer(gui_window_t *window, files_layout_t *l) {
    UNUSED(window);
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    int mx = l->x0 + 28;
    int my = l->y0 + 22;
    int mw = l->w - 56;
    int mh = l->h - 44;
    gfx_rounded_fill(mx + 4, my + 6, mw, mh, 12, settings_shadow());
    gfx_rounded_fill(mx, my, mw, mh, 12, settings_panel());
    gfx_rounded_border(mx, my, mw, mh, 12, 1, settings_strong());
    gfx_text_clip(&font_title, mx + 22, my + 18, fs.viewer_name, mw - 100,
                  settings_ink());
    int close_x = mx + mw - 36;
    int close_y = my + 18;
    bool hover_close = ui_point_in(close_x, close_y, 24, 24, mouse.x, mouse.y);
    if (hover_close) {
        gfx_rounded_fill(close_x, close_y, 24, 24, 6, settings_panel_alt());
    }
    gfx_line(close_x + 7, close_y + 7, close_x + 17, close_y + 17, settings_ink());
    gfx_line(close_x + 17, close_y + 7, close_x + 7, close_y + 17, settings_ink());
    gfx_hline(mx + 22, mx + mw - 22, my + 56, settings_line());

    if (!fs.viewer_text[0]) {
        gfx_text(&font_ui, mx + 24, my + 76, "(empty file)", settings_muted());
    } else {
        draw_wrapped_text(fs.viewer_text, mx + 24, my + 72, mw - 48, mh - 120,
                          fs.viewer_scroll, &font_ui_small, settings_ink());
    }
    gfx_text(&font_ui_small, mx + 22, my + mh - 30, "Esc to close  |  Up/Down to scroll",
             settings_muted());
}

static void files_draw(gui_window_t *window) {
    files_layout_t l;
    layout(window, &l);
    draw_toolbar(window, &l);
    draw_sidebar(window, &l);
    draw_list(window, &l);
    if (fs.prompt != PROMPT_NONE) {
        draw_prompt(window, &l);
    }
    if (fs.viewing) {
        draw_viewer(window, &l);
    }
}

static void files_sidebar_click(int x, int y, files_layout_t *l) {
    int sy = l->y0 + l->toolbar_h;
    for (int i = 0; i < 4; i++) {
        int iy = sy + 42 + i * 38;
        if (ui_point_in(l->x0 + 10, iy, l->sidebar_w - 20, 32, x, y)) {
            if (i == 0) {
                files_set_root();
            } else if (i == 1) {
                if (!files_go_named_in_root("DOCS")) {
                    set_status("Documents folder missing");
                }
            } else if (i == 2) {
                if (!files_go_named_in_root("WEB")) {
                    set_status("Web folder missing");
                }
            } else if (i == 3) {
                if (!files_go_named_in_root("SYSTEM")) {
                    set_status("System folder missing");
                }
            } else if (i == 4) {
                if (!files_go_named_in_root("APPS")) {
                    set_status("Apps folder missing");
                }
            }
            return;
        }
    }
}

static void files_prompt_click(int x, int y, files_layout_t *l) {
    int mx, my, mw, mh, fx, fy, fw, fh, cancel_x, action_x, button_y;
    prompt_layout(l, &mx, &my, &mw, &mh, &fx, &fy, &fw, &fh, &cancel_x, &action_x, &button_y);
    if (ui_point_in(cancel_x, button_y, 76, 32, x, y)) {
        prompt_reset();
        return;
    }
    if (ui_point_in(action_x, button_y, 86, 32, x, y)) {
        confirm_prompt();
        return;
    }
    if ((fs.prompt == PROMPT_FOLDER || fs.prompt == PROMPT_FILE ||
         fs.prompt == PROMPT_RENAME) &&
        ui_point_in(fx, fy, fw, fh, x, y)) {
        return;
    }
    UNUSED(mx);
    UNUSED(my);
    UNUSED(mw);
    UNUSED(mh);
}

static void files_mouse_down(gui_window_t *window, int x, int y) {
    files_layout_t l;
    layout(window, &l);
    if (fs.prompt != PROMPT_NONE) {
        files_prompt_click(x, y, &l);
        return;
    }
    if (fs.viewing) {
        int mx = l.x0 + 28;
        int my = l.y0 + 22;
        int mw = l.w - 56;
        if (ui_point_in(mx + mw - 36, my + 18, 24, 24, x, y)) {
            fs.viewing = false;
        }
        return;
    }

    if (ui_point_in(l.up_x, l.up_y, l.up_w, l.action_h, x, y)) {
        files_up();
        return;
    }
    if (ui_point_in(l.folder_x, l.action_y, 84, l.action_h, x, y)) {
        open_prompt(PROMPT_FOLDER);
        return;
    }
    if (ui_point_in(l.file_x, l.action_y, 68, l.action_h, x, y)) {
        open_prompt(PROMPT_FILE);
        return;
    }
    if (ui_point_in(l.rename_x, l.action_y, 72, l.action_h, x, y)) {
        open_rename_prompt();
        return;
    }
    if (ui_point_in(l.delete_x, l.action_y, 58, l.action_h, x, y)) {
        if (fs.selected >= 0 && fs.selected < fs.count) {
            open_prompt(PROMPT_DELETE);
        } else {
            set_status("Nothing is selected");
        }
        return;
    }
    if (x < l.x0 + l.sidebar_w && y >= l.y0 + l.toolbar_h) {
        files_sidebar_click(x, y, &l);
        return;
    }

    int visible = (l.list_bottom - l.list_y) / l.row_h;
    for (int i = 0; i < visible; i++) {
        int index = fs.scroll + i;
        if (index >= fs.count) {
            break;
        }
        int ry = l.list_y + i * l.row_h;
        if (ui_point_in(l.list_x - 4, ry - 1, l.list_w + 8, l.row_h - 1, x, y)) {
            uint32_t now = pit_ticks();
            bool double_click = (index == fs.last_click_index) &&
                                (now - fs.last_click_tick < 44);
            fs.selected = index;
            fs.last_click_index = index;
            fs.last_click_tick = now;
            if (double_click) {
                files_open_selected();
            }
            return;
        }
    }
}

static void files_mouse_up(gui_window_t *window, int x, int y) {
    UNUSED(window);
    UNUSED(x);
    UNUSED(y);
}

static void files_key(gui_window_t *window, const key_event_t *event) {
    UNUSED(window);
    if (fs.prompt != PROMPT_NONE) {
        if (fs.prompt == PROMPT_FOLDER || fs.prompt == PROMPT_FILE ||
            fs.prompt == PROMPT_RENAME) {
            if (event->ch >= 32 && event->ch < 127) {
                size_t len = kstrlen(fs.prompt_text);
                if (len + 1 < sizeof(fs.prompt_text)) {
                    fs.prompt_text[len] = event->ch;
                    fs.prompt_text[len + 1] = 0;
                }
            } else if (event->code == KEY_BACKSPACE) {
                size_t len = kstrlen(fs.prompt_text);
                if (len) {
                    fs.prompt_text[len - 1] = 0;
                }
            }
        }
        if (event->code == KEY_ENTER) {
            confirm_prompt();
        } else if (event->code == KEY_ESC) {
            prompt_reset();
        }
        return;
    }

    if (fs.viewing) {
        if (event->code == KEY_ESC || event->code == KEY_ENTER) {
            fs.viewing = false;
        } else if (event->code == KEY_DOWN) {
            fs.viewer_scroll += 2;
        } else if (event->code == KEY_UP) {
            fs.viewer_scroll -= 2;
            if (fs.viewer_scroll < 0) {
                fs.viewer_scroll = 0;
            }
        }
        return;
    }

    if (event->code == KEY_UP) {
        if (fs.selected > 0) {
            fs.selected--;
            files_refresh();
        }
    } else if (event->code == KEY_DOWN) {
        if (fs.selected + 1 < fs.count) {
            fs.selected++;
            files_refresh();
        }
    } else if (event->code == KEY_ENTER) {
        files_open_selected();
    } else if (event->code == KEY_BACKSPACE) {
        files_up();
    } else if (event->code == KEY_DELETE) {
        if (fs.selected >= 0 && fs.selected < fs.count) {
            open_prompt(PROMPT_DELETE);
        }
    }
}

static void files_init(gui_window_t *window) {
    kmemset(&fs, 0, sizeof(fs));
    fs.window = window;
    files_set_root();
}

const gui_app_t files_app = {
    .title = "Files",
    .width = 730,
    .height = 530,
    .init = files_init,
    .draw = files_draw,
    .mouse_down = files_mouse_down,
    .mouse_up = files_mouse_up,
    .key = files_key,
    .tick = NULLPTR,
};
