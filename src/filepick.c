#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "filepick.h"
#include "state.h"

/* ---- tool detection ------------------------------------------------------ */

typedef enum { TOOL_NONE, TOOL_ZENITY, TOOL_KDIALOG, TOOL_YAD } PickerTool;

static PickerTool detect_tool(void)
{
    static PickerTool cached = (PickerTool)-1;
    if ((int)cached != -1) return cached;

    const struct { const char *name; PickerTool tool; } candidates[] = {
        { "zenity",  TOOL_ZENITY  },
        { "kdialog", TOOL_KDIALOG },
        { "yad",     TOOL_YAD     },
    };

    for (int i = 0; i < 3; i++) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "which %s >/dev/null 2>&1",
                 candidates[i].name);
        if (system(cmd) == 0) {
            cached = candidates[i].tool;
            return cached;
        }
    }
    cached = TOOL_NONE;
    return TOOL_NONE;
}

bool filepick_has_tool(void)
{
    return detect_tool() != TOOL_NONE;
}

/* ---- run a dialog command and capture first line of stdout --------------- */

static bool run_dialog(const char *cmd, char *buf, int len)
{
    FILE *f = popen(cmd, "r");
    if (!f) return false;
    bool ok = (fgets(buf, len, f) != NULL);
    pclose(f);
    if (!ok) return false;

    /* strip trailing newline / carriage return */
    int n = (int)strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
        buf[--n] = '\0';

    return n > 0;
}

/* ---- file picker --------------------------------------------------------- */

bool filepick_file(char *buf, int len)
{
    char cmd[1024];
    switch (detect_tool()) {
    case TOOL_ZENITY:
        snprintf(cmd, sizeof(cmd),
            "zenity --file-selection"
            " --title='Open PDF or Comic'"
            " --file-filter='PDF / Comic (*.pdf *.cbz *.cbr) | *.pdf *.cbz *.cbr'"
            " --file-filter='All files | *'"
            " 2>/dev/null");
        break;
    case TOOL_KDIALOG:
        snprintf(cmd, sizeof(cmd),
            "kdialog --getopenfilename ."
            " '*.pdf *.cbz *.cbr|PDF / Comic files'"
            " --title 'Open PDF or Comic'"
            " 2>/dev/null");
        break;
    case TOOL_YAD:
        snprintf(cmd, sizeof(cmd),
            "yad --file-selection"
            " --title='Open PDF or Comic'"
            " --file-filter='*.pdf;*.cbz;*.cbr'"
            " 2>/dev/null");
        break;
    default:
        return false;
    }
    return run_dialog(cmd, buf, len);
}

/* ---- folder picker ------------------------------------------------------- */

bool filepick_folder(char *buf, int len)
{
    char cmd[512];
    switch (detect_tool()) {
    case TOOL_ZENITY:
        snprintf(cmd, sizeof(cmd),
            "zenity --file-selection --directory"
            " --title='Select Folder to Scan'"
            " 2>/dev/null");
        break;
    case TOOL_KDIALOG:
        snprintf(cmd, sizeof(cmd),
            "kdialog --getexistingdirectory ."
            " --title 'Select Folder to Scan'"
            " 2>/dev/null");
        break;
    case TOOL_YAD:
        snprintf(cmd, sizeof(cmd),
            "yad --file-selection --directory"
            " --title='Select Folder to Scan'"
            " 2>/dev/null");
        break;
    default:
        return false;
    }
    return run_dialog(cmd, buf, len);
}

/* ---- folder scanner ------------------------------------------------------ */

static bool is_readable_file(const char *ext)
{
    return (strcasecmp(ext, ".pdf")  == 0 ||
            strcasecmp(ext, ".cbz")  == 0 ||
            strcasecmp(ext, ".cbr")  == 0 ||
            strcasecmp(ext, ".epub") == 0);
}

static void path_to_title(const char *path, char *title, int len)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t blen = strlen(base);
    size_t copy = blen < (size_t)(len - 1) ? blen : (size_t)(len - 1);
    memcpy(title, base, copy);
    title[copy] = '\0';
    char *dot = strrchr(title, '.');
    if (dot) *dot = '\0';
}

/* Recursive scanner — depth 0 = the folder itself. */
static int scan_dir(const char *dir_path, Library *lib,
                    int depth, int max_depth)
{
    if (depth > max_depth) return 0;

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int added = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* skip hidden entries */
        if (entry->d_name[0] == '.') continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* recurse */
            added += scan_dir(full, lib, depth + 1, max_depth);

        } else if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext || !is_readable_file(ext)) continue;

            /* skip if already in library */
            if (library_find(lib, full) >= 0) continue;

            LibraryItem item;
            memset(&item, 0, sizeof(item));
            snprintf(item.path,  sizeof(item.path),  "%s", full);
            path_to_title(full, item.title, sizeof(item.title));
            item.zoom = 1.0f;
            library_add(lib, &item);
            added++;
        }
    }

    closedir(dir);
    return added;
}

int filepick_scan_folder(const char *folder_path, Library *lib, int max_depth)
{
    return scan_dir(folder_path, lib, 0, max_depth);
}
