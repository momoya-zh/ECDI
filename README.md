# ECDI

> 📄 Language: **English** · [简体中文](README.zh-CN.md)

**ECDI** is a from-scratch C++20 GUI framework for Windows, built directly on raw Win32 and GDI — pure C++, plus the platform SDK and only those third-party libraries that clear the bar set out under *The goal*.

> Currently under active development toward **v0.1.0** (first library release). Not yet stable — the API may change freely under SemVer `0.y.z`.

## The goal: *Everyone Can Do It*

The name is the mission — **E**veryone **C**an **D**o **I**t.

ECDI is a **teaching-first** framework: it exists so that anyone can learn how a GUI framework is built, and put it to use, at the lowest possible learning cost. That is a design constraint rather than a slogan, so it shows up as rules:

- **Plain public API** — a name says what it does; one word per concept, never two names for one idea.
- **No speculative abstraction** — an abstraction arrives when a second real consumer appears, not before; "not yet" is written down in the deferred ledger (`docs/dev/roadmap-deferred.md`) instead of being coded.
- **The design trail is the textbook** — every phase ships requirements → preliminary design → detailed design *before* any code, so the reasoning is readable, not just the result.
- **Runnable examples are the front door** — `examples/MinimalApp` is a complete app in about 20 lines, and `examples/ModelProbe` is a real tool built on the same public API.
- **Adopting a new capability never breaks existing code** — new behaviour defaults to *bit-for-bit* what it did before; that has been a hard contract in every recent phase.
- **No dependency without a bar** — a third-party library is admitted only when all three hold: it covers a domain that is *not* this project's own (the GUI framework itself); writing it by hand would leave its acceptance test dependent on the very thing being written; and the obligation to track its releases — security included — is accepted. A rendering pipeline or a platform abstraction fails the first two, so those stay hand-written.

## Why ECDI

Most hobby GUI projects stop at "a window with buttons". ECDI is built the way a real framework is built: strict layering, platform abstraction, a self-hosted test suite, and a library-first build — with every design decision documented (`docs/`, 229 design documents in Chinese).

## How this was built

"From-scratch" describes the provenance of the code — no borrowed UI framework or engine — not its authorship. Implementation and documentation drafting are done with heavy AI assistance.

What that assistance is *not* is unsupervised generation. Every phase runs the same loop, and each step leaves a written record:

1. **Design before code** — requirements → preliminary design → detailed design, reviewed before any implementation (`docs/`, per phase)
2. **External review gate** — every design document is critiqued and then revised, with version numbers bumped per review round
3. **Atomic authorization** — a change set is applied only when *all* of its files are approved; if one file is unapproved, nothing is touched
4. **Verification by hand** — builds and test runs happen in Visual Studio / CLion across four toolchains, never delegated

The architecture, the layering, the phase breakdown, and every design decision are mine. That is what `docs/` is evidence of.

## Architecture

```
Widget ──▶ PaintContext ──▶ CommandBuffer ──▶ Renderer ──▶ RenderingBackend (GDI)
                                                                    ▲
   Application / Window / EventSystem ──────────▶ Platform abstraction (PlatformWindow, ...)
```

- **Four-layer rendering contract**: Widget, PaintContext, CommandBuffer and Renderer never see each other's internals — a `Widget` only emits commands, the backend only consumes them. Swapping GDI for another backend means implementing one interface.
- **Platform abstraction**: `PlatformWindow`, `PlatformApplication`, `ChildProcess` interfaces isolate all `Windows.h` usage; the framework core is platform-independent C++20. Window-level capabilities (chrome, input, file drop) hang off `PlatformWindow`; application-level ones (tray icon) off `PlatformApplication` — two symmetric seams, neither of which grows its own platform object tree.
- **Dependencies by exception**: no GDI+, no UI framework, no rendering library — the framework itself stands on the Windows SDK alone (`user32`, `imm32`, `msimg32`, `windowscodecs`, `ole32`, `shlwapi`, `shell32`). Rendering is GDI + `msimg32` (AlphaBlend); image decoding uses the system WIC. **FreeType** — text rasterisation and hinting for the GPU backend — is the first library admitted under the bar set out in *The goal*; it is vendored and tracked rather than linked dynamically.

## Features

- **Widgets**: Panel, Label, Button, CheckBox, Radio, TextBox (single/multi-line, soft wrap, vertical alignment, IME, undo/redo, clipboard), ProgressBar, RingProgressBar, CollapsiblePanel
- **Layout**: `VerticalLayout` / `HorizontalLayout` with stretch weights, spacing, cross-axis fill, plus `ListLayout` / `GridLayout` for index-ordered rows and fixed-cell grids
- **Layout invalidation**: geometry / text / visibility setters mark the root dirty, and the frame head settles layout once per frame (`NeedsLayout`) -- so an application never hand-writes `Arrange` / `Invalidate` chains
- **AutoSize**: content-driven sizing (`GetPreferredSize` / `AutoSize`) with size-intent semantics (explicit size > stretch > auto)
- **Animation**: per-window `AnimationManager`, token-based, easing functions
- **Theme**: style layer (colors/fonts/corner radius/borders) with per-instance overrides
- **Text**: text measuring, selection, IME composition, clipboard, undo/redo, word wrap (labels and the multi-line editor), font family / weight, and **inline styled spans** (`SetStyledText` — per-run weight / family / colour / italic / size inside one text flow, indexed by code point, with a shared per-line baseline; a zero-span flow stays bit-identical to plain text)
- **Imaging**: WIC-backed decoding (`Decode::DecodeFile` / `Decode::DecodeMemory`) producing premultiplied BGRA, ready for `DrawImage`
- **Anti-aliasing**: supersampled corner coverage masks for rounded rects (`S=8`), cached per radius — GDI has no native AA, so arcs are composited through a premultiplied alpha path that composes with the theme's corner radius
- **Window chrome**: borderless mode (`WM_NCCALCSIZE` interception) with a self-drawn caption bar — title plus vector min/max/close buttons — and `NCHITTEST` delegated into the widget tree, so interactive controls inside the caption stay clickable while the rest drags the window
- **Overlay & Popups**: window-level dual-tree topology with dedicated `m_overlayRoot` and three layered panels (Normal, Modal, Tooltip); escapes ancestor container clipping; Light Dismiss (outside-click auto-dismiss with pass-through / consume policies), ESC key handling, modal focus trap with previous-focus restoration, and Flip-then-Clamp anchor tracking with de-tree detection
- **Window geometry & material**: runtime bounds (`SetBounds` / `GetBounds`, request semantics -- the read-back value is authoritative), minimum size, and a frosted backdrop (`SetWindowBackdrop`, Acrylic with a documented degraded fallback)
- **Shell integration**: tray icon (application-level — lives on `PlatformApplication`, not on any `Window`, so closing/rebuilding every window leaves it intact) with a native popup menu, plus window-level file drop (`WM_DROPFILES` → UTF-8 path list; the `HDROP` is released before the event is emitted). Both stay behind platform seams — the public API exposes no Win32 types.
- **Testing**: self-hosted test framework (638 cases as of 2026-11-18, zero dependencies) with a recording backend for paint assertions

## Build

Requires C++20. Four toolchains are supported via CMake (MSVC / Clang / ClangCL / MinGW):

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

For a native Visual Studio solution, use the `vs2026` preset:

```bash
cmake --preset vs2026            # → cmake-build-vs/ : one project per target
cmake --build --preset vs2026-debug
```

There is no hand-written `.vcxproj` in this repository — CMake generates the
solution, so each target becomes its own project wired with `ProjectReference`,
and the file lists cannot drift out of sync.

Targets:

| Target | Type | Description |
|---|---|---|
| `ECDI` | static library | The framework (`include/ECDI/*.h` — 102 public headers; internal implementation lives in `src/`) |
| `ecdi_tests` | executable | Self-hosted test suite — 638 cases (2026-11-18), zero dependencies |
| `modelprobe` | executable | ModelProbe — a real tool built on ECDI (see below) |
| `visualtest` | executable | Side-by-side visual check for image decoding (Phase 11) |
| `ecdi_public_header_test` | test | Self-containment check: every public header compiled as an independent TU (opt-in via `--target`) |

### Install & consume as a library

```bash
cmake -S . -B build && cmake --build build
cmake --install build --prefix <prefix>       # headers + ECDI.lib + ECDIConfig.cmake
```

Then, from any external CMake project:

```bash
cmake -S examples/MinimalApp -B build-minimal -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build-minimal && .\build-minimal\MinimalApp.exe
```

`examples/MinimalApp` is the library-ization smoke test: it consumes ECDI strictly via
`find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI` — no manual include/lib paths, and
platform libraries propagate through the installed CMake targets.

Visual Studio and CLion both open the CMake project directly — there is no hand-written `.vcxproj` in this repository (see **Build** above).

## Minimal usage

```cpp
#include <Windows.h>   // wWinMain entry (WINAPI / HINSTANCE / PWSTR)

// Windows.h macro guard — the DrawText macro would otherwise rewrite
// PaintContext::DrawText out of the library's exported symbol (same three
// lines as examples/MinimalApp/main.cpp; harmless when the macro is absent)
#ifdef DrawText
#undef DrawText
#endif

#include <ECDI/Application/Application.h>
#include <ECDI/Window/Window.h>
#include <ECDI/Widget/Panel.h>
#include <ECDI/Layout/VerticalLayout.h>

#include <memory>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ECDI::Application application;

    ECDI::Window& window = application.Create("Hello ECDI", 640, 480);
    ECDI::Widget& root = window.GetRootWidget();
    root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));

    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStretch(1);                       // fill the window
    root.AddChild(std::move(panel));
    root.Arrange();

    window.Show();
    return application.Run();
}
```

## Examples

- **[ModelProbe](examples/ModelProbe/)** — a model-probing tool shipped as a single static-linked executable: process backend embedded as an RC resource and released at runtime, dynamic stat text (AutoSize), adaptive layout, custom app icon. This is the reference consumer of the framework.

## Project layout

```
ECDI/       framework sources (include/ = 102 public headers, src/ = implementation + tests)
examples/   consumers: ModelProbe (real tool), MinimalApp (library-ization smoke test), VisualTest; DesktopNest (desktop organizer -- app-side M1: requirements v1.1 review passed, preliminary design v1.3 (all five open questions settled) and detailed design v1.3 filed 2026-10-09 -- three documents in place, three review rounds absorbed, ready for implementation)
probe-go/   Go backend embedded into ModelProbe as an RC resource
docs/       usage.md (API / usage guide, how to use the framework) + dev/ (228 design documents: requirements → preliminary → detailed, per phase; 229 total incl. usage.md)
```

📚 **Design documents** (Chinese): [docs/dev/README.md](docs/dev/README.md) — full index of phase-by-phase design docs, development progress, and technical-debt ledger.

📖 **Usage guide** (Chinese): [docs/usage.md](docs/usage.md) — how to use the framework: every widget, the layout logic (`SetStretch` / the four `Layout` classes / when `Arrange` runs), events, animation, theming, and the known-limits list.

## Status

Detailed design triads, verification logs, and decision histories live in [docs/dev/README.md](docs/dev/README.md) and [docs/dev/roadmap-deferred.md](docs/dev/roadmap-deferred.md).

| Phase | Scope | Status |
|---|---|---|
| 1–5 | Core, events, widgets, text, IME | ✅ |
| 6–7 | Layout, platform decoupling, test framework | ✅ |
| 8–9 | Rendering extensions, theme, hover, clip, animation, AutoSize | ✅ |
| **10** | **Library-ization (v0.1.0)**: public API boundary, install/export, external consumer | ✅ |
| 11 | **Image decoding**: WIC backend, `Decode` module, premultiplied-BGRA contract | ✅ |
| 12 | **Window chrome**: borderless mode, `WM_NCCALCSIZE` / `NCHITTEST` interception, maximize work-area correction, DWM integration | ✅ |
| 13 | **Caption bar**: self-drawn title bar, `NCHITTEST` → widget-tree delegation, window-state query API | ✅ |
| **14** | **Tray icon + file drop**: `PlatformApplication` seam + hidden host window, `NOTIFYICON_VERSION_4`, explorer-restart recovery, `WM_DROPFILES` | ✅ |
| **15** | **Scroll container (`ScrollView` + `ScrollBar`)**: content-offset seam, `ClipsChildren` hit-test gate, two-pass dual-axis viewport, single-source offset | ✅ |
| **16** | **Desktop-resident layer (`WindowLayer::Desktop`)**: window anchored above desktop via foreground hook; drops `WS_MINIMIZEBOX` to survive Show Desktop (API +0) | ✅ Closed 2026-09-21 (218 tests) |
| **17** | **Layout padding**: optional `int` inset on `VerticalLayout` / `HorizontalLayout` applied at four `Arrange` points; hard inset collapsing to 0 | ✅ Closed (226 tests) |
| **18** | **Window / root background**: `Window::SetBackgroundColor` → `RenderingBackend::BeginFrame(const Color&)` (API +2) | ✅ Closed (231 tests) |
| **18.1** | **Child inset**: per-child layout inset | ⏸️ Shelved 2026-09-22 (flush caption bar + transparent `Panel` already suffice; see `roadmap-deferred.md` #40) |
| **19** | **Mouse event dimensions**: pressed-button bitmask (`IsButtonDown`) + `KeyModifier` state on mouse events from `wParam` (#41) | ✅ Closed 2026-09-23 (236 tests) |
| **20 / 20.1** | **Per-Monitor V2 DPI awareness & render scaling**: public API stays in DIP, conversion at platform & `Renderer` entry, backend stays in physical pixels (#8) | ✅ Closed 2026-09-24 (252 tests) |
| **21** | **System icons to `Image`**: runtime `HICON` / path → premultiplied-BGRA `Image` via `GetIconInfo` + alpha-presence detection (#G-2, API +1) | ✅ Closed 2026-09-27 (261 tests) |
| **22** | **DIP size contract on `Create`**: `ApplyStartupSize` seam fixes `Create`→`Show` DIP drift across non-96 DPI screens (D-7) | ✅ Closed 2026-09-29 (264 tests) |
| **23** | **Worker-to-UI dispatch**: `Application::PostToUi` queued through `PlatformApplication` / `PostThreadMessageW`, swap-drained safely (D-4) | ✅ Closed 2026-09-29 (275 tests) |
| **24** | **Line anti-aliasing (`DrawLine`)**: `Render/LineCoverage` S=8 round-cap rasterization sharing `RasterizeMask` (#43 / D-6, API +0) | ✅ Closed 2026-10-01 (297 tests) |
| **25** | **List / grid container**: `ListLayout` / `GridLayout` arrangement + `C-VIS` invisible-child negative parking (#42 / D-5, headers 92→94, API +2) | ✅ Closed 2026-10-02 (307 tests) |
| **26** | **FreeType text stack + GL backend**: vendored FreeType 2.14.3, shared `FontEngine`, WGL `GLRenderer` + glyph atlas, D-8 per-window DPI measurement fix | ✅ Closed 2026-10-04 (319 tests) |
| **27** | **Viewport culling**: construction-side `m_cullStack` on `PaintContext` skipping off-screen subtrees before command emission (#49, headers 94, API +2) | ✅ Closed 2026-10-04 (330 tests, −98.9% render commands) |
| **28** | **Long-text measurement & draw cost**: FreeType per-glyph advance memo, `TextWidget` revision fingerprint, GDI clip-prefix truncation (#50, API +0) | ✅ Closed 2026-10-06 (335 tests) |
| **29** | **Text multi-line capability (`Label` word-wrap)**: opt-in `SetWordWrap` sharing one `TextLayout` + `TextMeasurer::FitText` virtual (#52, API +4) | ✅ Closed 2026-10-07 (349 tests) |
| **30** | **Text / interaction quick-fix round**: `TextBoxStyle::caretColor`, `Core/TextAlignment.h` on `Button`, `H/V` layout `C-VIS`, `CollapsiblePanel` pin (#51, headers 94→95) | ✅ Closed 2026-10-07 (356 tests) |
| **31** | **Window geometry & line height**: `SetBounds` / `GetBounds` request semantics + unified `LineHeight` rule (REQ-05) | ✅ Closed 2026-10-08 (366 tests; O10 residual documented) |
| **32** | **Frosted backdrop**: `WindowBackdrop.h`, DWM Acrylic accent + 32bpp DIB back buffer + per-command alpha fix-up + 0.8 opaque-white degradation (#55 / #57 / #58, headers 95→96) | ✅ Closed 2026-10-14 (445 tests) |
| **33** | **Arc primitive & ring progress**: `DrawArc` + `GenerateArcCoverage` + `RingProgressBar` + `PaintProbe` verification (#REQ-08, headers 96→98) | ✅ Closed 2026-10-16 (465 tests) |
| **34** | **Backend geometry quantization**: `ToPixelRect` / `ToPixelBox` unifying `GDIBackend` on `QuantizeEdge` floor rules to match GL | ✅ Closed 2026-10-19 (487 tests) |
| **35** | **Event consumption & safe destroy**: `Event::SetHandled()` stops bubbling, `Widget::RequestRemove()` defers destruction to outermost dispatch | ✅ Closed 2026-10-19 (487 tests) |
| **36** | **Font expressiveness**: `Font::weight` + TTC `faceIndex` + compound family lookup via `ResolveFileCandidates` (#70 / REQ-10, headers 98, API +1 field) | ✅ Closed 2026-10-24 (497 tests) |
| **37** | **Single-line `TextBox` vertical alignment**: `Core/VerticalAlignment.h` + `TextBoxStyle::verticalAlign` (#76 / REQ-14, headers 99→100) | ✅ Closed 2026-10-26 (513 tests) |
| **38** | **`TextBox` editor soft wrap**: visual `LineRange` / `LineDrawRange` line table + 10-field invalidation key (#77 / REQ-15, headers 100, API +0 public) | ✅ Closed 2026-10-29 (531 tests) |
| **39** | **UTF-8 O(N) pass**: `DecodeCodepointAt` single-pass `RecalculateLines` + equivalent default `FitText` rewrite (#78, API +1) | ✅ Closed 2026-10-29 (538 tests) |
| **40** | **Wrap per-line prep elimination**: lazy `TextWidget` codepoint↔byte table + 5-arg `FitText` overload (#83, API +1) | ✅ Closed 2026-10-29 (546 tests) |
| **41** | **Frame-level layout / repaint invalidation**: dual-channel dirty flags + frame-head `SettleLayoutIfDirty` (#79, API +1 `NeedsLayout`) | ✅ Closed 2026-10-30 (556 tests) |
| **42** | **Window overlay root & light dismiss**: `Window/OverlayTypes.h`, `m_overlayRoot` layers (`Normal`/`Modal`/`Tooltip`), ESC/focus trap, deferred overlay destroy (#81, headers 100→101, API +4) | ✅ Closed 2026-10-30 (572 tests) |
| **43** | **Scrollbar per-axis thickness**: split `VerticalBarThickness()` / `HorizontalBarThickness()` closing M5/M6 (#84, headers 101, API +0) | ✅ Closed 2026-11-01 (584 tests) |
| **44** | **Inline styled spans (rich text)**: atomic `SetStyledText`, `Widget/TextSpan.h`, per-run `DrawText` reusing backends (#88 / REQ-20, headers 101→102) | ✅ Closed 2026-11-08 (599 tests) |
| **45** | **Styled spans expressiveness**: `Font::italic`, `SpanStyle::italic` / `size`, `TextMeasurer::Ascent`, per-line max line box & shared baseline (#89, API +3 fields + 1 virtual) | ✅ Closed 2026-11-09 (608 tests) |
| **46** | **GL/FreeType font parity**: synthetic bold (`w>=551`, `ydelta=0`, `ceil(65536/x_ppem)`, 0.0000 max ratio diff 12..32px) + synthetic oblique (`>=1.2px`, `B->O`) + `FontEngine` single-pass lazy `FT_Get_Sfnt_Name` Chinese family lookup (~96-103ms) (#94, folding #69 / L45-1 / C36-4) | ✅ Closed 2026-11-15 (622 tests, headers 102, Batch 0/1/2 implemented; MinGW & Clang verified) |
| **47** | **GL rendering cache & automated verification**: `GLGlyphAtlas` paged expansion/LRU eviction, `DrawImage` 128-bit payload hash LRU texture cache, headless/offscreen WGL pixel verification (`#95`, closing `#75` ③) | ✅ Completed 2026-11-16 (`Batch 0–2` landed across 6 internal/test files; 631 cases, 102 public headers, verified on MinGW & Clang) |

## License

ECDI itself is [MIT](LICENSE)-licensed (Copyright © 2026 momoya-zh).

**Third-party**: FreeType 2.14.3 — vendored under `third_party/freetype`, statically linked into the GL backend's text stack — is dual-licensed under the FreeType License (**FTL**) or GPLv2; this project takes the **FTL**. Full license texts ship in-tree: `third_party/freetype/LICENSE.TXT` (entry point) pointing at `docs/FTL.TXT` / `docs/GPLv2.TXT`; the contrib licenses it references (bdf/pcf X11-style, gzip/zlib, autofit `ft-hb-*` Old MIT) ride along under `src/`. FTL §2 requires any binary distribution embedding FreeType to carry a disclaimer in the distribution documentation:

> This product is based in part on the work of the FreeType Team (https://freetype.org).

The FreeType Team's preferred credit form (FTL introduction, encouraged; `<year>` = the copyright year of the FreeType version in use):

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

`cmake --install` ships the notice with the prefix — `<prefix>/share/licenses/ECDI/NOTICE.md` plus ECDI's MIT `LICENSE` and FreeType's `LICENSE.TXT` / `FTL.TXT` / `GPLv2.TXT` under `share/licenses/ECDI/` — because the install prefix is a binary distribution in its own right (FreeType objects are merged into the `ECDI` static library). Delivery notes must pin the notice item and the delivered library's SHA256; see [docs/usage.md §14](docs/usage.md).
