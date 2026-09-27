#pragma once

#include "ECDI/Core/Image.h"

#include <windows.h>      // HICON（★ 仅内部头——平台层边界内，公共头不得出现）

namespace ECDI::Decode
{

/// @brief HICON → Image（内核；★ 不触 shell ⇒ 可无头测试）
/// @details 直接把内核做进平台层，与 shell 调用分离（Phase 21 初设 §2.5 / D3）。
///          - color 恒按 32bpp / top-down 读（实测 GetIconInfo 会归一化为 32bpp）
///          - ★ 不透明度：alpha 含非零值 ⇒ 用 alpha；**alpha 全为 0 ⇒ 用 mask**
///          - premultiply 在内部完成（Image 契约：premultiplied BGRA）
///          - 失败 ⇒ 空 Image + Logger Error；不抛异常
[[nodiscard]] Image ImageFromHIcon(HICON icon);

}
