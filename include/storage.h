#ifndef STORAGE_H
#define STORAGE_H

#include "library.h"

void storage_get_dir(char *buf, int len);
void storage_load(Library *lib);
void storage_save(const Library *lib);
void storage_ensure_dir(void);

/* Per-file progress helpers */
void storage_save_progress(const LibraryItem *item);

#endif
