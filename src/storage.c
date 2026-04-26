#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "storage.h"
#include "library.h"

void storage_get_dir(char *buf, int len)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, len, "%s/.local/share/creader", home);
}

static void storage_get_legacy_dir(char *buf, int len)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, len, "%s/.local/share/nvreader", home);
}

void storage_ensure_dir(void)
{
    char dir[MAX_PATH_LEN];
    storage_get_dir(dir, sizeof(dir));

    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", dir);

    /* mkdir -p equivalent: create each component */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    char new_path[MAX_PATH_LEN];
    char new_dir[MAX_PATH_LEN];
    char old_dir[MAX_PATH_LEN];
    char old_path[MAX_PATH_LEN];
    storage_get_dir(new_dir, sizeof(new_dir));
    snprintf(new_path, sizeof(new_path), "%.4000s/library.dat", new_dir);
    storage_get_legacy_dir(old_dir, sizeof(old_dir));
    snprintf(old_path, sizeof(old_path), "%.4000s/library.dat", old_dir);

    if (access(new_path, F_OK) != 0 && access(old_path, F_OK) == 0) {
        FILE *src = fopen(old_path, "rb");
        FILE *dst = fopen(new_path, "wb");
        if (src && dst) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                fwrite(buf, 1, n, dst);
        }
        if (src) fclose(src);
        if (dst) fclose(dst);
    }
}

static char *get_library_path(char *buf, int len)
{
    char dir[MAX_PATH_LEN - 16];
    storage_get_dir(dir, sizeof(dir));
    snprintf(buf, len, "%.4000s/library.dat", dir);
    return buf;
}

void storage_load(Library *lib)
{
    char path[MAX_PATH_LEN];
    get_library_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_PATH_LEN + 64];
    LibraryItem item;
    memset(&item, 0, sizeof(item));
    bool in_item = false;

    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (strcmp(line, "[item]") == 0) {
            if (in_item && item.path[0]) {
                library_add(lib, &item);
            }
            memset(&item, 0, sizeof(item));
            item.zoom = 1.0f;
            in_item = true;
            continue;
        }

        if (!in_item) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

#define FIELD_STR(name, member) \
    if (strcmp(key, name) == 0) { strncpy(item.member, val, sizeof(item.member)-1); }
#define FIELD_INT(name, member) \
    if (strcmp(key, name) == 0) { item.member = atoi(val); }
#define FIELD_FLT(name, member) \
    if (strcmp(key, name) == 0) { item.member = (float)atof(val); }

        FIELD_STR("path",        path)
        FIELD_STR("title",       title)
        FIELD_STR("category",    category)
        FIELD_INT("last_page",   last_page)
        FIELD_INT("page_count",  page_count)
        FIELD_FLT("zoom",        zoom)
        FIELD_INT("rotation",    rotation)
        FIELD_INT("scroll_y",    scroll_y)
        FIELD_INT("view_mode",   view_mode)
        FIELD_STR("last_opened", last_opened)
#undef FIELD_STR
#undef FIELD_INT
#undef FIELD_FLT
    }

    /* save last item */
    if (in_item && item.path[0]) {
        library_add(lib, &item);
    }

    fclose(f);
}

void storage_save(const Library *lib)
{
    storage_ensure_dir();
    char path[MAX_PATH_LEN];
    get_library_path(path, sizeof(path));

    /* write to temp file, then rename for atomicity */
    char tmp_path[MAX_PATH_LEN + 8];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        fprintf(stderr, "storage_save: cannot open %s: %s\n", tmp_path, strerror(errno));
        return;
    }

    for (int i = 0; i < lib->count; i++) {
        const LibraryItem *it = &lib->items[i];
        fprintf(f, "[item]\n");
        fprintf(f, "path=%s\n",        it->path);
        fprintf(f, "title=%s\n",       it->title);
        fprintf(f, "category=%s\n",    it->category);
        fprintf(f, "last_page=%d\n",   it->last_page);
        fprintf(f, "page_count=%d\n",  it->page_count);
        fprintf(f, "zoom=%f\n",        (double)it->zoom);
        fprintf(f, "rotation=%d\n",    it->rotation);
        fprintf(f, "scroll_y=%d\n",    it->scroll_y);
        fprintf(f, "view_mode=%d\n",   (int)it->view_mode);
        fprintf(f, "last_opened=%s\n", it->last_opened);
        fprintf(f, "\n");
    }

    fclose(f);
    rename(tmp_path, path);
}

void storage_save_progress(const LibraryItem *item)
{
    (void)item;
    /* Progress is always saved via the full storage_save(). */
    /* This stub exists so callers can request immediate flush if needed. */
}
