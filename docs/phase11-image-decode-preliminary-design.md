# Phase 11 图片解码初步设计（v1.0）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-06
> 状态：待评审
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
| 3.1 | COM 初始化 | `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED \| COINIT_DISABLE_OLE1DDE)` | S_FALSE/RPC_E_CHANGED_MODE 视为成功继续；其余 → 空 Image |
| 3.2 | 工厂创建 | `CoCreateInstance(CLSID_WICImagingFactory, ..., IID_IWICImagingFactory)` | 同上 |
| 3.3 | 解码器创建 | 文件路：`CreateDecoderFromFilename(widePath, nullptr, GENERIC_READ, METADATACACHE_ONDEMAND)`；内存路：`SHCreateMemStream(data, size)` → `CreateDecoderFromStream` | 失败 → 「无法创建解码器（格式不支持/文件不存在）」 |
| 3.4 | 帧获取 | `decoder->GetFrame(0, &frame)` | GIF 首帧即 frame 0 |
| 3.5 | 尺寸读取 | `frame->GetSize(&w, &h)` | w/h==0 → 空 Image |
| 3.6 | 格式转换器 | `factory->CreateFormatConverter()` → `Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)` | 源格式不可转 PBGRA → 失败路径 |
| 3.7 | 像素拷贝 | `converter->CopyPixels(nullptr, stride = w*4, w*stride, pixels.data())` | **直灌**——PBGRA 与 Image 同构（§R5） |
| 3.8 | RAII 收尾 | 局部 `struct ComScope` 析构 `CoUninitialize()`；WIC 接口指针 ComPtr/手动 Release | — |
| 3.9 | 返回 | `Image{w, h, stride, std::move(pixels)}` | — |

### 3.8 错误点映射表（全部走 R6 契约）

每个失败点 → `Logger::Log(LogLevel::Error, "ImageDecoder: <步骤> failed (hr=0x...)")` → `return {}`。无异常路径。

## 4. R3：COM RAII 包装（per-call——评审冻结）

```cpp
namespace {
struct ComScope {
	bool ok = false;
	ComScope() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
	             || hr == S_FALSE || hr == RPC_E_CHANGED_MODE; }   // 已初始化也继续
	~ComScope() { if (ok) CoUninitialize(); }
};
}
```

- **无静态/全局 COM 对象**（评审冻结：apartment 归属坑避让）
- WIC 接口指针用局部 `Microsoft::WRL::ComPtr`？——**否：手动 Release 或最小 RAII**（不引 wrl 头——保持依赖面最小；详设定稿实现风格）

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

## 7. 测试方向（ImageDecodeTests.cpp——新增）

| 用例 | 资产 | 断言 |
|---|---|---|
| T1 最小 PNG 解码 | 硬编码 1×1 PNG bytes（67B hex 数组——IHDR/IDAT/IEND） | width/height/stride + 像素 == 预期 BGRA |
| T2 半透明预乘验证 | 2×2 PNG（含 alpha=128 像素） | premultiply 值正确（R/B 已乘 alpha） |
| T3 JPEG 解码 | 硬编码最小 JPEG（或跳过——JPEG 最小编码复杂，详设定） | 有损断言放宽（仅尺寸） |
| T4 非法字节 | `"not an image"` 字符串 | 空 Image + 无崩溃 |
| T5 空输入 | `DecodeMemory(nullptr, 0)` | 空 Image |
| T6 文件不存在 | `DecodeFile("Z:/nonexistent.png")` | 空 Image |
| T7 DrawImage 集成 | T1 结果 → PaintContext DrawImage → RecordingBackend 断言 DrawImageCommand 参数 | 管线贯通 |

## 8. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI/Decode/ | 新建 ImageDecoder.h（Public——81 头） |
| src/Platform/Win32/ | 新建 WicImageDecoder.cpp（Internal） |
| CMakeLists | PUBLIC 链接追加（windowscodecs/uuid/ole32[/shlwapi]） |
| ECDI.vcxproj | ClInclude 登记 + 链接库同步（辅助工程） |
| Tests | ImageDecodeTests.cpp + RunAllTests 登记 |

## 9. 开放决策点（归详设）

1. `SHCreateMemStream` MinGW 可用性 → 定内存流方案（§6——详设实测）
2. WIC 接口指针管理风格：手动 Release vs 最小 RAII 模板（§4——不引 wrl）
3. JPEG 测试资产：硬编码最小 JPEG vs 跳过 JPEG 用例（§7 T3）
4. `uuid.lib` vs `__uuidof`：跨工具链写法统一（§5——MinGW 对 CLSID 常量的链接差异）

## 10. 修订记录

- v1.0（2026-09-06）初步设计初稿：**头全文草案**（80+1=81 头）+ **WIC 管线九步分解**（COM→Factory→Decoder→Frame→Converter PBGRA→CopyPixels 直灌）+ COM RAII 包装（无静态 COM 对象）+ **链接库传播分析**（windowscodecs/uuid/ole32[/shlwapi] 全 PUBLIC——静态库消费者链接器解析需要）+ 内存流倾向（SH，MinGW 可用性定方案）+ 测试方向 7 用例（T1 硬编码 1×1 PNG）+ 影响面 + 4 开放点。待评审。
