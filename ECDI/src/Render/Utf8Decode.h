#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ECDI{

/// @brief UTF-8 → 码点序列（★ Phase 26：测量链与渲染链**共用同一解码**）
/// @details 抽出的理由 = **第二个真实消费者**（Phase 26 批三）：测量侧（`FontEngine`）与
///          渲染侧（`GLRenderer::DrawText`）**必须按同一套码点切分**，否则「测出来的宽」
///          与「画出来的宽」会在非法/截断字节处失配（同源前提之一，详设 S6）。
///          与 `Core/String.h` 的 `UTF8ToWide` 分工不同：那个产出 **wchar_t**（Win32 边界），
///          本函数产出 **char32_t 码点**（度量 / 字形查找用）。
/// @return 码点序列；★ **非法首字节 / 续字节跳过、截断序列停止**——**不抛不崩**（沿
///         `FontEngine` 既有语义，保证两链一致）
inline std::vector<char32_t> DecodeUtf8(const std::string& utf8)
{
	std::vector<char32_t> out;
	out.reserve(utf8.size());

	std::size_t i = 0;
	while (i < utf8.size())
	{
		const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
		char32_t cp = 0;
		int len = 0;
		if (b0 < 0x80)              { cp = b0;         len = 1; }
		else if ((b0 >> 5) == 0x6)  { cp = b0 & 0x1F;  len = 2; }
		else if ((b0 >> 4) == 0xE)  { cp = b0 & 0x0F;  len = 3; }
		else if ((b0 >> 3) == 0x1E) { cp = b0 & 0x07;  len = 4; }
		else { ++i; continue; }    // 非法首字节 ⇒ 跳过

		if (i + static_cast<std::size_t>(len) > utf8.size())
		{
			break;                 // 截断的序列 ⇒ 停止
		}

		bool ok = true;
		for (int k = 1; k < len; ++k)
		{
			const unsigned char bk = static_cast<unsigned char>(utf8[i + static_cast<std::size_t>(k)]);
			if ((bk >> 6) != 0x2) { ok = false; break; }
			cp = (cp << 6) | (bk & 0x3F);
		}

		if (ok)
		{
			out.push_back(cp);
			i += static_cast<std::size_t>(len);
		}
		else
		{
			++i;
		}
	}
	return out;
}

}
