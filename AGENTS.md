# Zathura Agent Guide

## What This Is

This repository is the `lmist/zathura` fork of upstream Zathura, a girara/GTK document viewer. In the parent `pdfdb` repository it is used as an inspectable and patchable source tree for understanding or extending how Zathura opens documents.

Do not treat this tree as pdfdb application code. It is upstream-style C/GObject code with Meson build rules and a plugin ABI.

## Relationship To pdfdb

`pdfdb` stores PDF bytes in VoltDB and reconstructs them into disposable local cache files before opening Zathura. The fork now has optional native pdfdb support: it resolves `pdfdb://doc/<slug-or-id>` before `zathura_document_open`, then passes the reconstructed local PDF path into the existing document/plugin path.

For future pdfdb work:

- Do not add macFUSE or a mount daemon.
- Keep native pdfdb support behind the Meson `pdfdb` feature option.
- Resolve literal `pdfdb://` support before `zathura_document_open` and pass a local file path into the existing document/plugin path.
- Preserve the original URI for display/session context where useful.
- Avoid expanding the plugin API for storage unless the explicit goal is a broader upstream storage abstraction.

## Build And Test

Typical build:

```sh
meson setup build
meson compile -C build
meson test -C build
```

When optional Linux sandbox dependencies are unavailable, narrow the build:

```sh
meson setup build -Dseccomp=disabled -Dlandlock=disabled
```

On macOS, expect GTK/girara/plugin dependencies to come from the local package manager. Do not vendor those dependencies into this repository.

## Source Map

- `zathura/main.c`: process entry point and command-line handling.
- `zathura/zathura.h`: central `zathura_t` runtime state.
- `zathura/zathura.c`: session lifecycle, UI setup, document-open orchestration, path config, and cleanup.
- `zathura/commands.c`: command implementations, including `:open`.
- `zathura/document.c`: document allocation, local path/hash handling, content-type detection, plugin lookup, and page creation.
- `zathura/content-type.c`: libmagic/GLib content-type detection.
- `zathura/plugin.c`: plugin discovery, dynamic loading, required hook validation, and MIME mapping.
- `zathura/plugin-api.h`: public plugin ABI; update API/ABI intentionally if this changes.
- `zathura/render.c`: render worker pool, render requests, page cache, and recolor pipeline.
- `zathura/page-widget.c`: GTK page widget and render-request callbacks.
- `zathura/database-sqlite.c`: reader-state persistence, not document-byte storage.

## Coding Conventions

- Follow the local C style and Meson structure already in the repository.
- Keep patches focused and upstream-compatible unless the work is explicitly fork-only.
- Use GLib/GObject/GIO idioms already present in nearby code.
- Do not bypass the plugin manager for normal document formats.
- Keep document-byte storage concerns outside rendering and page-widget code.
- Update tests or add focused coverage when changing document open, plugin selection, or session persistence behavior.

## Git Hygiene

This directory is a git submodule inside `pdfdb`. Commit Zathura changes inside this repository first, then update the parent repository's submodule pointer.
