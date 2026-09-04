# ECDI

**ECDI** is a hand-written C++20 GUI framework for Windows, built from scratch on top of raw Win32 and GDI — no third-party UI, rendering, or utility libraries. Pure C++ and the platform SDK only.

> Currently under active development toward **v0.1.0** (first library release). Not yet stable — the API may change freely under SemVer `0.y.z`.

## Why ECDI

Most hobby GUI projects stop at "a window with buttons". ECDI is built the way a real framework is built: strict layering, platform abstraction, a self-hosted test suite, and a library-first build — with every design decision documented (`docs/`, 90+ design documents in Chinese).

## Architecture

```
Widget ──▶ PaintContext ──▶ CommandBuffer ──▶ Renderer ──▶ RenderingBackend (GDI)
                                                                    ▲
   Application / Window / EventSystem ──────────▶ Platform abstraction (PlatformWindow, ...)
```

- **Four-layer rendering contract**: Widget, PaintContext, CommandBuffer and Renderer never see each other's internals — a `Widget` only emits commands, the backend only consumes them. Swapping GDI for another backend means implementing one interface.
- **Platform abstraction**: `PlatformWindow`, `PlatformApplication`, `ChildProcess` interfaces isolate all `Windows.h` usage; the framework core is platform-independent C++20.
- **Zero dependencies**: no STL-external libraries, no GDI+, no UI framework. `msimg32` (AlphaBlend) is the only non-kernel link.

## Features

- **Widgets**: Panel, Label, Button, CheckBox, Radio, TextBox (single/multi-line, IME, undo/redo, clipboard), ProgressBar, CollapsiblePanel
- **Layout**: `VerticalLayout` / `HorizontalLayout` with stretch weights, spacing, cross-axis fill
- **AutoSize**: content-driven sizing (`GetPreferredSize` / `AutoSize`) with size-intent semantics (explicit size > stretch > auto)
- **Animation**: per-window `AnimationManager`, token-based, easing functions
- **Theme**: style layer (colors/fonts/corner radius/borders) with per-instance overrides
- **Text**: text measuring, selection, IME composition, clipboard, undo/redo
- **Testing**: self-hosted test framework (~150 cases, zero dependencies) with a recording backend for paint assertions

## Build

Requires C++20. Four toolchains are supported via CMake (MSVC / Clang / ClangCL / MinGW):

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Targets:

| Target | Type | Description |
|---|---|---|
| `ECDI` | static library | The framework (`include/ECDI/*.h`) |
| `modelprobe` | executable | ModelProbe — a real tool built on ECDI (see below) |

A Visual Studio project (`ECDI/ECDI.vcxproj`, via `ECDI.slnx`) is kept for Windows debugging.

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
ECDI/                 framework sources (include/ = public headers, src/ = implementation + tests)
examples/ModelProbe/  first real-world consumer of the framework
probe-go/             Go backend embedded into ModelProbe as an RC resource
docs/                 design documents (requirements → preliminary → detailed, per phase)
```

📚 **Design documents** (Chinese): [docs/README.md](docs/README.md) — full index of phase-by-phase design docs, development progress, and technical-debt ledger.

## Status

| Phase | Scope | Status |
|---|---|---|
| 1–5 | Core, events, widgets, text, IME | ✅ |
| 6–7 | Layout, platform decoupling, test framework | ✅ |
| 8–9 | Rendering extensions, theme, hover, clip, animation, AutoSize | ✅ |
| **10** | **Library-ization (v0.1.0): public API boundary, install/export, external consumer** | 🚧 |

## License

[MIT](LICENSE)
