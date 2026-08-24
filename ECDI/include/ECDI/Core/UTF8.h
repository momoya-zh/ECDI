#pragma once

#include <cstddef>
#include <string>

namespace ECDI{

/// @brief UTF-8 编码与码点索引工具（5.5 P6：第二消费者必然出现——IME/剪贴板/多行）
/// @details UTF-8 变长：ASCII 1 字节 / 中文 3 字节 / emoji 4 字节——
/// 码点索引 ≠ 字节偏移，索引转换是 TextBox 光标/删除的正确性前提。

/// @brief 码点 → UTF-8 编码（返回 1-4 字节字符串）
/// @pre codepoint 合法（≤ 0x10FFFF 且非代理区 0xD800-0xDFFF）——调用方保证
///       （TextBox 输入来自 CharInputEvent，翻译器已组合合法码点）
std::string EncodeUTF8(char32_t codepoint);

/// @brief 码点索引 → 字节偏移（遍历跳过 index 个码点）
/// @return 对应字节偏移；index ≥ 码点数时返回 text.size()（自然钳制到末尾）
size_t CodepointIndexToByteOffset(const std::string& text, size_t codepointIndex);

/// @brief 字节偏移 → 码点索引（0 到 byteOffset 之间完整经过的码点数）
/// @pre byteOffset 必须位于 UTF-8 码点边界（非边界输入属未定义行为）
///      —— 鼠标点击定位可能产生非边界偏移，调用方需自行钳制
size_t ByteOffsetToCodepointIndex(const std::string& text, size_t byteOffset);

/// @brief 解码 UTF-8 首码点（8.5.2：RecalculateLines 行扫描 / GetWordBounds 分词共用——
/// 第二个消费者出现，YAGNI 解除正式加回；5.5.1.1 删 DecodeUTF8 时零消费者）
/// @param text 待解码字符串（空串 → 返回 0——调用方需先判空）
/// @return 首码点（按前导字节解码 1-4 字节；非法前导按 1 字节 ASCII 处理——与 SequenceLength 同语义）
char32_t DecodeFirstCodepoint(const std::string& text);

}
