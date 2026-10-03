# ═══════════════════════════════════════════════════════════════════════════
# FreeType 2.14.3 —— 本仓库编译的源文件列表
#
# ★★ 为什么是**显式列表**而不是 GLOB：
#   FreeType 的 `src/**/*.c` 共 210 个，其中 **165 个是「被其他 .c include」的**
#   （如 `src/lzw/ftzopen.c` 由 `ftlzw.c` include、`src/base/ftcalc.c` 由 `ftbase.c` include），
#   **不是独立 TU** —— GLOB 逐个编译**必然失败**（实测：`ftzopen.c` 报
#   `expected '=', ',', ';' ... before '{'`，根因即 `FT_LOCAL` 未定义）。
#   ⇒ 只编译**模块入口** `.c`（= 上游 `CMakeLists.txt` 的 `BASE_SRCS` 所定义的 TU 边界）。
#
# ★ 来源：上游 `CMakeLists.txt` 的 `set(BASE_SRCS ...)`（40 项）
#          + Windows 分支追加的两项（上游 `if (WIN32)` 块）：
#            · `builds/windows/ftsystem.c`——**宽字符 Win32 API**（比通用
#              `src/base/ftsystem.c` 更适合非 ASCII 字体路径）
#            · `builds/windows/ftdebug.c`——★ **必需**：`FT_Trace_Disable` /
#              `FT_Trace_Enable` 的定义处（`src/smooth/ftgrays.c` 引用它们；
#              漏掉会在**链接期**报 undefined reference，编译期看不出来）
# ★ 未采纳上游 WIN32 分支的 `src/base/ftver.rc`（版本资源）——需要 `enable_language(RC)`，
#   且只影响文件属性、不影响功能。★ 升级时如需版本资源再补。
#
# ★ 升级时：取新版本上游 `CMakeLists.txt` 的 `BASE_SRCS`，与本列表逐行比对。
#           上游若增删模块（如 2.14 之前没有 `src/sdf/` / `src/svg/`），本列表同步增删。
# ═══════════════════════════════════════════════════════════════════════════

set(FREETYPE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/autofit/autofit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftbase.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftbbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftbdf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftbitmap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftcid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftfstype.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftgasp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftglyph.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftgxval.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftinit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftmm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftotval.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftpatent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftpfr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftstroke.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftsynth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/fttype1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/base/ftwinfnt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/bdf/bdf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/bzip2/ftbzip2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/cache/ftcache.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/cff/cff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/cid/type1cid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/gzip/ftgzip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/lzw/ftlzw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/pcf/pcf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/pfr/pfr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/psaux/psaux.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/pshinter/pshinter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/psnames/psnames.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/raster/raster.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/sdf/sdf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/sfnt/sfnt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/smooth/smooth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/svg/svg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/truetype/truetype.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/type1/type1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/type42/type42.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/src/winfonts/winfnt.c
    # Windows 平台系统接口（宽字符 API）与调试追踪
    #   ★ ftsystem.c —— 上游 WIN32 分支选用（宽字符路径）
    #   ★ ftdebug.c  —— ★ **必需**：`FT_Trace_Disable` / `FT_Trace_Enable` 在此定义，
    #      而 `src/smooth/ftgrays.c` 引用它们（漏了 ⇒ 链接期 undefined reference，已实测）
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/builds/windows/ftsystem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/builds/windows/ftdebug.c
)
