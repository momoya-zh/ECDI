#include "ECDI/Core/UTF8.h"

namespace ECDI{

namespace{   // 匿名 namespace：内部辅助（不暴露）

/// @brief 按前导字节判定 UTF-8 序列长度（非法前导按 1 处理——避免遍历死循环）
/// @param lead unsigned char：char 在 MSVC 有符号，0xF0 等高位字节会变负、整数提升隐患
size_t SequenceLength(unsigned char lead) noexcept{
	if ((lead & 0x80) == 0)      return 1;
	if ((lead & 0xE0) == 0xC0)   return 2;
	if ((lead & 0xF0) == 0xE0)   return 3;
	if ((lead & 0xF8) == 0xF0)   return 4;
	return 1;   // 连续字节 10xxxxxx / 5-6 字节 11111xxx：非法，按 1 跳过
}

}

std::string EncodeUTF8(char32_t codepoint){
	if (codepoint <= 0x7F)
		return std::string(1, static_cast<char>(codepoint));
	if (codepoint <= 0x7FF)
		return { static_cast<char>(0xC0 | (codepoint >> 6)),
		         static_cast<char>(0x80 | (codepoint & 0x3F)) };
	if (codepoint <= 0xFFFF)
		return { static_cast<char>(0xE0 | (codepoint >> 12)),
		         static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
		         static_cast<char>(0x80 | (codepoint & 0x3F)) };
	return { static_cast<char>(0xF0 | (codepoint >> 18)),
	         static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)),
	         static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
	         static_cast<char>(0x80 | (codepoint & 0x3F)) };
}

size_t CodepointIndexToByteOffset(const std::string& text, size_t codepointIndex){
	size_t byteOffset = 0;
	size_t cpIndex = 0;
	while (byteOffset < text.size() && cpIndex < codepointIndex){
		byteOffset += SequenceLength(static_cast<unsigned char>(text[byteOffset]));
		++cpIndex;
	}
	return byteOffset;   // index 超界 → 停在 text.size()（自然钳制）
}

size_t ByteOffsetToCodepointIndex(const std::string& text, size_t byteOffset){
	size_t cpIndex = 0;
	size_t pos = 0;
	while (pos < text.size() && pos < byteOffset){
		pos += SequenceLength(static_cast<unsigned char>(text[pos]));
		++cpIndex;
	}
	return cpIndex;
}

char32_t DecodeFirstCodepoint(const std::string& text){
	if (text.empty())
		return 0;
	// 按前导字节解码（与 SequenceLength 同规则——非法前导按 1 字节 ASCII 处理）
	const unsigned char lead = static_cast<unsigned char>(text[0]);
	if ((lead & 0x80) == 0)
		return static_cast<char32_t>(lead);   // 1 字节 ASCII
	const size_t len = SequenceLength(lead);
	if (len == 2 && text.size() >= 2){
		return static_cast<char32_t>(((lead & 0x1F) << 6) |
			(static_cast<unsigned char>(text[1]) & 0x3F));
	}
	if (len == 3 && text.size() >= 3){
		return static_cast<char32_t>(((lead & 0x0F) << 12) |
			((static_cast<unsigned char>(text[1]) & 0x3F) << 6) |
			(static_cast<unsigned char>(text[2]) & 0x3F));
	}
	if (len == 4 && text.size() >= 4){
		return static_cast<char32_t>(((lead & 0x07) << 18) |
			((static_cast<unsigned char>(text[1]) & 0x3F) << 12) |
			((static_cast<unsigned char>(text[2]) & 0x3F) << 6) |
			(static_cast<unsigned char>(text[3]) & 0x3F));
	}
	// 连续字节/非法前导/序列不完整：按 1 字节 ASCII 处理（与 SequenceLength 同语义）
	return static_cast<char32_t>(lead);
}

}
