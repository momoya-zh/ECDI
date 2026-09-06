# Phase 11 图片解码需求确认（v1.0）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-06
> 状态：待评审
> 前置：Phase 10 库化 ✅（80 Public 头 / 依赖方向单向律 / install-export 闭环）
> 三项立项决策（已拍板）：**WIC（COM）** 解码方案 + **新模块 Decode/** + **仅解码 API**（不含显示控件）
> 目标：补上「文件 → Image」的入口——`Image.h` 注释预留的「文件格式加载属未来 ImageLoader」正式兑现

---

## 1. 背景与现状事实

| 项 | 现状 |
|---|---|
| `Core/Image.h` | 已解码数据容器：32bpp **premultiplied BGRA** + top-to-bottom + stride 对齐 + 值语义（vector）——注释明示「文件格式加载属未来 ImageLoader」 |
| DrawImage 管线 | ✅ Phase 8 完整（预乘 BGRA + AC_SRC_ALPHA + 顶降 DIB）——只差解码入口 |
| COM 现状 | 框架**零 CoInitialize**（grep 实证）——WIC 引入需自管 COM 生命周期（§R3） |
| 解码方案 | **WIC（COM）**——系统自带；`GUID_WICPixelFormat32bppPBGRA` 直接输出预乘 BGRA，**与 Image 格式零像素转换** |
| 模块位置 | **Decode/ 新模块**（Public 抽象）——WIC 实现含平台代码，按依赖方向单向律下沉 src/ |
| 范围 | **仅解码 API**（不含 ImageWidget 显示控件——二次需求出现另立） |

**方案对比存档（立项依据）**：stb_image vendored（零平台依赖但引 vendored 代码）＞ 手写 PNG（zlib 级工作量，YAGNI）＞ GDI+（原则排除）——最终选 WIC：系统自带、格式覆盖最全（含 TIFF/HD Photo）、PBGRA 直出零转换。

## 2. 需求条目

### R1：Public API——`include/ECDI/Decode/ImageDecoder.h`

无状态静态函数 API（**不做接口 + unique_ptr 工厂**——解码无状态、值语义返回，与 ChildProcess 的有状态实例模式区分）：

```cpp
namespace ECDI::Decode
{
/// @brief 从内存字节流解码图像 → Image（premultiplied BGRA / top-down / stride 对齐）
/// @return 成功：填充的 Image；失败：空 Image（width==0）+ Logger::Log Error
[[nodiscard]] Image DecodeMemory(const std::uint8_t* data, std::size_t size);

/// @brief 从文件解码（路径 UTF-8，内部 UTF8ToWide 转换——Core/String 既有契约）
[[nodiscard]] Image DecodeFile(const std::string& utf8Path);
}
```

### R2：WIC 实现（Internal——依赖方向单向律下位置决策点 §3.1）

- 实现 cpp 含平台代码（COM/WIC/Windows.h）——**下沉 src/**，Public 头零平台依赖
- 位置候选：`src/Decode/WicImageDecoder.cpp`（与 Public 模块镜像）vs `src/Platform/Win32/WicImageDecoder.cpp`（平台实现惯例）——§3.1
- WIC 管线：`CoCreateInstance(CLSID_WICImagingFactory)` → `CreateDecoderFromFilename/FromStream`（内存流：`SHCreateMemStream`）→ `GetFrame(0)` → `IWICFormatConverter::Initialize(GUID_WICPixelFormat32bppPBGRA)` → `CopyPixels(stride=width*4)` → 填入 `Image`
- **GIF 取首帧**（动图多帧非目标）

### R3：COM 生命周期（决策点 §3.2）

框架零 CoInitialize——WIC 实现**自管**：decode 入口 `CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`，容错已初始化（S_FALSE/RPC_E_CHANGED_MODE 均继续），配对 CoUninitialize；工厂实例 per-call 或缓存——初设定

### R4：格式范围

WIC 系统解码器全量接受（BMP/PNG/JPEG/GIF/TIFF/ICO/HD Photo），**不白名单**——WIC 按文件嗅探自动选解码器；不支持格式 → 解码失败路径（空 Image + Error 日志）。文档标注「实际可用格式随 Windows 版本」

### R5：像素契约（零转换红利）

- WIC `32bppPBGRA` = premultiplied BGRA，**与 Image::pixels 逐字节同构**
- stride = `width * 4`（Image 契约 ≥ width*4，直接取等）
- 行序 top-down 双方一致——**CopyPixels 直灌 pixels，无任何像素级处理**

### R6：错误处理契约

失败一律：空 `Image{}`（width==0——现有 DrawImage 对空图不绘制，天然安全）+ `Logger::Log(LogLevel::Error, ...)`（HRESULT/格式名入日志）。**不抛异常**（与框架 Logger 终止/非终止策略一致——解码失败是可恢复运行时事件）

### R7：文件路径编码

`DecodeFile(const std::string&)`——UTF-8 入参，内部 `UTF8ToWide`（Core/String 既有契约——与框架公共 API UTF-8 边界一致）

## 3. 开放决策点

1. **WIC 实现的 src 位置**：`src/Decode/`（镜像 Public 模块）vs `src/Platform/Win32/`（平台实现惯例）——倾向后者（WIC 是 Windows 组件，Platform/Win32 语义精确）
2. **COM 生命周期粒度**：per-call init/uninit（简单、线程安全）vs 工厂缓存（省每次 CoCreate 开销）——倾向初设按「per-call + 工厂静态缓存」混合
3. **API 形态复核**：静态函数（本方案）vs 接口工厂（ChildProcess 同构）——无状态解码倾向静态函数，评审确认
4. **库化边界的 install 自包含**：ImageDecoder 全在库内，无新 install 项——确认无遗漏

## 4. 非目标（YAGNI 圈定）

- **PNG/JPEG 编码器**（Image → 文件写出——需求出现另立）
- 动图多帧（GIF 动画播放——只取首帧）
- EXIF 方向矫正、ICC 色彩管理、元数据（元数据读取 future）
- **ImageWidget / 图片显示控件**（已拍板出范围）
- 渐进式/流式解码（全量 buffer 进出）
- 跨平台解码后端（WIC 绑定 Windows——未来 Linux 立项时经抽象接口替换，本期不做接口化）

## 5. 测试/验证方向

- 测试资产：硬编码最小 PNG/JPEG bytes（1×1 / 2×2 已知像素）进测试源（零文件依赖）
- 断言：解码后 width/height/stride/关键像素 premultiply 值（含半透明像素的预乘验证）
- 失败路径：非法 bytes → 空 Image + 日志
- 渲染验收：解码图经 DrawImage 渲染（RecordingBackend 命令断言或 ModelProbe 界面消费）
- 四工具链 + VS（WIC 为系统组件，工具链间无差异预期）

## 6. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI/Decode/ | 新建（ImageDecoder.h——Public） |
| src/ | WIC 实现 cpp（位置 §3.1）——link 需增 `windowscodecs.lib` + `uuid.lib`（CLSID） |
| CMakeLists / vcxproj | 链接库补充；Public 头 ClInclude 登记 |
| Tests | 新增 ImageDecodeTests.cpp（硬编码资产） |
| README/索引 | Phase 11 登记表 |

## 7. 修订记录

- v1.0（2026-09-06）需求确认初稿：现状勘察（Image.h 留口实证/零 COM/DrawImage 就绪）+ R1-R7（静态函数 API / WIC 管线 / COM 自管 / 格式全量 / PBGRA 零转换契约 / 失败=空图+日志 / UTF-8 路径）+ §3 四决策点（src 位置/COM 粒度/API 形态/install 自包含）+ §4 非目标（编码器/动图/EXIF/控件/流式）+ §5 测试方向 + 影响面。待评审。
