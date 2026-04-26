# creader

Small Linux-first PDF/comic reader written in C with SDL2 and MuPDF.

creader is experimental but usable for daily reading. It focuses on a fast
library view, keyboard navigation, continuous scrolling, and a compact reader UI.

## Features

- Dark modern UI with card-based library view
- Continuous single-page scrolling and two-page book mode
- Zoom in/out, page rotation, page slider
- Auto-saves reading progress per file
- Library with thumbnails, categories, search, add/remove files
- Resizable window, fullscreen support

## Dependencies

| Library      | Package (Arch)          | Package (Debian/Ubuntu)       |
|-------------|-------------------------|-------------------------------|
| SDL2        | `sdl2`                  | `libsdl2-dev`                 |
| SDL2_ttf    | `sdl2_ttf`              | `libsdl2-ttf-dev`             |
| MuPDF       | `mupdf-gl` / `mupdf`    | `libmupdf-dev`                |

### Arch Linux
```bash
sudo pacman -S sdl2 sdl2_ttf mupdf-gl
```

### Debian / Ubuntu
```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libmupdf-dev
```

### Gentoo
```bash
sudo emerge media-libs/libsdl2 media-libs/sdl2-ttf app-text/mupdf
```

> **Note on fonts:** creader searches for DejaVu Sans, Noto Sans, or Liberation Sans in
> standard system font paths. Install any of them for text UI. On most distros they are
> already present (`ttf-dejavu` / `fonts-dejavu`).

## Build

```bash
cd creader
make
```

The binary is placed at `./creader`.

```bash
sudo make install     # installs to /usr/local/bin/creader
make clean            # remove build artefacts
```

## Add to Applications Menu

To open creader like Firefox or other desktop apps, install the binary and add a
desktop launcher.

```bash
make
sudo make install
make install-menu
```

This installs the binary to `/usr/local/bin/creader`, then creates
`~/.local/share/applications/creader.desktop`, refreshes the applications menu,
and sets creader as the default PDF app when `xdg-mime` is available.

Do not run `make install-menu` with `sudo`; it should install the launcher for
your normal user so tools like `rofi -show drun` can find it.

To remove only the desktop launcher:

```bash
make uninstall-desktop
```

Manual desktop file, if needed:

```ini
[Desktop Entry]
Type=Application
Name=creader
Comment=PDF and comic reader
Exec=/usr/local/bin/creader %f
Terminal=false
Categories=Office;Viewer;Graphics;
MimeType=application/pdf;application/epub+zip;application/vnd.comicbook+zip;application/vnd.comicbook-rar;
StartupNotify=true
```

## Usage

```bash
creader                    # open library view
creader book.pdf           # open a specific PDF
```

### Keyboard shortcuts

#### Reader
| Key              | Action                          |
|------------------|---------------------------------|
| `→` / `PageDown` | Next page (or +2 in two-page)   |
| `←` / `PageUp`   | Previous page                   |
| `↑` / `↓`        | Scroll up / down                |
| `+` / `=`        | Zoom in                         |
| `-`              | Zoom out                        |
| `0`              | Reset zoom to 100%              |
| `r`              | Rotate page 90°                 |
| `d`              | Toggle single / two-page mode   |
| `f`              | Toggle fullscreen               |
| `b`              | Open library                    |
| `q`              | Quit                            |

The reader also has a left sidebar for zoom and view mode, plus a right sidebar
for page navigation and returning to the library.

#### Library
| Key        | Action                            |
|------------|-----------------------------------|
| `Enter`    | Open selected item                |
| `Del`      | Remove from library (file kept)   |
| `a`        | Add PDF (type path + Enter)       |
| `d`        | Add folder by path or name        |
| `/`        | Search by title or path           |
| `← → ↑ ↓` | Navigate cards                    |
| `b` / `Esc`| Back to reader                    |
| `q`        | Quit                              |

Double-click a card to open it directly.

## Data storage

Progress and library data are stored at:

```
~/.local/share/creader/library.dat
```

Plain-text INI-like format, easy to inspect or backup.

On first run, creader migrates an existing
`~/.local/share/nvreader/library.dat` file to the new location if needed.

## Known Limitations

- Linux only.
- No annotations, forms, or editing support.
- Continuous scrolling caches nearby pages, but very large or image-heavy PDFs
  can still take CPU while new pages render.
- EPUB/CBZ/CBR support depends on the local MuPDF build and installed libraries.
- No packaged releases yet; build from source with `make`.

## Project structure

```
creader/
├── src/
│   ├── main.c       – entry point, main loop
│   ├── state.c      – AppState init
│   ├── pdf.c        – MuPDF wrapper
│   ├── ui.c         – all SDL2 rendering
│   ├── input.c      – event handling
│   ├── library.c    – in-memory library management
│   └── storage.c    – load/save library.dat
├── include/         – header files
├── assets/          – (reserved for future icons/fonts)
└── Makefile
```
