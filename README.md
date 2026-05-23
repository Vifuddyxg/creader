# creader

`creader` is a small PDF, comic, and document reader for Unix-like systems,
written in C with SDL2 and MuPDF. It opens into a visual library, remembers
reading progress, and keeps the reader interface compact for keyboard and mouse
use.

Supported formats depend on your local MuPDF build, but typically include PDF,
EPUB, CBZ, and CBR.

## Features

- Dark SDL2 interface with a card-based library
- Continuous single-page scrolling with nearby-page caching
- Two-page book mode
- Zoom, rotation, page slider, fullscreen mode
- File and folder import from the library
- Thumbnail generation for library items
- Search and keyboard navigation
- Reading progress saved per document
- Desktop launcher for application menus and `drun`

## Dependencies

- C compiler
- `make` on Linux, `gmake` on FreeBSD/OpenBSD
- `pkg-config` / `pkgconf`
- SDL2
- SDL2_ttf
- MuPDF development headers and library
- `zenity` or `kdialog` for optional graphical file/folder pickers

Examples:

- Arch Linux / Artix Linux: `sudo pacman -S --needed git base-devel pkgconf sdl2 sdl2_ttf mupdf zenity`
- Debian / Ubuntu: `sudo apt install git build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libmupdf-dev zenity`
- Fedora: `sudo dnf install git gcc make pkgconf-pkg-config SDL2-devel SDL2_ttf-devel mupdf-devel zenity`
- openSUSE: `sudo zypper install git gcc make pkg-config libSDL2-devel SDL2_ttf-devel mupdf-devel zenity`
- Gentoo: `sudo emerge --ask dev-vcs/git sys-devel/gcc sys-devel/make virtual/pkgconfig media-libs/libsdl2 media-libs/sdl2-ttf app-text/mupdf gnome-extra/zenity`
- Alpine: `sudo apk add git build-base pkgconf sdl2-dev sdl2_ttf-dev mupdf-dev zenity`
- FreeBSD: `doas pkg install git gmake pkgconf sdl2 sdl2_ttf mupdf zenity`
- OpenBSD: `doas pkg_add git gmake pkgconf sdl2 sdl2-ttf mupdf zenity`

Install a common sans font if the UI text does not appear. DejaVu Sans, Noto
Sans, and Liberation Sans are detected from common system paths.

## Build And Install

Linux:

```sh
git clone https://github.com/Vifuddyxg/creader
cd creader
make
sudo make install
```

FreeBSD/OpenBSD:

```sh
git clone https://github.com/Vifuddyxg/creader
cd creader
gmake
doas gmake install
```

`make install` installs:

- `/usr/local/bin/creader`
- `/usr/local/share/applications/creader.desktop`

That makes `creader` available from the terminal and from application launchers
such as `rofi -show drun`, `wofi --show drun`, or similar menus.

Run without installing:

```sh
./creader
./creader /path/to/book.pdf
```

Run after installing:

```sh
creader
creader /path/to/book.pdf
```

## Reader Controls

- Mouse wheel / `Up` / `Down`: smooth scroll
- `Right` / `PageDown`: next page, or next spread in two-page mode
- `Left` / `PageUp`: previous page, or previous spread in two-page mode
- `+` / `=`: zoom in
- `-`: zoom out
- `0`: reset zoom to 100%
- `r`: rotate page 90 degrees
- `d`: toggle continuous single-page mode and two-page book mode
- `f`: fullscreen on/off
- `b`: open library
- `q`: quit

Mouse controls in reader mode:

- Left sidebar slider: zoom
- Left sidebar `Book` / `Single`: switch view mode
- Right sidebar slider: jump through pages
- Right sidebar `Lib`: return to library

## Library Controls

- Double-click a card: open it
- `Enter`: open selected item
- `Delete`, `Backspace`, or `x`: remove selected item from the library only
- `o`: add a file using the graphical picker
- `f`: add a folder using the graphical picker
- `a`: type a file path manually, then `Enter`
- `d`: type a folder path/name manually, then `Enter`
- `/`: search by title or path
- Arrow keys: navigate cards
- Mouse wheel: scroll library
- Drag and drop a PDF/EPUB/CBZ/CBR or folder into the library window to import
- `b` / `Esc`: return to reader when a document is open
- `q`: quit

## Data Storage

Library and progress data are stored in:

```text
~/.local/share/creader/library.dat
```

The file is plain text and can be backed up or edited carefully. Existing
`~/.local/share/nvreader/library.dat` data is migrated on first run when found.

## Notes

- Continuous mode renders nearby pages lazily and uses a safer layout estimate
  for large PDFs, so scrolling stays smooth without forcing every page to render.
- Very large pages are skipped instead of allocating unsafe pixmaps/textures.
- EPUB/CBZ/CBR support depends on how MuPDF was built by your distribution.
- creader does not support annotations, forms, or document editing.

## Project Structure

```text
creader/
├── src/
│   ├── main.c       # entry point and main loop
│   ├── state.c      # app state initialization
│   ├── pdf.c        # MuPDF wrapper
│   ├── ui.c         # SDL2 rendering
│   ├── input.c      # keyboard, mouse, drag/drop handling
│   ├── library.c    # in-memory library management
│   ├── filepick.c   # file/folder picker integration
│   └── storage.c    # load/save library.dat
├── include/         # headers
├── assets/          # reserved for future assets
└── Makefile
```
