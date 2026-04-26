#ifndef LIBRARY_H
#define LIBRARY_H

#include <SDL2/SDL.h>
#include "state.h"

typedef struct {
    LibraryItem  items[MAX_LIBRARY];
    int          count;
    SDL_Texture *thumbs[MAX_LIBRARY];
    int          selected;
    int          scroll_y;
    bool         thumbs_loaded;
    /* text input for adding a file */
    bool         input_active;
    bool         input_dir_mode;
    char         input_buf[MAX_PATH_LEN];
    int          input_len;
    /* search */
    bool         search_active;
    char         search_buf[MAX_TITLE_LEN];
    int          search_len;
} Library;

void library_init(Library *lib);
void library_free_thumbs(Library *lib);

int  library_add(Library *lib, const LibraryItem *item);
void library_remove(Library *lib, int idx);
int  library_find(const Library *lib, const char *path);
void library_update(Library *lib, int idx, const LibraryItem *item);

void library_load_thumbs(Library *lib, SDL_Renderer *r, int thumb_w, int thumb_h);

/* Returns filtered count and fills indices[]. indices may be NULL to just count. */
int  library_filter(const Library *lib, const char *query,
                    int *indices, int max_indices);

/* Scan a directory for PDF/CBZ/CBR files (recursive, max 3 levels).
   Category is set to the top-level dir basename.
   Returns number of new items added. */
int  library_add_directory(Library *lib, const char *dir_path);

/* Build a display-order index sorted by category then title.
   out_sorted must be at least lib->count elements. */
void library_sorted_indices(const Library *lib, int *out_sorted);

/* Sort an existing index list by category then title. */
void library_sort_indices(const Library *lib, int *indices, int count);

#endif
