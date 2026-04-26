#ifndef FILEPICK_H
#define FILEPICK_H

#include <stdbool.h>
#include "library.h"

/* Open a native file-picker dialog (zenity / kdialog / yad).
   Returns true and fills buf if the user selected a file.
   Returns false if cancelled or no dialog tool is available. */
bool filepick_file(char *buf, int len);

/* Open a native folder-picker dialog.
   Returns true and fills buf if the user selected a directory. */
bool filepick_folder(char *buf, int len);

/* Scan a directory tree (up to max_depth levels deep) for PDF/CBZ/CBR files
   and add new ones to lib.  Returns the number of new items added. */
int  filepick_scan_folder(const char *folder_path, Library *lib, int max_depth);

/* Returns true if at least one dialog tool (zenity/kdialog/yad) is available. */
bool filepick_has_tool(void);

#endif
