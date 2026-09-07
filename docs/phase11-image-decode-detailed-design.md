# Phase 11 图片解码详细设计（v1.1）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-07
> 状态：**v1.1 外部评审通过（基本通过，修订 7 项——可进实施）**
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

### 3.1 include 策略与 CLSID 定义（开放点④定稿——v1.1 表述谨慎化）

```cpp
#include <windows.h>      // 必须最先（COM 基础类型）

#include <initguid.h>     // ⚠️ 必须在【首次包含声明相关 GUID 的 SDK 头】之前——不得依赖包含顺序碰巧成立
#include <wincodec.h>     // WIC 全套 + CLSID_WICImagingFactory + IID_PPV_ARGS
#include <shlwapi.h>      // SHCreateMemStream
```

- **谨慎表述（v1.1——评审修正）**：本 TU 在 wincodec.h 前包含 initguid.h，使其中 `DEFINE_GUID` 声明的 GUID 在本 TU **生成定义**，从而避免**当前 WIC CLSID/IID 使用路径**对 uuid.lib 的链接依赖；**最终是否需要额外 GUID 库仍以四工具链实际链接结果为准**（initguid 不是「所有 COM GUID 自动解决」的通用机制）
- IID 获取统一 `IID_PPV_ARGS(ReleaseAndGetAddressOf())`（两工具链头均支持）

### 3.2 ComRAII 定稿（开放点②收——轻量模板，不引 wrl）

```cpp
template <typename T>
class ComPtr {   // v1.1 定稿：ReleaseAndGetAddressOf 防覆盖泄漏 + 移动语义
	T* m_ptr = nullptr;
public:
	ComPtr() = default;
	~ComPtr() { Reset(); }
	ComPtr(const ComPtr&) = delete;
	ComPtr& operator=(const ComPtr&) = delete;
	ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
	ComPtr& operator=(ComPtr&& other) noexcept {
		if (this != &other) { Reset(); m_ptr = other.m_ptr; other.m_ptr = nullptr; }
		return *this;
	}
	T* Get() const noexcept { return m_ptr; }
	T* operator->() const noexcept { return m_ptr; }
	explicit operator bool() const noexcept { return m_ptr != nullptr; }
	void Reset() noexcept { if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; } }
	T** ReleaseAndGetAddressOf() noexcept { Reset(); return &m_ptr; }   // 出参专用——重取址先释放（防覆盖泄漏）
};
```

- **v1.1 修正（评审 🔴）**：删除裸 `operator&()`（对已持有对象的 ComPtr 重取址会覆盖旧指针致泄漏）→ `ReleaseAndGetAddressOf()`；补移动语义（RAII 完整性）
- **失败输出指针假设（文档明示）**：COM API 约定——成功返回有效指针，失败输出指针为 nullptr；若个别 API 失败仍写非空，失败路径显式 `Reset()` 兜底
- 调用形式：`CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()))`

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

### 3.3 解码主流程（v1.1 重构——三函数拆分，消除 bool 参数隐含约束）

```cpp
Image DecodeFileImpl(const std::wstring& widePath);              // 文件入口专用
Image DecodeMemoryImpl(const std::uint8_t* data, std::size_t size); // 内存入口专用
Image DecodeFromDecoder(IWICImagingFactory* factory, IWICBitmapDecoder* decoder);  // 共同后段（Frame→Converter→CopyPixels→Image）
```

- `DecodeMemory`：输入校验 → `DecodeMemoryImpl`
- `DecodeFile`：`UTF8ToWide(utf8Path)` → `DecodeFileImpl`
- 两个 Impl 各自负责：ComScope → factory → decoder 创建；然后**汇合到 `DecodeFromDecoder`**（Frame → Converter → CopyPixels → Image）——无 `bool fromFile` 隐含参数约束，factory 创建逻辑不重复
- 步骤按初设 §3 表（3.0–3.9）实施；每步 HRESULT 失败 → 日志（§3.7 方案 B）→ `return {}`

### 3.4 溢出与尺寸域检查定稿（v1.1 重构——int/UINT/size_t 三域显式）

```cpp
#include <limits>
constexpr std::uint64_t kIntMax  = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
constexpr std::uint64_t kUintMax = static_cast<std::uint64_t>(std::numeric_limits<UINT>::max());
constexpr std::uint64_t kSizeMax = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
constexpr std::uint64_t kMaxDecodedBytes = 256ull * 1024ull * 1024ull;   // 资源上限（256MB）——防恶意图片/解压炸弹，非溢出检查

// ① int 域（Image 字段）
if (w == 0 || h == 0) { LogEmptyImage(w, h); return {}; }
if (w > kIntMax || h > kIntMax) { LogImageTooLarge(w, h); return {}; }
// ② width → stride（uint64 中间量）
const std::uint64_t stride64 = static_cast<std::uint64_t>(w) * 4u;
if (stride64 > kIntMax) { LogImageTooLarge(w, h); return {}; }           // Image::stride 是 int
// ③ stride → bufferSize
const std::uint64_t buffer64 = stride64 * h;
if (buffer64 > kUintMax) { LogImageTooLarge(w, h); return {}; }          // CopyPixels cbBufferSize 是 UINT——仅 SIZE_MAX 不够
if (buffer64 > kSizeMax) { LogImageTooLarge(w, h); return {}; }
if (buffer64 > kMaxDecodedBytes) { LogImageTooLarge(w, h); return {}; }  // 资源上限
// ④ 显式转换（不依赖隐式）
const UINT stride = static_cast<UINT>(stride64);
const UINT bufferSize = static_cast<UINT>(buffer64);
```

- 四域检查链：int（Image 字段）→ stride int → buffer UINT（**CopyPixels 参数域**——评审 🔴：仅 SIZE_MAX 不够，vector 容纳得了但 WIC 参数表示不了）→ size_t → **kMaxDecodedBytes 资源上限**
- 显式 `static_cast`（不依赖隐式转换）；`Image{static_cast<int>(w), static_cast<int>(h), static_cast<int>(stride), std::move(pixels)}` 同样显式

### 3.5 内存流定稿（开放点①收）

- **主案**：`SHCreateMemStream(data, size)`（shlwapi）——MinGW-w64 头/lib 均含（实施时四工具链链接验证）
- **预案**（MinGW 链接失败时启用，骨架附文档附录）：只读 IStream 最小实现（QI/AddRef/Release/Read/Seek/Stat 六方法，~80 行）——**实施阶段按需启用，预先不写**

### 3.6 palette 定稿（开放点⑤收）

`WICBitmapPaletteTypeCustom` 保持——WIC 文档语义：非索引（32bpp）目标格式下 Initialize **忽略** palette 参数；Custom 即「不使用」的显式表达。MedianCut 仅对索引化输出有意义——不采用。

### 3.7 日志文案定稿（R6——v1.1 定方案 B）

**Logger 契约勘察**：`static void Log(LogLevel, std::wstring_view)`——纯文本宽串，**无格式化**。→ **方案 B**：实现内辅助函数组装：

```cpp
void LogDecoderError(const wchar_t* step, HRESULT hr) {
	wchar_t buf[128];
	std::swprintf(buf, 128, L"ImageDecoder: %s failed (hr=0x%08lX)", step, static_cast<unsigned long>(hr));
	Logger::Log(LogLevel::Error, std::wstring_view{buf});
}
```

| 失败点 | step 文案 |
|---|---|
| COM init | `CoInitializeEx` |
| Factory | `create WIC factory` |
| Decoder | `create decoder`（文件不存在/格式不支持） |
| Frame | `GetFrame` |
| Size | `empty image (w=%u h=%u)` / `image too large (w=%u h=%u)`（同法组装） |
| Converter | `convert to 32bppPBGRA` |
| CopyPixels | `CopyPixels` |
| 输入校验 | `invalid input (null data / zero size)` |

（不为 Phase 11 顺手改 Logger——用既有契约。）

## 4. 链接库定稿（开放点⑥收——v1.1 分组注释）

```cmake
# Existing Win32 / rendering dependencies
target_link_libraries(ECDI PUBLIC user32 imm32 msimg32)
# Phase 11 image decoding dependencies（组内以实际符号收敛——四工具链 linker 验证收尾）
target_link_libraries(ECDI PUBLIC windowscodecs ole32 shlwapi)
```

- **不含 uuid.lib**（initguid 方案——仍以四工具链实际链接结果为准，不凭文档绝对保证）
- 分组注释：旧模块移除时不会误以为它是 WIC 依赖
- vcxproj 辅助工程 Link AdditionalDependencies 同步

## 5. 测试规格定稿（含资产生成策略——开放点③收）

### 5.1 PNG 资产生成（T1/T2）

实施时用 **Python 脚本现生成**（标准库 zlib+struct 手写 PNG chunk——无 PIL 依赖），输出 C++ hex 数组贴入测试源：

- T1：1×1 不透明白 PNG → BGRA 像素 = `FF FF FF FF`
- T2：2×2 PNG，像素含 R200/G100/B50/A128 → 断言内存序 `B'G'R'A ≈ 25/50/100/128`（premultiply 数值容差 ±1——WIC 内部舍入）
- 脚本存 `ECDI/src/Tests/assets/gen_test_assets.py`（生成过程可复现）

### 5.2 JPEG 资产（T3——v1.1 策略升级：**已知有效 JPEG → 二进制转 hex**（评审 🔴：手工构造最小 JFIF 不合理——SOI/DQT/SOF0/DHT/SOS/熵编码/EOI 等于自己实现 JPEG 编码器））

- **主案**：取一个**已知有效的小 JPEG 文件**（系统自带 `%WINDIR%\Web\...` 小图或任何稳定来源），**脚本只负责二进制 → hex 数组转换**（不实现 JPEG 编码），结果 `constexpr std::uint8_t kJpegData[] = {...}` 固定进仓库
- 断言：非空 + width/height/stride/pixels.size() 契约（有损不验像素——冻结）
- 测试运行时不依赖外部文件（bytes 内嵌）

### 5.3 用例清单（定稿 8 条）

T1 PNG 精确像素 + 契约 / T2 半透明预乘 + 契约 / T3 JPEG 契约 / T4 非法字节 / T5 空输入×3 形态 / T6 文件不存在 / T7 DrawImage 集成（RecordingBackend 断言）/ T8 失败日志路径（**半自动验收——不纳入全自动「全绿」计数**，除非 Logger 支持捕获断言）

## 6. CMake / vcxproj 规格

- CMakeLists：`target_link_libraries(ECDI PUBLIC user32 imm32 msimg32 windowscodecs ole32 shlwapi)`（追加）
- vcxproj：ClInclude 登记 `include\ECDI\Decode\ImageDecoder.h` + ClCompile `src\Platform\Win32\WicImageDecoder.cpp` + Link 库同步 + filters
- Tests：ImageDecodeTests.cpp + RunAllTests 登记

## 7. 验收清单

| # | 验收 | 方式 |
|---|---|---|
| 1 | 头计数（**v1.1 计数规则明确**）：Public headers = **81**（含 ImageDecoder.h）；Internal headers = 9；`version.h.in` 为模板输入文件**不计入** Public 头计数（生成物 version.h 安装后才属 Public） | `find ECDI/include -name "*.h" \| wc -l` = 81 + 双 grep 零平台泄漏 |
| 2 | 四工具链编译 | ECDI 库 + modelprobe ×4（WIC 链接在 MSVC/MinGW 均通） |
| 3 | **151+7 测试全绿**（T8 半自动不计入） | ImageDecodeTests T1-T7 自动 + T8 半自动人工核 |
| 4 | selfcontain | 新头 81 TU 含 ImageDecoder.h 独立编译通过 |
| 5 | VS 不回归 | ECDI.exe 照常 |
| 6 | 消费验证 | ModelProbe 或 MinimalApp 解码一张真实 PNG 显示/断言 |

## 8. 实施顺序

1. 新建 `include/ECDI/Decode/ImageDecoder.h`（定稿头）+ `src/Platform/Win32/WicImageDecoder.cpp`（§3 规格）
2. CMake 链接追加 + vcxproj/filters 同步 + Tests 登记
3. 测试资产生成（PNG 脚本生成 + JPEG 来源转 hex）→ 数组入 ImageDecodeTests.cpp
4. 本地编译回归（ECDI/modelprobe/VS）+ **链接库最小集 linker 验证**
5. selfcontain（81 TU）× 工具链
6. 四工具链 + 终验

## 9. 修订记录

- v1.1（2026-09-07）**外部评审：基本通过，允许进入实施——实施前修订 7 项全采纳**：① 🔴 **ComPtr 定稿 ReleaseAndGetAddressOf()**（删裸 operator&——对已持有对象重取址覆盖旧指针致泄漏）+ 移动语义 + 失败输出指针假设明示；② 🔴 **CopyPixels UINT 参数域检查**（cbStride/cbBufferSize 是 UINT——仅 SIZE_MAX 不够，加 kUintMax 域）；③ 🔴 **int/UINT/size_t 三尺寸域显式转换**（constexpr kIntMax/kUintMax/kSizeMax + 显式 static_cast，Image 构造显式转换）；④ 🔴 **JPEG 资产策略升级**——「已知有效 JPEG → 脚本只做二进制转 hex」升主案（手工构造最小 JFIF = 自己实现 JPEG 编码器，不合理）；⑤ **Logger 契约勘察定稿方案 B**——`Log(LogLevel, wstring_view)` 无格式化 → 实现 `LogDecoderError(step, hr)` swprintf 组装（不顺手改 Logger）；⑥ **DecodeImpl 拆三函数**（DecodeFileImpl/DecodeMemoryImpl/DecodeFromDecoder 共同后段——消除 bool 隐含约束 + factory 创建不重复）；⑦ **计数规则明确**（Public 81 / Internal 9 / version.h.in 模板不计入）+ **kMaxDecodedBytes 256MB 资源上限**（防恶意图片/解压炸弹——独立于溢出检查）+ **T8 标半自动**（不纳入全自动全绿计数）+ **initguid 表述谨慎化**（完整 include 顺序 + 以链接结果为准）+ 链接库分组注释。**状态：评审通过——可进实施**。
- v1.0（2026-09-07）详细设计初稿：**6 开放点全收**（① 内存流主案 SH + IStream 预案 §3.5/§5；② ComRAII 轻量模板 + ComScope 显式状态机构造 §3.2；③ JPEG 资产生成策略 §5.2；④ initguid + IID_PPV_ARGS 零 uuid.lib §3.1；⑤ palette=Custom 定稿 §3.6；⑥ 链接最小集 windowscodecs/ole32/shlwapi §4）+ ComScope 显式状态机构造（评审建议）+ 溢出检查定稿（两步 uint64 数学界，无魔法数）+ 日志文案定稿表 + 测试资产生成策略（Python 手写 PNG chunk + JPEG 降级预案）+ 用例 8 条 + 验收 6 项 + 实施顺序 6 步。待评审。
