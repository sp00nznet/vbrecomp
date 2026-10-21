# Third-party code and licensing

This project is MIT (see [LICENSE](LICENSE)). Every file we wrote carries
`SPDX-License-Identifier: MIT`. The things below are
**not** ours and are not covered by that licence.

## 1. Recompiled ROM code is a derived work of the ROM

`v810recomp` translates a Virtual Boy ROM into C. That output is a machine
translation of copyrighted game code: it is a derived work of the input ROM, not
of this project, and **we cannot license it**. It is therefore never committed:

- `corpus/**` and `games/*/generated/**` are in `.gitignore`.
- Both are reproducible from a ROM with one command — see
  [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

The same goes for the ROMs themselves. `roms/` is gitignored; supply your own
dumps of cartridges you own.

### Screenshots

`docs/screenshots/` holds progress shots of what the recompiler currently
produces. They are image files we captured, not extracted game assets.

> **History:** recompiled code that was committed early on (`games/*/generated/`) was
> removed from the whole git history on 2026-10-21 (`git filter-repo` and a force-push).

## 2. Dear ImGui — MIT

Vendored in `src/imgui/` (v1.92.7-WIP) with the SDL2 and SDL_Renderer2 backends,
used by the debugger UI.

- Copyright (c) 2014-2025 Omar Cornut
- MIT, full text in [`src/imgui/LICENSE.txt`](src/imgui/LICENSE.txt)
- <https://github.com/ocornut/imgui>

## 3. stb single-file libraries — public domain / MIT

Sean Barrett's stb headers, each dual-licensed public domain (Unlicense) or MIT;
the full terms are at the bottom of each file.

| File | Library | Used for |
|---|---|---|
| `src/stb_image_write.h` | stb_image_write v1.16 | writing PNG screenshots |
| `src/imgui/imstb_rectpack.h` | stb_rect_pack v1.01 | vendored with Dear ImGui |
| `src/imgui/imstb_textedit.h` | stb_textedit v1.14 | vendored with Dear ImGui |
| `src/imgui/imstb_truetype.h` | stb_truetype v1.26 | vendored with Dear ImGui |

`src/stb_impl.c` is our own one-line translation unit that instantiates
stb_image_write, and is MIT.

## 4. Build-time dependency: SDL2

SDL2 is not vendored — it is found via vcpkg at build time. SDL2 is
[zlib-licensed](https://www.libsdl.org/license.php) and is not redistributed
here.

The vendored code is also listed in [NOTICE](NOTICE), with licence texts in
[LICENSES/](LICENSES/).

## Checking this yourself

```sh
# every file we wrote should carry an SPDX tag
git ls-files '*.c' '*.h' '*.py' '*.ps1' '*.lua' \
  | grep -v 'src/imgui\|stb_image_write' \
  | xargs grep -L 'SPDX-License-Identifier'
```

That command should print nothing.
