# ECDI

**ECDI** is a from-scratch C++20 GUI framework for Windows, built directly on raw Win32 and GDI — no third-party UI, rendering, or utility libraries. Pure C++ and the platform SDK only.

> Currently under active development toward **v0.1.0** (first library release). Not yet stable — the API may change freely under SemVer `0.y.z`.

## The goal: *Everyone Can Do It*

The name is the mission — **E**veryone **C**an **D**o **I**t.

ECDI is a **teaching-first** framework: it exists so that anyone can learn how a GUI framework is built, and put it to use, at the lowest possible learning cost. That is a design constraint rather than a slogan, so it shows up as rules:

- **Plain public API** — a name says what it does; one word per concept, never two names for one idea.
- **No speculative abstraction** — an abstraction arrives when a second real consumer appears, not before; "not yet" is written down in the deferred ledger (`docs/roadmap-deferred.md`) instead of being coded.
- **The design trail is the textbook** — every phase ships requirements → preliminary design → detailed design *before* any code, so the reasoning is readable, not just the result.
- **Runnable examples are the front door** — `examples/MinimalApp` is a complete app in about 20 lines, and `examples/ModelProbe` is a real tool built on the same public API.
- **Adopting a new capability never breaks existing code** — new behaviour defaults to *bit-for-bit* what it did before; that has been a hard contract in every recent phase.

## Why ECDI

Most hobby GUI projects stop at "a window with buttons". ECDI is built the way a real framework is built: strict layering, platform abstraction, a self-hosted test suite, and a library-first build — with every design decision documented (`docs/`, 129 design documents in Chinese).

## How this was built

"From-scratch" describes the provenance of the code — no third-party libraries, no borrowed framework — not its authorship. Implementation and documentation drafting are done with heavy AI assistance.

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
- **Zero third-party dependencies**: no external libraries, no GDI+, no UI framework — only the Windows SDK (`user32`, `imm32`, `msimg32`, `windowscodecs`, `ole32`, `shlwapi`, `shell32`). Rendering is GDI + `msimg32` (AlphaBlend); image decoding uses the system WIC.

## Features

- **Widgets**: Panel, Label, Button, CheckBox, Radio, TextBox (single/multi-line, IME, undo/redo, clipboard), ProgressBar, CollapsiblePanel
- **Layout**: `VerticalLayout` / `HorizontalLayout` with stretch weights, spacing, cross-axis fill
- **AutoSize**: content-driven sizing (`GetPreferredSize` / `AutoSize`) with size-intent semantics (explicit size > stretch > auto)
- **Animation**: per-window `AnimationManager`, token-based, easing functions
- **Theme**: style layer (colors/fonts/corner radius/borders) with per-instance overrides
- **Text**: text measuring, selection, IME composition, clipboard, undo/redo
- **Imaging**: WIC-backed decoding (`Decode::DecodeFile` / `Decode::DecodeMemory`) producing premultiplied BGRA, ready for `DrawImage`
- **Anti-aliasing**: supersampled corner coverage masks for rounded rects (`S=8`), cached per radius — GDI has no native AA, so arcs are composited through a premultiplied alpha path that composes with the theme's corner radius
- **Window chrome**: borderless mode (`WM_NCCALCSIZE` interception) with a self-drawn caption bar — title plus vector min/max/close buttons — and `NCHITTEST` delegated into the widget tree, so interactive controls inside the caption stay clickable while the rest drags the window
- **Shell integration**: tray icon (application-level — lives on `PlatformApplication`, not on any `Window`, so closing/rebuilding every window leaves it intact) with a native popup menu, plus window-level file drop (`WM_DROPFILES` → UTF-8 path list; the `HDROP` is released before the event is emitted). Both stay behind platform seams — the public API exposes no Win32 types.
- **Testing**: self-hosted test framework (236 cases, zero dependencies) with a recording backend for paint assertions

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
| `ECDI` | static library | The framework (`include/ECDI/*.h` — 92 public headers; internal implementation lives in `src/`) |
| `ecdi_tests` | executable | Self-hosted test suite — 236 cases, zero dependencies |
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
#include <ECDI/Application/Application.h>
#include <ECDI/Window/Window.h>
#include <ECDI/Widget/Panel.h>
#include <ECDI/Layout/VerticalLayout.h>

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
ECDI/       framework sources (include/ = 92 public headers, src/ = implementation + tests)
examples/   consumers: ModelProbe (real tool), MinimalApp (library-ization smoke test), VisualTest
probe-go/   Go backend embedded into ModelProbe as an RC resource
docs/       design documents (131 files; requirements → preliminary → detailed, per phase)
```

📚 **Design documents** (Chinese): [docs/README.md](docs/README.md) — full index of phase-by-phase design docs, development progress, and technical-debt ledger.

## Status

| Phase | Scope | Status |
|---|---|---|
| 1–5 | Core, events, widgets, text, IME | ✅ |
| 6–7 | Layout, platform decoupling, test framework | ✅ |
| 8–9 | Rendering extensions, theme, hover, clip, animation, AutoSize | ✅ |
| **10** | **Library-ization (v0.1.0): public API boundary, install/export, external consumer** | ✅ |
| 11 | Image decoding (WIC backend, `Decode` module, premultiplied-BGRA contract) | ✅ |
| 12 | Window chrome (borderless mode, `WM_NCCALCSIZE` / `NCHITTEST` interception, maximize work-area correction, DWM integration) | ✅ |
| 13 | Caption bar (self-drawn title bar, `NCHITTEST` → widget-tree delegation, window-state query API) | ✅ |
| **14** | **Tray icon + file drop**: application-level platform seam (`PlatformApplication` + internal hidden top-level host window), `NOTIFYICON_VERSION_4` callback translation, self-healing after explorer restart, window-level `WM_DROPFILES` | ✅ |
| **15** | **Scroll container (`ScrollView` + scrollbar)**: content-offset seam (`GetContentOffsetX/Y`, consumed by paint / hit test / absolute position), `ClipsChildren` hit-test gate, two-pass dual-axis viewport, single-source offset with self-drawn scrollbars, internal `ScrollContent` as the root of the content coordinate space, ModelProbe list migrated off its hand-rolled container | ✅ |
| **16** | **Desktop-resident layer (`WindowLayer::Desktop`)**: ships the route the spike validated — a top-level window wedged directly above the desktop window, held there by a foreground event hook. The fact survey traced the failure to a single style bit (Show Desktop only minimizes minimizable windows), so desktop windows drop `WS_MINIMIZEBOX` instead of switching to `WS_POPUP`. No public API change. | ✅ Requirements confirmed · preliminary design reviewed (`SWP_FRAMECHANGED` measured and closed: not needed) · detailed design approved (v1.11) · implemented and closed 2026-09-21: three batches plus the follow-chain fix and T16-8; 218 tests green; A1-A8 accepted. Public API additions: none. |
| **17** | **Layout padding**: one optional `int` on each of `VerticalLayout` / `HorizontalLayout`, applied at four points in `Arrange` — the main-axis start, the remaining-space computation, the cross-axis size and the cross-axis position. Padding is a hard inset: when there is not enough room the content area collapses to zero and the coordinates stay put. Four existing files change, no new headers. | ✅ Implemented — 226 tests green, acceptance A1–A6 passed. The demo puts the inset on the page rather than the root: the root is a bare widget with no background and the backend clears with a white brush, so padding there would expose a white frame and pull the caption bar in with it. |
| **18** | **Window / root background**: the client-area clear color becomes configurable — `Window::SetBackgroundColor` → `Renderer::BeginFrame(const Color&)` → `RenderingBackend::BeginFrame(const Color&)`, so GDI clears with the frame’s color instead of a hard-coded white brush. The color lives on `Window` (the single source of the default), the backend stays stateless and Window-agnostic, and alpha is dropped (`COLORREF` has no alpha channel). Two public API additions; 226 → 231 cases. | ✅ Requirements, preliminary and detailed design all reviewed; implemented — 231 tests green across four toolchains. Visual outcome recorded: with the inset at the root the caption bar is pulled in along with the content -- and, on inspection, it also desynchronises the caption bar from the platform’s drag region (`WM_NCHITTEST` grants dragging across a full-width `y < captionHeight` band and never looks at where the caption widget actually sits), which turned "a flush caption bar" into a platform contract rather than a style choice. The demo therefore keeps the inset on the page. A1–A7 all pass, with the design’s call-site count corrected where it had added the `Begin`/`End` pair together (6 → 3). |
| **18.1** | **Child inset**: give a child its own inset inside the slot its parent layout assigns, so that “window-level” padding can apply to content without dragging the caption bar along. | ⏸️ Shelved 2026-09-22, decided the day it opened. Splitting the requirement into three layers settled it: a flush caption bar and an inset content area are both baseline (the first is a *platform contract*), while showing the window background through the gap is an optional effect. The first two were already in place; the third is met by composing a transparent `Panel` in the application, which needs no framework change. A per-child `SetInset` was parked — it reaches the same result but adds a near-synonym for `padding`. Restart conditions: `docs/roadmap-deferred.md` §7.8 #40. |
| **19** | **Mouse event dimensions**: carry the pressed-button set and the modifier state on mouse events. The platform already hands them over -- `WM_MOUSEMOVE` puts `MK_LBUTTON` and `MK_SHIFT` into `wParam` -- but the translator reads the coordinates and drops the rest, so every drag-while-holding gesture has to keep its own boolean. Wheel events already spell the principle out (raw values are not normalised; interpreting them is the consumer job); mouse events simply never carried the raw values. Modifiers reuse the keyboard `KeyModifier`. | ✅ Requirements v1.1, preliminary design v1.1 and detailed design v1.1 all approved (2026-09-23); implemented, with 236 tests green across four toolchains. First item of the defect queue. The design settles four things: the pressed set stays a private bitmask behind an `IsButtonDown` predicate rather than a new public type; the existing constructor keeps working because new parameters carry defaults; tests drive the real translator rather than hand-building events; and modifiers come from `wParam` at the event instant, which is why Alt is deliberately absent. |

## License

[MIT](LICENSE)
