# Phase 11 图片解码初步设计（v1.0）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-06
> 状态：**v1.1 外部评审通过（修改后通过——可进详设）**
> 前置：phase11-image-decode-requirements.md **v1.1**（评审通过，决策点全收敛）
> 一句话：把 WIC 决策落成「Public 头全文草案 + WIC 管线九步分解 + 链接库传播分析 + 测试方向」——详设收工程细节

---

## 1. 范围映射（R → 设计域）

| 需求 | 设计域 | 本文档节 |
|---|---|---|
| R1 Public API（静态函数） | ImageDecoder.h 头全文草案 | §2 |
| R2 WIC 实现（src/Platform/Win32/） | 管线九步分解 | §3 |
| R3 COM 生命周期（per-call） | RAII 包装 | §4 |
| R5 PBGRA 零转换 | CopyPixels 直灌细节 | §3.7 |
| R6 失败契约 | 错误点映射表 | §3.8 |
| R7 UTF-8 路径 | DecodeFile 分支 | §3.9 |
| — 链接库传播 | windowscodecs/uuid PUBLIC 分析 | §5 |
| — 内存流 | SHCreateMemStream 倾向（详设定稿） | §6 |
| — 测试 | 硬编码资产方向 | §7 |

## 2. R1：`include/ECDI/Decode/ImageDecoder.h` 头全文草案

```cpp
﻿#pragma once

#include "ECDI/Core/Image.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ECDI::Decode
{

/// @brief 图片解码（Phase 11——WIC 后端，文件/内存 → Image）
/// @details 无状态自由函数 API：失败一律返回空 Image（width==0，DrawImage 天然跳过）
///          并经 Logger 记 Error；不抛异常。实际支持格式由系统 WIC 解码器决定
///          （BMP/PNG/JPEG/GIF/TIFF/ICO 等——ECDI 不对 WIC 可识别格式做独立兼容承诺）。
///          像素输出固定 premultiplied BGRA / top-down / stride = width*4（32bppPBGRA 直灌）。

/// @brief 从内存字节流解码图像
/// @param data  原始编码字节（PNG/JPEG/... 二进制内容，非像素数据）
/// @param size  字节数
/// @return 解码成功返回填充的 Image；失败返回空 Image
[[nodiscard]] Image DecodeMemory(const std::uint8_t* data, std::size_t size);

/// @brief 从文件解码
/// @param utf8Path 文件路径（UTF-8——框架公共 API 编码契约，内部 UTF8ToWide）
[[nodiscard]] Image DecodeFile(const std::string& utf8Path);

}
```

- include 最小化：`Core/Image.h`（类型）+ `<cstddef>/<string>`——**零 Windows/WIC 泄漏**（依赖方向单向律）
- 命名保留 `ImageDecoder.h`（评审已确认：表「Image 解码相关 API」而非「存在 ImageDecoder 类」）

## 3. R2：WIC 管线九步分解（`src/Platform/Win32/WicImageDecoder.cpp`）

| # | 步骤 | API | 失败处理 |
|---|---|---|---|
| 3.0 | **输入校验（v1.1 新增）** | `DecodeMemory`：`size == 0 → 失败`；`data == nullptr && size != 0 → 立即失败`（不交给 WIC/流层） | 空 Image + Error |
| 3.1 | COM 初始化 | `ComScope`（见 §4——S_OK/S_FALSE 取得引用计数；RPC_E_CHANGED_MODE 可用但不计数；其余失败） | 非 usable → 空 Image |
| 3.2 | 工厂创建 | `CoCreateInstance(CLSID_WICImagingFactory, ..., IID_IWICImagingFactory)` | 同上 |
| 3.3 | 解码器创建 | 文件路：`CreateDecoderFromFilename(widePath, nullptr, GENERIC_READ, METADATACACHE_ONDEMAND)`；内存路：`SHCreateMemStream(data, size)` → `CreateDecoderFromStream` | 失败 → 「无法创建解码器（格式不支持/文件不存在）」 |
| 3.4 | 帧获取 | `decoder->GetFrame(0, &frame)` | GIF 首帧即 frame 0 |
| 3.5 | 尺寸读取 + **溢出防护（v1.1 新增）** | `frame->GetSize(&w, &h)`；w/h==0 → 空；**乘法检查**：`w > kMaxDim \|\| h > kMaxDim \|\| w > SIZE_MAX/(4*h)` 等——防恶意/损坏图片触发异常大分配（`width*4`、`width*height*4` 均在分配**前**验证） | 超限 → 空 Image + Error |
| 3.6 | 格式转换器 | `factory->CreateFormatConverter()` → `Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)` | 源格式不可转 PBGRA → 失败路径 |
| 3.7 | 像素拷贝 | `converter->CopyPixels(nullptr, stride = w*4, w*h*stride, pixels.data())` | **直灌**——PBGRA 与 Image 同构（§R5） |
| 3.8 | RAII 收尾 | 局部 `ComScope`（v1.1 修正版——见 §4）；WIC 接口指针手动 Release | — |
| 3.9 | 返回 | `Image{w, h, stride, std::move(pixels)}` | — |

### 3.8 错误点映射表（全部走 R6 契约）

每个失败点 → `Logger::Log(LogLevel::Error, "ImageDecoder: <步骤> failed (hr=0x...)")` → `return {}`。无异常路径。

## 4. R3：COM RAII 包装（per-call——评审冻结；**v1.1 修正 ComScope 双状态 bug**）

```cpp
namespace {
/// @brief per-call COM 生命周期。
/// 关键语义（评审修正）：RPC_E_CHANGED_MODE 表示当前线程【已有】其他模式的 COM 初始化——
/// 本次调用【未取得】新的初始化引用计数，因此「可用但不得 CoUninitialize」（否则替别人撤销）。
/// 「可以继续执行」与「负责 Uninitialize」是两个独立状态。
struct ComScope {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool shouldUninitialize = (hr == S_OK || hr == S_FALSE);   // 仅这两种取得了引用计数

	bool Usable() const { return shouldUninitialize || hr == RPC_E_CHANGED_MODE; }
	~ComScope() { if (shouldUninitialize) CoUninitialize(); }
};
}
```

- 三态语义：`S_OK`（首次初始化）/ `S_FALSE`（已有同模式初始化，计数+1）/ `RPC_E_CHANGED_MODE`（模式冲突，**未取得计数——可用但不 Uninit**）；其余 → 不可用失败
- **无静态/全局 COM 对象**（评审冻结：apartment 归属坑避让）
- WIC 接口指针手动 Release 或最小 RAII（不引 wrl 头——保持依赖面最小；详设定稿实现风格）

## 5. 链接库传播分析（库化边界关键项）

| 库 | 引用位置 | 链接属性 | 理由 |
|---|---|---|---|
| `windowscodecs.lib` | WicImageDecoder.cpp | **PUBLIC** | 静态库不链接——消费者链接器需解析 ECDI.obj 里的 WIC 符号（同 user32 先例） |
| `uuid.lib` | CLSID_WICImagingFactory/IID_ | **PUBLIC** | 同上（或用 `__uuidof` 依赖 MSVC 特性——跨工具链 MinGW 差异，详设验证） |
| `shlwapi.lib`（若 SHCreateMemStream） | 内存流 | **PUBLIC** | 同上——详设定稿后定 |
| `ole32.lib` | CoInitializeEx/CoCreateInstance | **PUBLIC** | COM 基础（MinGW 下亦需） |

- CMake：`target_link_libraries(ECDI PUBLIC ... windowscodecs uuid ole32)`（追加既有 PUBLIC 组）
- vcxproj：Link AdditionalDependencies 同步（辅助工程）
- **MinGW 差异风险**：`SHCreateMemStream` 在 MinGW 头/lib 的可用性待验证——若不可用则自实现只读 IStream（详设评审项提前获得权重）

## 6. 内存流（R 详设评审项——初设倾向）

- **倾向 `SHCreateMemStream`**：一行调用、语义精确、shlwapi 系统自带
- 自实现只读 IStream（QueryInterface/AddRef/Read/Seek/Stat 五方法）约 80 行——仅在 MinGW SH 不可用时启用
- **详设定稿**：MinGW 工具链实测 SHCreateMemStream 可用性 → 定方案

## 7. 测试方向（ImageDecodeTests.cpp——新增；**v1.1 修订：JPEG 不跳过 + 契约断言并入**）

| 用例 | 资产 | 断言 |
|---|---|---|
| T1 最小 PNG 解码 | 硬编码 1×1 PNG bytes（67B hex 数组——IHDR/IDAT/IEND） | width/height/stride + 像素 == 预期 BGRA；**契约断言：stride==width*4、pixels.size()==height*stride、非空** |
| T2 半透明预乘验证（**本 Phase 最重要测试**） | 2×2 PNG（含 alpha=128 像素） | **P' = P×A/255**：原 R200/G100/B50/A128 → 内存序 B'G'R'A ≈ 25/50/100/128（同时验证 converter/PBGRA/预乘/BGRA 布局/Image 契约五件事）+ 契约断言同 T1 |
| T3 JPEG 解码（**v1.1 不跳过**） | 硬编码最小 JPEG bytes（constexpr 数组） | **有损——只断言非空 + width/height/stride/pixels.size() 契约**，不验具体像素 |
| T4 非法字节 | `"not an image"` 字符串 | 空 Image + 无崩溃 |
| T5 空输入（v1.1 细化） | `DecodeMemory(nullptr, 0)`、`DecodeMemory(valid, 0)`、`DecodeMemory(nullptr, 5)` | 三者均空 Image（**nullptr&&size>0 立即失败契约**） |
| T6 文件不存在 | `DecodeFile("Z:/nonexistent.png")` | 空 Image |
| T7 DrawImage 集成 | T1 结果 → PaintContext DrawImage → RecordingBackend 断言 DrawImageCommand 参数 | 管线贯通 |

## 8. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI/Decode/ | 新建 ImageDecoder.h（Public——81 头） |
| src/Platform/Win32/ | 新建 WicImageDecoder.cpp（Internal） |
| CMakeLists | PUBLIC 链接追加（windowscodecs/uuid/ole32[/shlwapi]——**以实际符号收敛最小**，详设编译验证） |
| ECDI.vcxproj | ClInclude 登记 + 链接库同步（辅助工程） |
| Tests | ImageDecodeTests.cpp + RunAllTests 登记 |

## 9. 开放决策点（归详设——v1.1 增补 2 项）

1. `SHCreateMemStream` MinGW 可用性 → 定内存流方案（§6——详设实测）
2. WIC 接口指针管理风格：手动 Release vs 最小 RAII 模板（§4——不引 wrl）
3. ~~JPEG 测试资产~~——**已冻结（v1.1）**：硬编码最小 JPEG bytes，不跳过
4. `uuid.lib` vs `__uuidof`：跨工具链写法统一（§5——MinGW 对 CLSID 常量的链接差异）
5. **palette 参数语义查证**（新增）：`WICBitmapPaletteTypeCustom` vs `MedianCut`——32bpp 非索引输出下 palette 基本无意义，详设查 WIC 文档选语义最明确值
6. **链接库最小收敛**（新增）：windowscodecs/uuid/ole32/shlwapi 以实际符号为准——详设编译一次后剔除未用库（不为「保险」长期悬挂）

## 10. 修订记录

- v1.1（2026-09-07）**外部评审通过（修改后通过——可进详设），5 必改全采纳**：① **§4 ComScope 双状态 bug 修正**（原草案 hr 未定义 + RPC_E_CHANGED_MODE 误调 CoUninitialize——会替别人撤销 COM 引用计数；修正为 `shouldUninitialize = hr==S_OK||hr==S_FALSE` 双状态 + `Usable()` 三态语义，注释明写「RPC_E_CHANGED_MODE 未取得计数——可用但不 Uninit」防误改）；② §2 头草案**显式加 `<cstdint>`**（`std::uint8_t` 不依赖 Image.h 间接包含——Phase 10 自包含原则）；③ §3 新增 **3.0 输入校验**（size==0 失败；nullptr&&size>0 立即失败——不交给 WIC）+ **3.5 溢出防护**（w/h 上限 + `width*4`/`width*height*4` 分配前乘法验证——防恶意图片异常大分配）；④ §7 **T3 JPEG 不跳过**（constexpr 最小 JPEG 资产；有损只断言契约不断像素）+ T1/T2 并入 Image 契约断言（stride/size/非空）+ T5 细化三输入；⑤ §9 开放点增补 2 项（palette 语义查证 / 链接库以实际符号收敛最小）。可保持项全数确认（WIC/位置/自由函数/无接口/per-call×2/SH 待实测/无白名单/PBGRA 直灌/GIF 首帧/空 Image+Logger/无控件）。
- v1.0（2026-09-06）初步设计初稿：**头全文草案**（80+1=81 头）+ **WIC 管线九步分解**（COM→Factory→Decoder→Frame→Converter PBGRA→CopyPixels 直灌）+ COM RAII 包装（无静态 COM 对象）+ **链接库传播分析**（windowscodecs/uuid/ole32[/shlwapi] 全 PUBLIC——静态库消费者链接器解析需要）+ 内存流倾向（SH，MinGW 可用性定方案）+ 测试方向 7 用例（T1 硬编码 1×1 PNG）+ 影响面 + 4 开放点。待评审。
