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
/// ★ Phase 39：`RecalculateLines` 非折行支改用 `DecodeCodepointAt`（就地解码 + 步进量）；`DecodeFirstCodepoint` 保留给按子串取码点的调用点
char32_t DecodeFirstCodepoint(const std::string& text);

/// @brief 就地解码 `data` 处的一个码点，并返回它**声称**占用的字节数（★ Phase 39）
/// @details 与 `DecodeFirstCodepoint` **同语义**（同一 `SequenceLength`、同一三分支拼装），差别只有两点：
///          ① 不要求「以串尾为界」——`size` 由调用方给（单趟扫描给的是**剩余长度**）；
///          ② **同时给出步进量**——`TextBox::RecalculateLines` 的非折行支既要解码值（判 `\n`）
///             又要步进量，只给解码值会逼调用方自己判前导字节（`SequenceLength` 立刻双源）。
///          ★ 返回值**可能 > `size`**：末段截断的序列仍按前导声称的长度步进——这是
///             `CodepointIndexToByteOffset` 的既有行为，单趟扫描必须逐位复刻。
///          ★ **`codepoint` 任何情况下都会被写**（`size == 0` 写 0）——调用方不必预置初值。
/// @param data  首个字节（`size == 0` 时**不读**）
/// @param size  剩余可读字节数
/// @param codepoint [out] 解码结果；**剩余不足一个完整序列 ⇒ 回退为 `lead`**
/// @return 前导声称的序列长度（1–4）；非法前导 = 1（与 `SequenceLength` 一致）
[[nodiscard]] size_t DecodeCodepointAt(const char* data, size_t size, char32_t& codepoint);
}
