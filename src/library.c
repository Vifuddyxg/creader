#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "library.h"
#include "pdf.h"

void library_init(Library *lib)
{
    memset(lib, 0, sizeof(*lib));
    lib->selected = -1;
}

void library_free_thumbs(Library *lib)
{
    for (int i = 0; i < MAX_LIBRARY; i++) {
        if (lib->thumbs[i]) {
            SDL_DestroyTexture(lib->thumbs[i]);
            lib->thumbs[i] = NULL;
        }
    }
    lib->thumbs_loaded = false;
}

int library_add(Library *lib, const LibraryItem *item)
{
    int idx = library_find(lib, item->path);
    if (idx >= 0) {
        /* preserve progress fields, update metadata */
        LibraryItem *existing = &lib->items[idx];
        /* only overwrite category if the new item has one */
        if (item->category[0])
            snprintf(existing->category, sizeof(existing->category),
                     "%s", item->category);
        snprintf(existing->title, sizeof(existing->title), "%s", item->title);
        if (item->page_count > 0) existing->page_count = item->page_count;
        return idx;
    }
    if (lib->count >= MAX_LIBRARY) return -1;
    lib->items[lib->count] = *item;
    lib->count++;
    return lib->count - 1;
}

void library_remove(Library *lib, int idx)
{
    if (idx < 0 || idx >= lib->count) return;
    if (lib->thumbs[idx]) SDL_DestroyTexture(lib->thumbs[idx]);
    for (int i = idx; i < lib->count - 1; i++) {
        lib->items[i]  = lib->items[i + 1];
        lib->thumbs[i] = lib->thumbs[i + 1];
    }
    lib->count--;
    lib->thumbs[lib->count] = NULL;
    memset(&lib->items[lib->count], 0, sizeof(LibraryItem));
    if (lib->selected >= lib->count) lib->selected = lib->count - 1;
}

int library_find(const Library *lib, const char *path)
{
    for (int i = 0; i < lib->count; i++)
        if (strcmp(lib->items[i].path, path) == 0) return i;
    return -1;
}

void library_update(Library *lib, int idx, const LibraryItem *item)
{
    if (idx < 0 || idx >= lib->count) return;
    char category[MAX_TITLE_LEN];
    snprintf(category, sizeof(category), "%s", lib->items[idx].category);
    lib->items[idx] = *item;
    if (lib->items[idx].category[0] == '\0' && category[0] != '\0')
        snprintf(lib->items[idx].category, sizeof(lib->items[idx].category),
                 "%s", category);
}

void library_load_thumbs(Library *lib, SDL_Renderer *r, int thumb_w, int thumb_h)
{
    if (lib->thumbs_loaded) return;
    for (int i = 0; i < lib->count; i++) {
        if (!lib->thumbs[i])
            lib->thumbs[i] = pdf_render_thumbnail(lib->items[i].path, r,
                                                   thumb_w, thumb_h);
    }
    lib->thumbs_loaded = true;
}

int library_filter(const Library *lib, const char *query,
                   int *indices, int max_indices)
{
    int count = 0;
    for (int i = 0; i < lib->count && count < max_indices; i++) {
        if (strcasestr(lib->items[i].title,    query) != NULL ||
            strcasestr(lib->items[i].path,     query) != NULL ||
            strcasestr(lib->items[i].category, query) != NULL) {
            if (indices) indices[count] = i;
            count++;
        }
    }
    return count;
}

/* ---- sorted display order (by category, then title) -------------------- */

static int cmp_item(const void *a, const void *b, void *lib_ptr)
{
    const Library    *lib = (const Library *)lib_ptr;
    const LibraryItem *ia = &lib->items[*(const int *)a];
    const LibraryItem *ib = &lib->items[*(const int *)b];
    int ccat = strcasecmp(ia->category, ib->category);
    if (ccat != 0) return ccat;
    return strcasecmp(ia->title, ib->title);
}

void library_sorted_indices(const Library *lib, int *out_sorted)
{
    for (int i = 0; i < lib->count; i++) out_sorted[i] = i;
    library_sort_indices(lib, out_sorted, lib->count);
}

void library_sort_indices(const Library *lib, int *indices, int count)
{
    /* qsort_r is available on Linux with _GNU_SOURCE */
    qsort_r(indices, count, sizeof(int), cmp_item, (void *)lib);
}

/* ---- directory scanner ------------------------------------------------- */

static bool is_readable_ext(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".pdf")  == 0 ||
            strcasecmp(ext, ".cbz")  == 0 ||
            strcasecmp(ext, ".cbr")  == 0 ||
            strcasecmp(ext, ".epub") == 0);
}

static int scan_dir(Library *lib, const char *dir_path,
                    const char *category, int depth, int max_depth)
{
    if (depth > max_depth) return 0;
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int added = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            added += scan_dir(lib, full, category, depth + 1, max_depth);
        } else if (S_ISREG(st.st_mode) && is_readable_ext(entry->d_name)) {
            if (library_find(lib, full) >= 0) continue;

            LibraryItem item;
            memset(&item, 0, sizeof(item));
            snprintf(item.path,     sizeof(item.path),     "%s", full);
            snprintf(item.category, sizeof(item.category), "%s", category);
            item.zoom = 1.0f;

            /* title = filename without extension */
            size_t nlen = strlen(entry->d_name);
            const char *ext = strrchr(entry->d_name, '.');
            size_t tlen = ext ? (size_t)(ext - entry->d_name) : nlen;
            if (tlen >= sizeof(item.title)) tlen = sizeof(item.title) - 1;
            memcpy(item.title, entry->d_name, tlen);
            item.title[tlen] = '\0';

            library_add(lib, &item);
            added++;
        }
    }
    closedir(dir);
    return added;
}

int library_add_directory(Library *lib, const char *dir_path)
{
    /* category = basename of the directory */
    const char *base = strrchr(dir_path, '/');
    base = (base && base[1]) ? base + 1 : dir_path;

    return scan_dir(lib, dir_path, base, 0, 3);
}
