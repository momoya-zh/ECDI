# Phase 11 图片解码详细设计（v1.0）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-07
> 状态：待评审
> 前置：phase11-image-decode-requirements.md v1.1 / phase11-image-decode-preliminary-design.md **v1.1**（评审「通过，可进详设」）
> 一句话：收口 6 个开放点 + 落成实施规格（ComScope 定稿 / 溢出检查定稿 / 内存流与链接库定稿 / 测试资产生成策略 / 验收命令）——实施零决策

---

## 1. 范围映射

| 初设开放点 | 详设收敛 | 节 |
|---|---|---|
| ① SHCreateMemStream MinGW 可用性 | 定稿：主案 SH + 自实现 IStream 预案（§4） | §4 |
| ② WIC 指针管理风格 | 定稿：轻量 ComRAII 模板（§3.2） | §3 |
| ③ JPEG 测试资产 | 定稿：脚本生成策略（§6.3） | §6 |
| ④ uuid vs __uuidof | 定稿：`initguid.h` + `IID_PPV_ARGS`（§3.1——零 uuid.lib） | §3.1 |
| ⑤ palette 语义 | 定稿：`WICBitmapPaletteTypeCustom`（§3.6——32bpp 非索引输出忽略 palette） | §3.6 |
| ⑥ 链接库最小收敛 | 定稿：windowscodecs + ole32 + shlwapi（§5——initguid 去 uuid.lib；linker 验证收尾） | §5 |

## 2. Public 头定稿（初设 v1.1 §2 已含 `<cstdint>`——不变）

`include/ECDI/Decode/ImageDecoder.h` 按初设 v1.1 §2 草案原样实施（81st Public 头）。

## 3. `src/Platform/Win32/WicImageDecoder.cpp` 实施规格

### 3.1 include 策略与 CLSID 定义（开放点④定稿）

```cpp
#include <initguid.h>     // 必须在 wincodec.h 前——DEFINE_GUID 在本 TU 实例化
#include <wincodec.h>     // WIC 全套 + CLSID_WICImagingFactory + IID_PPV_ARGS
#include <shlwapi.h>      // SHCreateMemStream
```

- `initguid.h` 使本 TU 的 `CLSID_WICImagingFactory` 自带定义 → **零 uuid.lib 依赖**（MSVC/MinGW-w64 统一；开放点④收）
- IID 获取统一 `IID_PPV_ARGS(&ptr)`（两工具链头均支持）

### 3.2 ComRAII 定稿（开放点②收——轻量模板，不引 wrl）

```cpp
template <typename T>
class ComPtr {   // 最小可用集：Release 所有权语义（非完整智能指针）
	T* p = nullptr;
public:
	ComPtr() = default;
	~ComPtr() { if (p) p->Release(); }
	T** operator&() { return &p; }            // 仅用于 CoCreate/GetFrame 等出参
	T* operator->() const { return p; }
	T* Get() const { return p; }
	ComPtr(const ComPtr&) = delete;
	ComPtr& operator=(const ComPtr&) = delete;
};
```

- 命名避让：`ComPtr` 在匿名 namespace（不与 wrl/微软命名冲突——文件级可见）
- ComScope 按初设 v1.1 §4 修正版实施；**构造函数显式状态机**（评审建议——意图直观）：

```cpp
struct ComScope {
	bool usable = false;
	bool ownsInit = false;
	explicit ComScope() {
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (hr == S_OK || hr == S_FALSE) { usable = true; ownsInit = true; }   // 取得引用计数
		else if (hr == RPC_E_CHANGED_MODE) { usable = true; ownsInit = false; } // 已有他模式——可用不 Uninit
	}
	~ComScope() { if (ownsInit) CoUninitialize(); }
};
```

### 3.3 解码主流程（两入口汇合到单一 `DecodeImpl`）

```cpp
Image DecodeImpl(bool fromFile, const std::wstring& path, const std::uint8_t* data, std::size_t size)
```

- `DecodeMemory`：校验（size==0 / nullptr&&size>0 → 空+日志）→ `DecodeImpl(false, L"", data, size)`
- `DecodeFile`：`UTF8ToWide(utf8Path)` → `DecodeImpl(true, wide, nullptr, 0)`
- 步骤按初设 §3 表（3.0–3.9）实施；每步 HRESULT 失败 → `Logger::Log(LogLevel::Error, "ImageDecoder: <step> failed (hr=0x…)")` → `return {}`

### 3.4 溢出检查定稿（开放点⑤相关——纯数学界，无魔法数）

```cpp
// w/h 来自 WIC GetSize（UINT）——先限 int 域（Image 字段），再做分配数学
if (w == 0 || h == 0) return {};
if (w > INT_MAX || h > INT_MAX) return {};                    // Image 字段域
const std::uint64_t stride64 = static_cast<std::uint64_t>(w) * 4;
const std::uint64_t buffer64 = stride64 * h;
if (stride64 > INT_MAX || buffer64 > SIZE_MAX) return {};     // stride int 域 + 分配域
```

- 两步验证（width→stride、stride→bufferSize）——评审建议的结构化拆分 ✓
- uint64 中间量 + 边界比较——无 kMaxDim 魔法数；错误消息含 w/h 供诊断

### 3.5 内存流定稿（开放点①收）

- **主案**：`SHCreateMemStream(data, size)`（shlwapi）——MinGW-w64 头/lib 均含（实施时四工具链链接验证）
- **预案**（MinGW 链接失败时启用，骨架附文档附录）：只读 IStream 最小实现（QI/AddRef/Release/Read/Seek/Stat 六方法，~80 行）——**实施阶段按需启用，预先不写**

### 3.6 palette 定稿（开放点⑤收）

`WICBitmapPaletteTypeCustom` 保持——WIC 文档语义：非索引（32bpp）目标格式下 Initialize **忽略** palette 参数；Custom 即「不使用」的显式表达。MedianCut 仅对索引化输出有意义——不采用。

### 3.7 日志文案定稿（R6）

| 失败点 | Logger 消息 |
|---|---|
| COM init | `ImageDecoder: CoInitializeEx failed (hr=0x%08X)` |
| Factory | `ImageDecoder: create WIC factory failed (hr=0x%08X)` |
| Decoder | `ImageDecoder: create decoder failed (hr=0x%08X)`（文件不存在/格式不支持） |
| Frame | `ImageDecoder: GetFrame failed (hr=0x%08X)` |
| Size | `ImageDecoder: empty image (w=%u h=%u)` / `image too large (w=%u h=%u)` |
| Converter | `ImageDecoder: convert to 32bppPBGRA failed (hr=0x%08X)` |
| CopyPixels | `ImageDecoder: CopyPixels failed (hr=0x%08X)` |
| 输入校验 | `ImageDecoder: invalid input (null data / zero size)` |

（宽字符日志按 Logger 既有契约；%08X 由 HRESULT 格式化。）

## 4. 链接库定稿（开放点⑥收——§5）

```cmake
target_link_libraries(ECDI PUBLIC user32 imm32 msimg32 windowscodecs ole32 shlwapi)
```

- **不含 uuid.lib**（initguid 方案自给 CLSID——最小收敛 ✓）
- 以 linker 验证收尾：实施编译若报 unresolved → 按符号补；若无 → 维持最小集
- vcxproj 辅助工程 Link AdditionalDependencies 同步

## 5. 测试规格定稿（含资产生成策略——开放点③收）

### 5.1 PNG 资产生成（T1/T2）

实施时用 **Python 脚本现生成**（标准库 zlib+struct 手写 PNG chunk——无 PIL 依赖），输出 C++ hex 数组贴入测试源：

- T1：1×1 不透明白 PNG → BGRA 像素 = `FF FF FF FF`
- T2：2×2 PNG，像素含 R200/G100/B50/A128 → 断言内存序 `B'G'R'A ≈ 25/50/100/128`（premultiply 数值容差 ±1——WIC 内部舍入）
- 脚本存 `ECDI/src/Tests/assets/gen_test_assets.py`（生成过程可复现）

### 5.2 JPEG 资产（T3——开放点③收）

- 资产策略：实施时同样以脚本生成**最小基线 JFIF 灰度 8×8**（JPEG 熵编码手工构造复杂度高——降级预案：任取系统自带小 JPEG 转 hex 数组（如 `%WINDIR%\Web\Wallpaper` 小图裁切）——实施时择一，资产 bytes 硬编码进测试源）
- 断言：非空 + width/height/stride/pixels.size() 契约（有损不验像素——冻结）

### 5.3 用例清单（定稿 8 条）

T1 PNG 精确像素 / T2 半透明预乘 + 契约 / T3 JPEG 契约 / T4 非法字节 / T5 空输入×3 形态 / T6 文件不存在 / T7 DrawImage 集成（RecordingBackend 断言）/ T8 失败日志路径（Logger 断言或人工核）

## 6. CMake / vcxproj 规格

- CMakeLists：`target_link_libraries(ECDI PUBLIC user32 imm32 msimg32 windowscodecs ole32 shlwapi)`（追加）
- vcxproj：ClInclude 登记 `include\ECDI\Decode\ImageDecoder.h` + ClCompile `src\Platform\Win32\WicImageDecoder.cpp` + Link 库同步 + filters
- Tests：ImageDecodeTests.cpp + RunAllTests 登记

## 7. 验收清单

| # | 验收 | 方式 |
|---|---|---|
| 1 | 头计数 81、include/ 仍零平台泄漏 | `find include -name "*.h" \| wc -l` = 81（+version.h.in）+ 双 grep |
| 2 | 四工具链编译 | ECDI 库 + modelprobe ×4（WIC 链接在 MSVC/MinGW 均通） |
| 3 | 151+8 测试全绿 | ImageDecodeTests T1-T8 |
| 4 | selfcontain | 新头 81 TU 含 ImageDecoder.h 独立编译通过 |
| 5 | VS 不回归 | ECDI.exe 照常 |
| 6 | 消费验证 | ModelProbe 或 MinimalApp 解码一张真实 PNG 显示/断言 |

## 8. 实施顺序

1. 新建 `include/ECDI/Decode/ImageDecoder.h`（定稿头）+ `src/Platform/Win32/WicImageDecoder.cpp`（§3 规格）
2. CMake 链接追加 + vcxproj/filters 同步 + Tests 登记
3. 测试资产生成脚本运行 → hex 数组入 ImageDecodeTests.cpp
4. 本地编译回归（ECDI/modelprobe/VS）
5. selfcontain（81 TU）× 工具链
6. 用户四工具链 + 终验

## 9. 修订记录

- v1.0（2026-09-07）详细设计初稿：**6 开放点全收**（① 内存流主案 SH + IStream 预案 §3.5/§5；② ComRAII 轻量模板 + ComScope 显式状态机构造 §3.2；③ JPEG 资产生成策略 §5.2；④ initguid + IID_PPV_ARGS 零 uuid.lib §3.1；⑤ palette=Custom 定稿 §3.6；⑥ 链接最小集 windowscodecs/ole32/shlwapi §4）+ ComScope 显式状态机构造（评审建议）+ 溢出检查定稿（两步 uint64 数学界，无魔法数）+ 日志文案定稿表 + 测试资产生成策略（Python 手写 PNG chunk + JPEG 降级预案）+ 用例 8 条 + 验收 6 项 + 实施顺序 6 步。待评审。
