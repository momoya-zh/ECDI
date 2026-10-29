#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Core/UTF8.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/Render/PaintContext.h"
#include "Render/RecordingBackend.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "Render/GDIBackend.h"
#include "Render/GDITextMeasurer.h"

using namespace ECDI;

namespace {

constexpr float kEps = 0.001f;   // EXPECT_NEAR 精度（同 WidgetTests/ProgressBarTests）

/// @brief 可测 TextBox：暴露 protected 键盘选择路径（无窗口安全——SyncTextInputCaret 有 Window 防御）
/// @note CaretIndexFromX 为 private（点击定位算法），派生类不可访问——算法直测
/// 留待最小窗口集成测试（与拖选事件流同归；不为此改框架可见性——P0 边界修正，v0.2 实现记录）。
class FakeTextMeasurer : public TextMeasurer   // 9.8：确定性测量（每码点 8 宽/行高 16——与 WidgetTests 同构）
{
public:
	Size MeasureText(const Font&, const std::string& text) override{
		size_t count = 0;
		for (size_t i = 0; i < text.size(); ++i)
			if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
				++count;
		return Size{ static_cast<float>(count) * 8.0f, 16.0f };
	}
	float LineHeight(const Font&) override{ return 16.0f; }
};

class TestableTextBox : public TextBox
{
public:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;   ///< 暴露键盘选择路径（Shift+方向/Home/End）
    using TextBox::OnMouseButtonDown;   ///< scrollbar-placement-and-appearance T9：手势隔离守卫（protected override——无头验证）
    using TextBox::OnMouseMove;         ///< scrollbar-placement-and-appearance T9：拖选扩展（同上）
    using TextBox::OnFocusGained;       ///< ★ Phase 30 T30-1：置 m_showCaret=true（TextBox.cpp:212——窗口访问判空 ⇒ 无窗口/真窗口两可）
    using TextBox::GetTextTopInset;     ///< ★ Phase 37 T37-2：垂直对齐内缩（protected——using 暴露）

    /// @brief 文本内边距（protected `m_style` 的中转）——scrollbar-placement-and-appearance v1.5：条内缩 = max(padding, 2)
    [[nodiscard]] float Padding() const noexcept{ return m_style.padding.value; }

    /// @brief ★ 当前字体（`m_style` 的中转，Phase 38 批 4）
    /// @details T38-16② 要「按控件自己的字度量出行宽的一半」——`MeasureText` 的 Font 形参必须与绘制同源，
    ///   否则量的是另一套字宽，中点判据就没有意义（`TextWidget` 没有 public 的字体 getter，故在此中转）。
    /// @details ★ 必须写 `TextWidget::` 限定：`TextBox` 自己有同名 `m_style`（`TextBoxStyle`，无 font 字段）
    ///   遮蔽了基类的 `TextStyle` ⇒ 裸写 `m_style.font` 编译不过（本批实测报错，已订正）。
    [[nodiscard]] Font FontForTest() const noexcept{ return TextWidget::m_style.font.value; }

    /// @brief ★ 视觉行数（Phase 38 批 2）
    /// @details ★★ **为什么不直接 `m_lines.size()`**：`m_lines` 是 `TextBox` 的 **private** 成员
    ///   （行表数据私有是 D38-E 的一部分），派生类**不可访问** ⇒ 详设 §2.9① 那句
    ///   `return m_lines.size();` 编译不过（v1.3 订正，见文档 §9）。
    ///   改用 **public 面反推**：`p = 0` 且假测量器行高 16 ⇒ `preferred.height == ceil(n × 16)`
    ///   ⇒ `n = height / 16`。★ 这不是凑数——它顺带把「行表行数」与「preferred 高度」钉成一致，
    ///   两者若分叉本条就红（T38-12 也走同一路径）。
    [[nodiscard]] size_t RowCountForTest() const{
        return static_cast<size_t>(GetPreferredSize().height / 16.0f + 0.5f);
    }

    /// @brief 归属口径区间（自带重算——★ 漏调会读到上一轮的表 = 假绿的经典形态）
    [[nodiscard]] std::pair<size_t, size_t> OwnForTest(size_t i){ RecalculateLines(); return LineRange(i); }

    /// @brief 裁剪口径区间（同上）
    [[nodiscard]] std::pair<size_t, size_t> DrawForTest(size_t i){ RecalculateLines(); return LineDrawRange(i); }

    /// @brief ★ Phase 39：行表行数 = `m_lines.size()`（**唯一可用通道**）
    /// @details ★★ 为什么**不能**用 `RowCountForTest()`：它是从 preferred 高反推的，而
    ///   `TextBox::GetPreferredSize`（`TextBox.cpp:201`）只在「含字面 `\n` 或折行生效」时
    ///   才走 `MultilinePreferred()`（读 `m_lines.size()`）。Phase 39 的 overlong 样本
    ///   （`C0 8A` / `E0 80 8A` / `F0 80 80 8A`）**不含字面 0x0A 字节** ⇒ 非折行态读回
    ///   **1**、真值 **2** ⇒ 五条用例假红；若把 golden 迁就成 1，则 R39-3 的负对照
    ///   **永远不红**（本 Phase 唯一的假绿护栏当场作废）。
    /// @details 原理：Center 态下 `GetTextTopInset()`（protected，`TextBox.h:264`）在
    ///   `TextBox.cpp:611` **直接读 `m_lines.size()`**、且自带 `RecalculateLines()`
    ///   （`:607` 的 const_cast 调用）；无头行高兜底 16（`:589`）⇒
    ///   `n = (areaH − 2·(inset − padding)) / 16`。
    ///   ★ 前置：`n × 16 <= areaH`（内容高 > 文本区高时 `max(0, ·)` 把 offset 夹到 0、
    ///   行数信息消失）⇒ 调用方必须给足高度（表 B 样本统一高 200）。
    ///   ★ 用完**必须还原 Top**：`OnPaint` 的 `textY` 依赖 `GetTextTopInset()`，
    ///   同用例里若还要断言绘制坐标，Center 会让它整体下移。
    [[nodiscard]] size_t LineCountForTest(){
        TextBoxStyleOverride c;
        c.verticalAlign = VerticalAlignment::Center;
        SetStyle(c);                                     // Top 分支会早退、不碰行表
        const float inset = GetTextTopInset();           // 内含 RecalculateLines()
        const float areaH = static_cast<float>(GetHeight()) - Padding() * 2.0f;
        TextBoxStyleOverride t;
        t.verticalAlign = VerticalAlignment::Top;
        SetStyle(t);                                     // ★ 还原（不污染后续绘制断言）
        return static_cast<size_t>((areaH - 2.0f * (inset - Padding())) / 16.0f + 0.5f);
    }

    using TextBox::OnTimer;     ///< 8.5.1：光标闪烁（protected override——无窗口环境允许验证翻转逻辑）
    using TextBox::UpdateComposition;   ///< 8.5.1：Composition 状态机（protected——Window 转发入口）
    using TextBox::CommitComposition;
    using TextBox::CancelComposition;
    using TextBox::RecalculateLines;   ///< 8.5.2：行缓存重算（private——测试经 using 暴露）
    using TextBox::LineRange;          ///< 8.5.2：行区间查询（private——推断行分割）
    using TextBox::CaretIndexFromPosition;   ///< 8.5.2：多行坐标定位（private——Y 定行无窗口可测）
    using TextBox::GetWordBounds;      ///< 8.5.2：双击选词（private——纯码点逻辑）
    using TextBox::OnMouseWheel;       ///< 8.5.2：滚轮（protected override）
    // ── ★ Phase 38 批 2：折行两口径 + 折行判据（protected——using 暴露）──
    using TextBox::LineDrawRange;      ///< 裁剪口径（软断点行尾空格不发射）
    using TextBox::IsWrapActive;       ///< 折行生效（请求 ∧ 有测量器）
    using TextBox::WrapRequested;      ///< 折行请求（纯配置，noexcept）
    using TextBox::CaretIndexFromLineX;///< 行内 X → 全局码点（T38-18④ 的可达性判据）
protected:
    TextMeasurer* ResolveMeasurer() const override{ return &ms_fake; }   // 9.8：preferred 测量接缝
private:
    static FakeTextMeasurer ms_fake;
};

// ════════════════════════════════════════════════════════════════════════════
// ★ Phase 39 批 0：两张 golden 表（改前行为快照——批 1 改完必须仍绿）+ 两个 helper
// ════════════════════════════════════════════════════════════════════════════

/// @brief 表 A（Phase 39 批 0 采集）：默认 `FitText` 的等价性基线（A5）
/// @details ★ #5 组 `startCp != 0` 是**唯一能测到 R2 的组**：`startCp == 0` 时被外提的
///          `byteStart` 恒为 0，算一次与算两次结果相同 ⇒ 只用 `startCp == 0` 的样本，
///          R2 的等价性测不到任何东西（同型假绿，见 R39-3）。
/// @note 期望值 = 2026-10-29 手算（沿初设 §1.5）。★ **批 0 实跑若不符，以实测为准并回填**——
///       手算只是首次运行的对齐校验，不是真理（C38-5 教训）。
struct FitGold{
    const char* text;
    size_t      startCp;
    float       maxWidth;
    size_t      fitCp;
    float       width;
};

static const FitGold kFitGolden[] = {
    { "abcdef",   0, 100.0f, 6, 48.0f },   // 全部放得下
    { "abcdef",   2,  24.9f, 3, 24.0f },   // 非整档宽（防「整除」巧合）
    { "abcdef",   2,   7.9f, 0,  0.0f },   // 首码点都放不下
    { "一丁丂",    0,  16.0f, 2, 16.0f },   // 多字节 + 恰好边界
    { "一丁丂",    1, 100.0f, 2, 16.0f },   // ★ 关键组：startCp != 0
    { "abcdef",   6, 100.0f, 0,  0.0f },   // startCp >= totalCp ⇒ 早退
    { "",         0, 100.0f, 0,  0.0f },   // 空串
    { "\xC0\x8A", 0, 100.0f, 1,  8.0f },   // 脏字节（overlong U+000A，totalCp == 1）
};

/// @brief 表 B 的一行：两口径区间（own = 归属 / draw = 裁剪）
struct LineGoldRow{ size_t ownBeg, ownEnd, drawBeg, drawEnd; };

/// @brief 表 B（Phase 39 批 0 采集）：非折行支行表的改前快照
/// @details `lineCount` 字段 = **`m_lines.size()`**（经 `LineCountForTest()` 读），
///          **不是** `RowCountForTest()`——后者在 overlong 三行上读成 1（P0）。
///          `rows[3]` 用完的行留 `{0,0,0,0}`；本表最大行数 = 3（`"\n\n"`）。
struct LineGold{
    const char* text;
    size_t      lineCount;
    LineGoldRow rows[3];
};

static const LineGold kLineGolden[] = {
    // 类别                    行数  逐行 (ownBeg, ownEnd, drawBeg, drawEnd)
    { "",                 1, { { 0, 0, 0, 0 } } },                                  // 空串
    { "\n",               2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                   // 仅 \n
    { "\n\n",             3, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 } } },   // 连续 \n
    { "ab\n",             2, { { 0, 2, 0, 2 }, { 3, 3, 3, 3 } } },                   // 尾 \n（尾部空行）
    { "a\r\nb",           2, { { 0, 2, 0, 2 }, { 3, 4, 3, 4 } } },                   // CRLF（\r 不断行）
    { "\x80\x8A",         1, { { 0, 2, 0, 2 } } },                                   // 非法首字节
    { "a\xC0",            1, { { 0, 2, 0, 2 } } },                                   // 截断续字节
    { "\xC0\x8A",         2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                   // overlong U+000A（2 字节）
    { "\xE0\x80\x8A",     2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                   // overlong U+000A（3 字节）
    { "\xF0\x80\x80\x8A", 2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } },                   // overlong U+000A（4 字节）
    { "\xF0\x9F\x98\x80 a", 1, { { 0, 3, 0, 3 } } },                                 // 4 字节 emoji + 空格 + a
    { "abcdefghij",       1, { { 0, 10, 0, 10 } } },                                 // 纯 ASCII 长串
    { "一二三四五",         1, { { 0, 5, 0, 5 } } },                                  // CJK 长串
};

/// @brief 码点总数（**外部口径**，供 T39-4 / T39-7 交叉校验）
/// @details ★ 不能直接调 `GetCodepointCount()`——它是 **private**（`TextBox.h:322` 的
///          `private:` 早于 `:348` 的声明），而私有成员**不能用 `using` 暴露**
///          （访问检查在 `using` 声明处）。公共通道：`MoveCaretToEnd()`（`:128`）的实现
///          就是 `m_caret = GetCodepointCount()`（`TextBox.cpp:431`）⇒ 读回
///          `GetCaret()`（`:129`）即得**同一个**私有实现的结果。
///          ★ 副作用：移动 caret + Invalidate——caret **不在**行表的 10 维键里，故不干扰行表。
static size_t CountViaCaret(TestableTextBox& box){
    box.MoveCaretToEnd();
    return box.GetCaret();
}

/// @brief 表 B 的通用断言：行数走 `LineCountForTest()`（P0），逐行走两口径
/// @details 高 200 = 给 `n×16 <= areaH` 留足余量（最多 3 行 ⇒ 48）
static void ExpectLines(TestableTextBox& box, const LineGold& g){
    box.SetWordWrap(false);
    box.SetSize(120, 200);
    box.SetText(std::string(g.text));
    const size_t n = box.LineCountForTest();
    EXPECT_EQ(n, g.lineCount);
    if (n != g.lineCount)
        return;                                  // ★ 防越界：`LineRange` 是裸 `m_lines[i]`，越界 = UB
    for (size_t i = 0; i < n && i < 3; ++i){
        const auto own  = box.OwnForTest(i);
        const auto draw = box.DrawForTest(i);
        EXPECT_EQ(own.first,   g.rows[i].ownBeg);
        EXPECT_EQ(own.second,  g.rows[i].ownEnd);
        EXPECT_EQ(draw.first,  g.rows[i].drawBeg);
        EXPECT_EQ(draw.second, g.rows[i].drawEnd);
    }
}

/// @brief ★★ 2026-10-21：**小数行高**的可测 TextBox（取整方向的唯一可测面）。
/// @details 为什么必须单独做一个：`FakeTextMeasurer::LineHeight` 恒返回 **16.0（整数）**，
/// 于是 `行数 × 16 + padding×2` 永远是整数 ⇒ `ceil` 是**恒等变换** ⇒
/// 「取整方向」这条契约在既有夹具上**根本不可观测**。
/// ★ 这不是推测：负对照脚本（`.scratch/ecdi_multiline_negctl.py`）把实现改成截断后，
/// 全套 487 个用例**仍然全绿** —— 测试对这个缺陷是瞎的。
/// ⇒ 用 19.2（雅黑 13px 附近的真实量级）让"截断"与"ceil"产生可见差异。
class FractionalLineHeightTextBox : public TextBox
{
public:
    using TextBox::TextBox;
protected:
    TextMeasurer* ResolveMeasurer() const override{ return &ms_frac; }
private:
    /// @brief 行高 19.2、每码点 8 宽（宽度不参与本组判据）
    class FracMeasurer : public TextMeasurer
    {
    public:
        Size MeasureText(const Font&, const std::string& text) override{
            size_t count = 0;
            for (size_t i = 0; i < text.size(); ++i)
                if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
                    ++count;
            return Size{ static_cast<float>(count) * 8.0f, 19.2f };
        }
        float LineHeight(const Font&) override{ return 19.2f; }
    };
    static FracMeasurer ms_frac;
};

FractionalLineHeightTextBox::FracMeasurer FractionalLineHeightTextBox::ms_frac;

FakeTextMeasurer TestableTextBox::ms_fake;

// ══════════════════════════════════════════════════════════════════════════
// Phase 38 批 2 夹具：折行样本表 / 折行探针 / 计数测量器
// ══════════════════════════════════════════════════════════════════════════

/// @brief 常驻条槽（★ 折行请求态恒扣）：条厚 12（`ScrollBarStyle` 缺省）+ 内缩 max(0, 2) = 14
/// @details ⇒ 「样本折行宽 w」对应的**控件宽 = w + 14**。这条换算只在这里出现一次；
///   若将来缺省条厚变了，**样本表全体会一起红**（而不是悄悄偏移 14px 后仍然绿）。
constexpr int kWrapBarSlot = 14;

struct WrapLineExpect{
	std::size_t drawStart;   ///< 绘制起点（= 归属起点）
	std::size_t drawEnd;     ///< 绘制终点（裁剪口径 —— 软断点行尾空格不进命令）
	std::size_t ownEnd;      ///< 归属终点（可达性 / 选区口径）
};

/// @brief 一个折行样本（= 详设 §3.1 的一行；`width` 是**折行宽**，不是控件宽）
struct WrapSample{
	const char* text;
	float       width;
	bool        password;      ///< S7 走 `EchoMode::Password`（掩码串与明文同宽同码点数）
	std::size_t lineCount;
	WrapLineExpect lines[4];   ///< 本表最多 4 行（S3@24）
};

/// @brief ★ 全表 23 项 = §3.1 的 9 样本 × 2 档（18）+ `tail`（1）+ §3.4 的 S8 组（4）
/// @details **期望值一字不改地来自 `tools/phase38_wrap_probe.py` 的实算**（详设 §3 的样本表纪律）。
///   本文件里不得出现手推出来的行区间——要改先改脚本、重跑、再抄表。
const WrapSample kWrapSamples[] = {
	// ── §3.1 S1：拉丁软断点（分隔空格归前行）──
	{ "ab cd ef",  48.0f, false, 2, { { 0, 5, 6 }, { 6, 8, 8 } } },
	{ "ab cd ef",  24.0f, false, 3, { { 0, 2, 3 }, { 3, 5, 6 }, { 6, 8, 8 } } },
	// ── S2：纯 CJK（无空格可裁 ⇒ 两口径同值）──
	{ "一二三四五六", 40.0f, false, 2, { { 0, 5, 5 }, { 5, 6, 6 } } },
	{ "一二三四五六", 24.0f, false, 2, { { 0, 3, 3 }, { 3, 6, 6 } } },
	// ── S3：硬行 + 软行混存 ──
	{ "ab cd \nef gh", 40.0f, false, 3, { { 0, 2, 3 }, { 3, 5, 6 }, { 7, 12, 12 } } },
	{ "ab cd \nef gh", 24.0f, false, 4, { { 0, 2, 3 }, { 3, 5, 6 }, { 7, 9, 10 }, { 10, 12, 12 } } },
	// ── S4：悬挂闭标点（行宽 32 > 折行宽 24 —— D38-I 的判别面）──
	{ "一二三。四", 24.0f, false, 2, { { 0, 4, 4 }, { 4, 5, 5 } } },
	{ "一二三。四", 16.0f, false, 3, { { 0, 2, 2 }, { 2, 4, 4 }, { 4, 5, 5 } } },
	// ── S5：行首禁则（开括号回退到下一行行首）──
	{ "ab（cd",    24.0f, false, 2, { { 0, 2, 2 }, { 2, 5, 5 } } },
	{ "ab（cd",    16.0f, false, 3, { { 0, 2, 2 }, { 2, 4, 4 }, { 4, 5, 5 } } },
	// ── S5b：④b 守卫（不产生空行）──
	{ "（a",        8.0f, false, 2, { { 0, 1, 1 }, { 1, 2, 2 } } },
	{ "（a",       16.0f, false, 1, { { 0, 2, 2 } } },
	// ── S5c：对照组（④ 在纯 CJK 串上不可达）──
	{ "一二三（四", 32.0f, false, 2, { { 0, 3, 3 }, { 3, 5, 5 } } },
	{ "一二三（四", 16.0f, false, 3, { { 0, 2, 2 }, { 2, 3, 3 }, { 3, 5, 5 } } },
	// ── S6：超长词硬断 ──
	{ "abcdefgh",  26.0f, false, 3, { { 0, 3, 3 }, { 3, 6, 6 }, { 6, 8, 8 } } },
	{ "abcdefgh",  40.0f, false, 2, { { 0, 5, 5 }, { 5, 8, 8 } } },
	// ── S7：掩码（Password ⇒ 显示串是 ••，码点数与宽度同明文）──
	{ "secret",    40.0f, true,  2, { { 0, 5, 5 }, { 5, 6, 6 } } },
	{ "secret",    24.0f, true,  2, { { 0, 3, 3 }, { 3, 6, 6 } } },
	// ── tail：尾部空行（末行 w0、零 DrawText 命令）──
	{ "ab\n",      40.0f, false, 2, { { 0, 2, 2 }, { 3, 3, 3 } } },
	// ── §3.4 S8 组：分隔空格归属（A-15 的判别面）──
	{ "一二三 四",  24.0f, false, 2, { { 0, 3, 4 }, { 4, 5, 5 } } },
	{ "一二三  四", 24.0f, false, 2, { { 0, 3, 5 }, { 5, 6, 6 } } },
	{ "一二三 \n四", 24.0f, false, 2, { { 0, 3, 4 }, { 5, 6, 6 } } },
	{ "abc   ",    40.0f, false, 1, { { 0, 3, 6 } } },
};

/// @brief 按 (文本, 折行宽) 查样本 —— 用例只写「哪一串、哪一档」，不写下标
/// @details 用下标会让「表顺序调一下」变成一堆沉默的错位；用键则顺序无关。
const WrapSample& FindSample(const char* text, float width){
	for (const auto& sample : kWrapSamples){
		if (std::string(sample.text) == text && sample.width == width)
			return sample;
	}
	EXPECT_TRUE(false);   // 表里没有 ⇒ 用例写错了（不是产品码错）
	return kWrapSamples[0];
}

/// @brief 取第 `cpIndex` 个码点（越界返回 U'\0'）——T38-7 的硬行判据（`cps[ownedEnd] == '\n'`）
char32_t CodepointAt(const std::string& s, std::size_t cpIndex){
	std::size_t byte = 0;
	for (std::size_t i = 0; i < cpIndex; ++i){
		if (byte >= s.size())
			return U'\0';
		byte = CodepointIndexToByteOffset(s, i + 1);
	}
	if (byte >= s.size())
		return U'\0';
	return DecodeFirstCodepoint(s.substr(byte));
}

/// @brief 折行探针（无头）——`TestableTextBox::ResolveMeasurer()` 恒返回假测量器 ⇒ 无需窗口即可折行
/// @details 这正是 D38-D 不把 `GetWindow()` 放进折行判据的收益：折行逻辑可以在纯无头环境验。
struct WrapProbe{
	TestableTextBox box{ std::string("") };

	WrapProbe(){
		box.SetSize(40 + kWrapBarSlot, 64);
		box.SetWordWrap(true);
	}

	void Set(const WrapSample& s, int h = 64){
		box.SetEchoMode(s.password ? TextBox::EchoMode::Password : TextBox::EchoMode::Normal);
		box.SetText(std::string(s.text));
		box.SetSize(static_cast<int>(s.width) + kWrapBarSlot, h);
	}
};

/// @brief 计数型假测量器（T38-15）——先例 `WidgetTests.cpp` 的 CountingMeasurer 是 final 且
///        只覆写 `MeasureText`/`LineHeight` ⇒ 不可复用（要计数 `FitText`）
/// @details ★ 三个虚函数**均非 const**（`Render/TextMeasurer.h`）——写成 const override 不可编译。
class CountingFakeMeasurer final : public TextMeasurer{
public:
	int fitCalls = 0;
	int measureCalls = 0;

	Size MeasureText(const Font&, const std::string& text) override{
		++measureCalls;
		return Size{ static_cast<float>(CountCp(text)) * 8.0f, 16.0f };
	}

	/// ★ **必须覆写**：默认体用码点二分反调 `MeasureText` ⇒ 两个计数互相污染，
	///   T38-15 的「行数 == fitCalls」线性判据就不成立。
	TextFit FitText(const Font&, const std::string& text, std::size_t startCp, float maxWidth) override{
		++fitCalls;
		const std::size_t total = CountCp(text);
		if (startCp >= total)
			return TextFit{ 0, 0.0f };
		const std::size_t byWidth = static_cast<std::size_t>((std::max)(0.0f, maxWidth) / 8.0f);
		const std::size_t n = (std::min)(byWidth, total - startCp);   // ★ fitCp 是相对数、不得越串尾（C29-1）
		return TextFit{ n, static_cast<float>(n) * 8.0f };
	}

	float LineHeight(const Font&) override{ return 16.0f; }

private:
	static std::size_t CountCp(const std::string& s){
		std::size_t n = 0;
		for (char c : s)
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
				++n;
		return n;
	}
};

/// @brief 计数型可测 TextBox（★ 不能复用 `TestableTextBox`：它的测量器焊死为静态 `ms_fake`）
class CountingTextBox : public TextBox{
public:
	using TextBox::TextBox;
	using TextBox::RecalculateLines;
	using TextBox::LineRange;
	using TextBox::LineDrawRange;

	/// @brief 视觉行数（同 `TestableTextBox::RowCountForTest` 的理由：`m_lines` 是基类 private）
	[[nodiscard]] size_t RowCountForTest() const{
		return static_cast<size_t>(GetPreferredSize().height / 16.0f + 0.5f);
	}

	static CountingFakeMeasurer& Counter() noexcept{ return ms_counter; }

protected:
	TextMeasurer* ResolveMeasurer() const override{ return &ms_counter; }

private:
	static CountingFakeMeasurer ms_counter;
};

CountingFakeMeasurer CountingTextBox::ms_counter;

void TestTextBoxInsertDelete()
{
    // ── 原 #7：TextBox 编辑逻辑（迁移 + 扩展）──

    // InsertCodepoint 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "abcX");
        EXPECT_EQ(box.GetCaret(), 4);
    }

    // InsertCodepoint 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "Xabc");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // InsertCodepoint 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "aXbc");
        EXPECT_EQ(box.GetCaret(), 2);
    }

    // DeleteBackward 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);
    }

    // DeleteBackward 中间
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ac");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // DeleteBackward 头边界
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // DeleteForward 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "bc");
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // DeleteForward 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "ac");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // DeleteForward 尾边界
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // emoji 不切字
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'😀');
        EXPECT_EQ(box.GetText(), "abc😀");
        EXPECT_EQ(box.GetCaret(), 4);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // 中文不切字
    {
        TextBox box("ab");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'中');
        EXPECT_EQ(box.GetText(), "ab中");
        EXPECT_EQ(box.GetCaret(), 3);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);
    }
}

void TestTextBoxCaretMovement()
{
    // ── T2：光标迁移补测 ──

    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 2);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Right);
        EXPECT_EQ(box.GetCaret(), 3);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        EXPECT_EQ(box.GetCaret(), 3);
    }
    // 中文/emoji 光标移动不切字
    {
        TextBox box("中a😀");
        box.MoveCaretToEnd();
        EXPECT_EQ(box.GetCaret(), 3);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 2);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 1);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxGetSelection()
{
    // ── T3：GetSelection 查询接口 ──

    {
        TextBox box("hello");
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.InsertCodepoint(U'X');
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
        box.MoveCaret(TextBox::CaretDirection::Left);
        sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("");
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
}

void TestTextBoxBoundary()
{
    // ── T4：TextBox 边界条件 ──

    {
        TextBox box("");
        box.InsertCodepoint(U'a');
        EXPECT_EQ(box.GetText(), "a");
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("");
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("");
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "中🎉aX");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "中🎉");
        EXPECT_EQ(box.GetCaret(), 2);
    }
    {
        TextBox box("中🎉");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "中");
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("x");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("x");
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxCallback()
{
    // ── 7.5：回调注册（D4 RaiseXxx 分离模式 / D7 仅编辑操作触发）──

    // TC1: InsertCodepoint 触发回调 + 新文本正确
    {
        TextBox box("abc");
        std::string lastText;
        box.SetOnTextChanged([&lastText](const std::string& text){ lastText = text; });
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        EXPECT_EQ(lastText, "abcd");
    }

    // TC1b: DeleteBackward 触发回调（普通删除路径）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(count, 1);
    }

    // TC1c: DeleteForward 触发回调
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "bc");
        EXPECT_EQ(count, 1);
    }

    // TC2: DeleteBackward 头边界空操作 → 不触发（D7 边界语义）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteBackward();
        EXPECT_EQ(count, 0);
    }

    // TC2b: DeleteForward 尾边界空操作 → 不触发
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteForward();
        EXPECT_EQ(count, 0);
    }

    // TC3: SetText 不触发回调（D7 核心：程序设值不算用户修改）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.SetText("xyz");
        EXPECT_EQ(box.GetText(), "xyz");
        EXPECT_EQ(count, 0);
    }

    // TC3b: MoveCaret 系列不触发回调（仅光标移动，文本未变）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(count, 0);
    }

    // TC3c: 空回调 + 编辑 → 不崩溃（空 std::function 安全性，D5）
    {
        TextBox box("abc");
        box.SetOnTextChanged({});
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
    }

    // TC4: override OnTextChanged 不吞回调（D4 核心语义）
    {
        class MyTextBox : public TextBox{
        public:
            bool hookCalled = false;   // 类成员变量——override 内可访问（hook 而非 base：未调用基类）

        protected:
            void OnTextChanged(const std::string&) override{
                hookCalled = true;
                // 不调 TextBox::OnTextChanged() —— 模拟"忘记调基类"
            }
        };

        MyTextBox box;
        bool callbackCalled = false;
        box.SetOnTextChanged([&callbackCalled](const std::string&){ callbackCalled = true; });
        box.InsertCodepoint(U'X');

        EXPECT_TRUE(box.hookCalled);     // 虚方法被调用
        EXPECT_TRUE(callbackCalled);     // 回调仍被调用（D4 核心收益：override 不吞回调）
    }
}

// ── P0：Selection 测试（7.2 补欠账——5.5.2 P8 承诺）──────────────
// 全部经键盘路径（OnKeyDown 暴露）：纯码点逻辑 + SyncTextInputCaret 有 Window 防御 → 无窗口可测。
// 索引单位 = 码点（TextBox.h SelectionRange 注释明确）；anchor 固定端 + caret active 端模型。

void TestTextBoxSelectionKeyboard()
{
    // S1: Shift+Right 扩展选择（anchor=2 起点——先无 Shift 移动设 anchor，再 Shift 扩张）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();                                  // caret=0, anchor=0
        box.MoveCaret(TextBox::CaretDirection::Right);           // caret=1, anchor=1
        box.MoveCaret(TextBox::CaretDirection::Right);           // caret=2, anchor=2（无选择）
        EXPECT_FALSE(box.GetSelection().has_value());

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // caret 2→3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // caret 3→4
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 2);   // anchor 固定
        EXPECT_EQ(sel->end, 4);     // active 端
        EXPECT_EQ(box.GetCaret(), 4);
    }

    // S2a: Shift+Left 收缩（{0,3} → {0,2}）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 2
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 3 → {0,3}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {0,2}
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 2);
    }

    // S2b: Shift+Left 反向跨越（anchor=4，向左扩到整行 → {0,4}）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();                                    // caret=4, anchor=4
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));   // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 4);
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // S3: Shift+Home/End 扩展到行首/行尾
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();                                    // caret=3, anchor=3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Home, KeyModifier::Shift));   // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TestableTextBox box("abc");
        box.MoveCaretToStart();                                  // caret=0, anchor=0
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::End, KeyModifier::Shift));    // caret 3
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // S4: 无 Shift 移动清除选择（先动后清——防幽灵选择）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // {0,1}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // {0,2}
        EXPECT_TRUE(box.GetSelection().has_value());

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::None));     // caret 2→1 + 清选择
        EXPECT_FALSE(box.GetSelection().has_value());
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // S7: 跨代理对选择（emoji 整体占 1 码点索引——不切半个代理对）
    {
        TestableTextBox box("a😀b");                            // 码点：a(0) 😀(1) b(2)
        EXPECT_EQ(box.GetCaret(), 0);
        box.MoveCaretToEnd();                                   // caret=3, anchor=3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 2
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 1
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 1);   // 😀 整体（非 UTF-16 代理对拆半）
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 1);

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 0
        sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
    }

    // S8: 选区-光标一致性（向右扩张 caret==end；anchor 恒 0）
    {
        TestableTextBox box("abcde");
        box.MoveCaretToStart();
        for (int i = 0; i < 4; ++i)
        {
            box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));
            auto sel = box.GetSelection();
            EXPECT_TRUE(sel.has_value());
            EXPECT_EQ(box.GetCaret(), sel->end);   // active 端 = caret
        }
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 收缩
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(box.GetCaret(), sel->end);
    }

    // S10: 空文本/单字符边界（不崩 + 选区合法）
    {
        TestableTextBox box("");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 0<0 → 不动
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        EXPECT_EQ(box.GetCaret(), 0);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
    {
        TestableTextBox box("x");
        box.MoveCaretToEnd();                                   // caret=1, anchor=1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 1);
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxSelectionEdit()
{
    // S5: InsertCodepoint 删选中区（编辑操作自包含——有 Selection 先删）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();                                   // caret=4, anchor=4
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.InsertCodepoint(U'X');                              // 删 [2,4)="cd" → "ab" + X → "abX"
        EXPECT_EQ(box.GetText(), "abX");
        EXPECT_EQ(box.GetCaret(), 3);
        EXPECT_FALSE(box.GetSelection().has_value());
    }

    // S6a: DeleteBackward 删选中区（一次删除，非单字符）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);   // 新光标 = 选区 min
        EXPECT_FALSE(box.GetSelection().has_value());
    }

    // S6b: DeleteForward 删选中区
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 2 → {0,2}
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "cd");
        EXPECT_EQ(box.GetCaret(), 0);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
}

// ── 8.5.1：文本系统 2.0 测试（F1-F15）──────────────────────────
// 覆盖：InsertText 多码点 / Composition 模型 B 状态机（C7/C8/C12）/ Ctrl 组合（C1）/
// OnTimer 闪烁（C2）/ 中间替换（F14）/ code point 索引（F15）。

void TestTextBoxTextSystem2()
{
    // F1: InsertText 多码点插入 + 空串 no-op
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("def");
        EXPECT_EQ(box.GetText(), "abcdef");
        EXPECT_EQ(box.GetCaret(), 6);
    }
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.InsertText("");
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(count, 0);   // 空串 no-op（D7 边界语义）
    }
    // F2: InsertText 有 Selection 先删（粘贴覆盖选中区）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.InsertText("XY");
        EXPECT_EQ(box.GetText(), "abXY");
        EXPECT_EQ(box.GetCaret(), 4);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
    // F3: Composition 首帧占位（模型 B 起始）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();   // caret=3
        box.UpdateComposition("nihao");
        EXPECT_EQ(box.GetText(), "abcnihao");
        EXPECT_EQ(box.GetCaret(), 8);   // 3 + 5 码点
        // m_isComposing 无法直接断言（private）——经 F5/F7 行为验证
    }
    // F4: Composition 更新替换区间（非追加）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.UpdateComposition("nihao2");
        EXPECT_EQ(box.GetText(), "abcnihao2");   // 区间替换非追加
        EXPECT_EQ(box.GetCaret(), 9);
    }
    // F5: Composition 空串不结束（C7——空串 ≠ Commit）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.UpdateComposition("");   // 组合中无内容——组合仍在
        EXPECT_EQ(box.GetText(), "abc");   // 区间被清空（临时）
        box.UpdateComposition("nih");      // 组合继续 → 区间恢复
        EXPECT_EQ(box.GetText(), "abcnih");
    }
    // F6: Composition 期间不触发 TextChanged（C8——经 ReplaceTextRange 非 InsertText）
    {
        TestableTextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.UpdateComposition("n");
        box.UpdateComposition("ni");
        box.UpdateComposition("nihao");
        EXPECT_EQ(count, 0);   // 组合过程零回调
        box.CommitComposition("你好");
        EXPECT_EQ(count, 1);   // Commit = 正式编辑 → 一次回调
    }
    // F7: CommitComposition 正式生效（C7/C3）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.CommitComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好");
        EXPECT_EQ(box.GetCaret(), 5);
        box.UpdateComposition("zz");   // 组合已结束——下次 Update 视为新组合开始
        EXPECT_EQ(box.GetText(), "abc你好zz");
    }
    // F8: Commit 无组合 no-op（fail-safe）
    {
        TestableTextBox box("abc");
        box.CommitComposition("xyz");
        EXPECT_EQ(box.GetText(), "abc");   // 不变
        EXPECT_EQ(box.GetCaret(), 0);
    }
    // F9: CancelComposition 擦除区间
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.CancelComposition();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);   // 光标回组合起点
    }
    // F10: Ctrl+A 全选（C1 键盘路径）
    {
        TestableTextBox box("hello");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::A, KeyModifier::Ctrl));
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 5);
    }
    // F11: Ctrl+C 无选区 no-op（无窗口 GetWindow()==nullptr → 静默跳过，不崩）
    {
        TestableTextBox box("hello");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::C, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "hello");
    }
    // F12: OnTimer 切换光标（无窗口环境允许验证翻转逻辑）
    {
        TestableTextBox box("abc");
        EXPECT_FALSE(box.IsCaretVisible());
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));
        EXPECT_TRUE(box.IsCaretVisible());
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));
        EXPECT_FALSE(box.IsCaretVisible());
    }
    // F13: OnTimer 非本 id 忽略（多 timer 隔离）
    {
        TestableTextBox box("abc");
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));   // true
        box.OnTimer(TimerEvent(nullptr, 999));
        EXPECT_TRUE(box.IsCaretVisible());   // 非本 id 不翻转
    }
    // F14: Composition 中间替换（GPT 检查点 4——不破坏前后文本）
    {
        TestableTextBox box("abcDEF");
        box.MoveCaretToEnd();                                    // caret=6
        box.MoveCaret(TextBox::CaretDirection::Left);            // 5
        box.MoveCaret(TextBox::CaretDirection::Left);            // 4
        box.MoveCaret(TextBox::CaretDirection::Left);            // 3（'c' 后）
        box.UpdateComposition("nihao");
        EXPECT_EQ(box.GetText(), "abcnihaoDEF");
        box.UpdateComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好DEF");
        box.CommitComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好DEF");
        EXPECT_EQ(box.GetCaret(), 5);
    }
    // F15: Composition code point 索引（GPT 检查点 5——中文不按 UTF-8 byte 切）
    {
        TestableTextBox box("你ABC好");   // 码点：你(0) A(1) B(2) C(3) 好(4)
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);   // caret=1（'你' 后——非 byte 3）
        box.UpdateComposition("中文");
        EXPECT_EQ(box.GetText(), "你中文ABC好");
        EXPECT_EQ(box.GetCaret(), 3);   // 1 + 2 码点
        box.CommitComposition("中文");
        EXPECT_EQ(box.GetText(), "你中文ABC好");
        EXPECT_EQ(box.GetCaret(), 3);
    }
}

// ── 8.5.2：多行与滚动测试（F16-F30）──────────────────────────
// 覆盖：行分割（LineRange 推断——空尾行/连续换行/UTF-8 多字节）/ Enter 换行 / 跨行光标 /
// 坐标定位 Y 定行（无窗口 GetLineHeight=16 兜底）/ 滚动 clamp / 双击选词（word/non-word/中文/emoji）。

void TestTextBoxMultiline()
{
    // F16: RecalculateLines 行分割（"ab\ncd\ne" → 行起始 {0,3,6}——经 LineRange 推断）
    {
        TestableTextBox box("ab\ncd\ne");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        auto r2 = box.LineRange(2);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 2);
        EXPECT_EQ(r1.first, 3);  EXPECT_EQ(r1.second, 5);
        EXPECT_EQ(r2.first, 6);  EXPECT_EQ(r2.second, 7);
    }
    // F17: Enter 插入换行（OnKeyDown 键盘路径）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
        EXPECT_EQ(box.GetText(), "abc\n");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    // F18: 跨行光标移动（越过 \n）
    {
        TestableTextBox box("a\nb");   // 码点 a(0) \n(1) b(2)
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);   // 1（\n 处）
        EXPECT_EQ(box.GetCaret(), 1);
        box.MoveCaret(TextBox::CaretDirection::Right);   // 2（跨到第二行 b）
        EXPECT_EQ(box.GetCaret(), 2);
    }
    // F19a: CaretIndexFromPosition Y 定行（无窗口 GetLineHeight=16 兜底——第一行）
    {
        TestableTextBox box("ab\ncd");
        box.RecalculateLines();
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 0.0f }), 0);     // 第一行 → 行起始 0
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 10.0f }), 0);    // 第一行内
    }
    // F20a: CaretIndexFromPosition 跨行（第二行）
    {
        TestableTextBox box("ab\ncd");
        box.RecalculateLines();
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 16.0f }), 3);    // y=16 → 第二行起始 3
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 30.0f }), 3);    // 第二行内
    }
    // F21: OnMouseWheel 滚动（向上减小 / 向下增加 + clamp [0, max]）
    {
        TestableTextBox box("a\nb\nc\nd\ne");   // 5 行 × 16 = 80 内容高
        box.SetSize(200, 40);                  // 可视 40 → maxScroll = 80-40 = 40
        box.MoveCaretToEnd();                  // EnsureCaretVisible → 已滚到 40（max）
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 120));    // 向上滚一行 → 40-16 = 24
        EXPECT_EQ(box.GetScrollOffsetY(), 24.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -1200));  // 向下狂滚 → 24+160 = 184 → clamp 40
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 12000));  // 向上狂滚 → clamp 0
        EXPECT_EQ(box.GetScrollOffsetY(), 0.0f);
    }
    // F22: 双击英文选词（"hello world" 点 world 中）
    {
        TestableTextBox box("hello world");
        auto w = box.GetWordBounds(8);   // "world" 的 o（码点 8）
        EXPECT_EQ(w.first, 6);
        EXPECT_EQ(w.second, 11);
    }
    // F23: 双击中文选词（单码点独立）
    {
        TestableTextBox box("你好世界");
        auto w = box.GetWordBounds(1);   // 第 2 个码点（好）
        EXPECT_EQ(w.first, 1);
        EXPECT_EQ(w.second, 2);
    }
    // F24: 双击 emoji UTF-8 code point 完整选择（😀 = 4 字节 1 码点）
    {
        TestableTextBox box("a😀b");
        auto w = box.GetWordBounds(1);   // 😀
        EXPECT_EQ(w.first, 1);
        EXPECT_EQ(w.second, 2);
    }
    // F25: EnsureCaretVisible 光标跟随（末尾超出可视 → 滚动）
    {
        TestableTextBox box("a\nb\nc\nd\ne");   // 5 行 × 16 = 80
        box.SetSize(200, 40);                  // 可视 40 → maxScroll 40
        box.MoveCaretToEnd();                  // caret 第 5 行 top=64 → 滚到 64+16-40=40
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
    }
    // F26: 尾部换行空行（"ab\n" → line1=[3,3]）
    {
        TestableTextBox box("ab\n");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 2);
        EXPECT_EQ(r1.first, 3);  EXPECT_EQ(r1.second, 3);   // 空尾行
    }
    // F27: 空文本（"" → 单行 [0,0]）
    {
        TestableTextBox box("");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 0);
    }
    // F28: 连续换行（"a\n\nb" → line0=[0,1] line1=[2,2] line2=[3,4]）
    {
        TestableTextBox box("a\n\nb");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        auto r2 = box.LineRange(2);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 1);   // "a"
        EXPECT_EQ(r1.first, 2);  EXPECT_EQ(r1.second, 2);   // ""（空行）
        EXPECT_EQ(r2.first, 3);  EXPECT_EQ(r2.second, 4);   // "b"
    }
    // F29: 双击标点/空格（non-word → 单码点）
    {
        TestableTextBox box("hello, world");
        auto comma = box.GetWordBounds(5);   // 逗号（码点 5）
        EXPECT_EQ(comma.first, 5);  EXPECT_EQ(comma.second, 6);
        auto space = box.GetWordBounds(6);   // 空格（码点 6）
        EXPECT_EQ(space.first, 6);  EXPECT_EQ(space.second, 7);
        auto hello = box.GetWordBounds(1);   // "hello" 中
        EXPECT_EQ(hello.first, 0);  EXPECT_EQ(hello.second, 5);
    }
    // F30: UTF-8 多字节换行索引（"你\n好" → 行起始 {0,2}——"你" 3 字节但 1 码点）
    {
        TestableTextBox box("你\n好");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 1);   // "你"（码点 0，3 字节）
        EXPECT_EQ(r1.first, 2);  EXPECT_EQ(r1.second, 3);   // "好"（码点 2）
    }
    // F31: Up/Down 基本跨行（无窗口：preferred column=0 → 行起始）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0（第一行）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 3);                            // → 第二行起始（码点 3）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // → 回第一行起始
    }
    // F32: 边界 no-op（第一行 Up / 最后一行 Down 不动）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0（第一行）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // 第一行 Up → no-op
        box.MoveCaretToEnd();                                    // caret=5（末尾——"ab\ncd" 共 5 码点）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 5);                            // 最后一行 Down → no-op
    }
    // F33: Shift+Down 扩展选择（anchor 保留、caret 到下一行）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::Shift));
        EXPECT_EQ(box.GetCaret(), 3);                            // caret → 第二行起始
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);  EXPECT_EQ(sel->end, 3);       // anchor=0 保留 → {0,3}
    }
    // F34: Down→Up 往返（preferred column 跨行不重置——无窗口恒 0，验证机制往返）
    {
        TestableTextBox box("abc\ndef");
        box.MoveCaretToEnd();                                    // caret=6（第二行末尾）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // → 第一行起始（preferred=0）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 4);                            // → 回第二行起始
        EXPECT_FALSE(box.GetSelection().has_value());            // 无 Shift → 无选择残留
    }
}

// ── 8.5.3：Undo/Redo（快照模式 B6 + C3/C4/D4/D7/D9 契约）──
void TestTextBoxUndoRedo()
{
    // F35：基本撤销（InsertCodepoint）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        box.Undo();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }
    // F36：逐级撤销 + redo（InsertText 多步）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("de");                                     // "abcde"
        box.InsertText("f");                                      // "abcdef"
        EXPECT_EQ(box.GetText(), "abcdef");
        box.Undo();                                               // 撤销 "f" → "abcde"
        EXPECT_EQ(box.GetText(), "abcde");
        box.Redo();                                               // 重做 "f" → "abcdef"
        EXPECT_EQ(box.GetText(), "abcdef");
    }
    // F37：新编辑清空 redo 栈（C4b——作废旧分支）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("d");                                      // "abcd"
        box.Undo();                                               // → "abc"
        box.InsertText("x");                                      // 新编辑 → "abcx"（清 redo）
        EXPECT_EQ(box.GetText(), "abcx");
        box.Redo();                                               // redo 已清空 → no-op
        EXPECT_EQ(box.GetText(), "abcx");
    }
    // F38：空栈 no-op（Undo/Redo 各试）
    {
        TextBox box("abc");
        box.Undo();                                               // undo 栈空
        EXPECT_EQ(box.GetText(), "abc");
        box.Redo();                                               // redo 栈空
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F39：空操作不 Push（DeleteBackward 头边界 no-op 后 Undo 无操作——D7）
    {
        TextBox box("abc");                                       // caret=0（构造默认）
        box.DeleteBackward();                                     // 头边界 → no-op（不 Push）
        EXPECT_EQ(box.GetText(), "abc");
        box.Undo();                                               // 栈空 → no-op
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F40：Composition 一次撤销（C3——组合本身是一个 Undo 单元）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");                           // 首次 Push "abc" + 占位
        box.CommitComposition("你好");                             // 正式文本（不再 Push）
        EXPECT_EQ(box.GetText(), "abc你好");
        box.Undo();                                               // 一次撤销整个组合 → "abc"
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F41：快照含多行文本与光标（Undo 恢复完整编辑状态）
    {
        TextBox box("a\nb");
        box.MoveCaretToEnd();                                     // caret=3
        box.InsertText("\nc");                                    // "a\nb\nc"
        EXPECT_EQ(box.GetText(), "a\nb\nc");
        box.Undo();
        EXPECT_EQ(box.GetText(), "a\nb");
    }
    // F42：InsertText Undo（粘贴/Enter 路径）
    {
        TextBox box("ab");
        box.MoveCaretToEnd();
        box.InsertText("cd");                                     // "abcd"
        box.Undo();
        EXPECT_EQ(box.GetText(), "ab");
    }
    // F43：Cut Undo（剪切 = 复制 + 删选中区，一次编辑一次快照——走 Ctrl+X 键盘路径）
    {
        TestableTextBox box("hello world");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 选区 {0,2}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::X, KeyModifier::Ctrl));        // 剪 "he"（无窗口剪贴板静默跳过复制，删除生效）
        EXPECT_EQ(box.GetText(), "llo world");
        box.Undo();
        EXPECT_EQ(box.GetText(), "hello world");
    }
    // F44：Composition Cancel（恢复快照 + 不留历史 + 后续 Undo no-op——GPT 🔴）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");                           // → "abcnihao"，Push "abc"
        EXPECT_EQ(box.GetText(), "abcnihao");
        box.CancelComposition();                                  // RestoreSnapshot → "abc"，弹栈
        EXPECT_EQ(box.GetText(), "abc");
        box.Undo();                                               // 栈空（快照已弹）→ no-op
        EXPECT_EQ(box.GetText(), "abc");                          // Cancel 不是编辑，不留历史
    }
    // F45：连续 Redo（Undo↔Redo 双向流转完整链）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("d");                                      // "abcd"
        box.InsertText("e");                                      // "abcde"
        box.Undo();                                               // → "abcd"
        box.Undo();                                               // → "abc"
        EXPECT_EQ(box.GetText(), "abc");
        box.Redo();                                               // → "abcd"
        box.Redo();                                               // → "abcde"
        EXPECT_EQ(box.GetText(), "abcde");
    }
    // F45b：Ctrl+Z/Y 键盘路径（走 OnKeyDown Ctrl 分支）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("xyz");                                    // "abcxyz"
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Z, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "abc");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Y, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "abcxyz");
    }
}

// ── P1：echo 掩码 / 只读 / 形态（modelprobe-p1-detailed-design §3/§8）──

void TestTextBoxEchoMasked()
{
	// PaintMasked：Password 绘制串 = 每码点一个 •（U+2022，UTF-8 3 字节）
	TextBox box("abc");
	box.SetEchoMode(TextBox::EchoMode::Password);
	box.SetSize(200, 30);
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 10, 20);
	bool foundMasked = false;
	for (const auto& cmd : commands){
		if (const auto* text = std::get_if<DrawTextCommand>(&cmd)){
			EXPECT_EQ(text->text, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");   // •••
			foundMasked = true;
		}
	}
	EXPECT_TRUE(foundMasked);
}

void TestTextBoxEchoGetTextReal()
{
	// GetTextReal：数据层不变（显示层打点）——编辑作用于真实文本
	TextBox box("sk-abc");
	box.SetEchoMode(TextBox::EchoMode::Password);
	EXPECT_EQ(box.GetText(), "sk-abc");
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'X');
	EXPECT_EQ(box.GetText(), "sk-abcX");
	EXPECT_EQ(box.GetCaret(), 7);
}

void TestTextBoxReadOnlyEditNoOp()
{
	TextBox box("abc");
	box.SetReadOnly(true);
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'X');
	box.DeleteBackward();
	box.InsertText("YY");
	box.Undo();
	box.Redo();
	EXPECT_EQ(box.GetText(), "abc");   // 编辑全 no-op
	EXPECT_EQ(box.GetCaret(), 3);
	// SetText 仍可程序写入（预览刷新）
	box.SetText("{\"k\":1}");
	EXPECT_EQ(box.GetText(), "{\"k\":1}");
	// 关闭只读恢复编辑
	box.SetReadOnly(false);
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'Z');
	EXPECT_EQ(box.GetText(), "{\"k\":1}Z");
}

void TestTextBoxReadOnlyCompositionRefuse()
{
	TestableTextBox box("abc");   // TestableTextBox：UpdateComposition/CommitComposition 经 using 暴露（protected）
	box.SetReadOnly(true);
	box.MoveCaretToEnd();
	box.UpdateComposition("ni");   // 只读 → no-op（不进入组合）
	EXPECT_EQ(box.GetText(), "abc");
	box.CommitComposition("你好");
	EXPECT_EQ(box.GetText(), "abc");   // 未组合 → no-op
}

void TestTextBoxSingleLineEnterNoOp()
{
	// 单行：Enter 不插入 \n（多行默认 Enter 换行——8.5.2 行为不变）
	TestableTextBox box("abc");
	box.MoveCaretToEnd();
	box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
	EXPECT_EQ(box.GetText(), "abc\n");   // 默认多行：Enter 换行
	EXPECT_EQ(box.GetCaret(), 4);

	box.SetSingleLine(true);
	box.MoveCaretToEnd();
	box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
	EXPECT_EQ(box.GetText(), "abc\n");   // 单行：Enter no-op
	EXPECT_EQ(box.GetCaret(), 4);

	// 单行开关幂等
	box.SetSingleLine(true);
	EXPECT_TRUE(box.IsSingleLine());
}

void TestTextBoxPreferredMultilineParticipates()
{
	// ★★ 2026-10-21：**旧断言翻转**（原 `TestTextBoxPreferredMultilineNotParticipating`）。
	//   改前：多行（含 \n）→ preferred = 当前尺寸（K4 挂账「v1 手工 SetSize，不参与 AutoSize」）。
	//   改后：多行 → {当前宽, ceil(行数 × 行高 + padding×2)}，与 wrap 分支（D29-B）同构。
	//
	//   ★ 为什么**不删掉这条测试**：它是这个语义唯一的守卫。翻转成反向断言之后，
	//     「装不下 ⇒ preferred > 当前」「装得下 ⇒ preferred == 当前（即 AutoSize 不会
	//      把框撑大）」两个方向都有测试盯着——后者正是"缩得回去"那个方向的守卫。
	//     ★ 单靠"长高"一侧的测试抓不到"只增不减"的实现（harness 的 M5 变异实证过）。
	//
	//   ★ 口径：FakeTextMeasurer 的 LineHeight = 16、MeasureText 高 = 16、每码点 8 宽。
	//     "ab\ncd" ⇒ 2 行 ⇒ 内容高 = 2×16 = 32；padding 0 ⇒ preferred = {当前宽, 32}。
	TestableTextBox box("ab\ncd");

	// ① 当前尺寸**小于**内容高 ⇒ preferred 必须报告真话（改前这里返回 30，即"撒谎"）。
	box.SetSize(100, 30);
	{
		const Size preferred = box.GetPreferredSize();
		EXPECT_NEAR(preferred.width, 100.0f, kEps);    // 宽 = 当前宽（不制造宽度约束）
		EXPECT_NEAR(preferred.height, 32.0f, kEps);    // 高 = 2 行 × 16
		EXPECT_TRUE(preferred.height > 30.0f);         // ★ 反向断言：装不下就必须大于当前
	}

	// ② `AutoSize()` 真的会长高（改前是结构性 no-op——`Widget::AutoSize` 见同尺寸即返回 false）。
	{
		EXPECT_TRUE(box.AutoSize());
		EXPECT_EQ(box.GetHeight(), 32);
	}

	// ③ ★★ 「报告内容高、不是视口高」——这是"只增不减"实现的**唯一**反证方向。
	//    ★ 我第一版把这条写成 `EXPECT_FALSE(box.AutoSize())` + `GetHeight()==60`，**写错了**：
	//      `AutoSize` 的本职就是"尺寸 = preferred"，视口 60 > 内容 32 时它**应当**缩到 32。
	//      那条断言是在要求 AutoSize 什么都不做，等于把"不参与"的旧语义又请了回来。
	//      ⇒ 正确的判据是**preferred 本身**：它必须报内容高（32），不能报视口高（60）。
	//      若实现取 `GetMaxScrollOffset()+GetTextAreaHeight()` 那种带 `max(0,·)` 的式子，
	//      这里会得到 60 —— 那条断言正是打它的。
	{
		box.SetSize(100, 60);                       // 视口比内容高
		const Size preferred = box.GetPreferredSize();
		EXPECT_NEAR(preferred.height, 32.0f, kEps); // ★ 内容高，不是视口高 60
		EXPECT_TRUE(preferred.height < 60.0f);      // ★ 反向：绝不等于视口
		// 且 AutoSize 把框**缩**到内容高（"缩得回去"的正面表述）
		EXPECT_TRUE(box.AutoSize());
		EXPECT_EQ(box.GetHeight(), 32);
	}

	// ④ ★ 删掉一行后必须变矮（行数口径 = `\n` 数 + 1 的可测面）。
	{
		box.SetText("ab");
		const Size preferred = box.GetPreferredSize();
		EXPECT_NEAR(preferred.height, 16.0f, kEps); // 1 行
		EXPECT_TRUE(preferred.height < 32.0f);      // ★ 反向：真的缩回去了
	}

	// ⑤ ★★ **取整方向**（`ceil` 而不是截断）——本组唯一能盯住它的判据。
	//    ★ 为什么必须用小数行高：`FakeTextMeasurer` 行高 16.0 是整数，
	//      `行数 × 16` 恒为整数 ⇒ `ceil` 与截断**结果相同**，这条契约在 ①②③④
	//      里根本不可观测（负对照实测：改成截断后全套仍绿）。
	//    行高 19.2、2 行 ⇒ 内容高 = 38.4：
	//      · `ceil`   ⇒ 39  ⇒ 39 >= 38.4 ✅ 内容装得下
	//      · 截断      ⇒ 38  ⇒ 38 <  38.4 ❌ 差 0.4px ⇒ `GetMaxScrollOffset > 0`
	//                        ⇒ **恰好装得下时反而冒出内部滚动条、末行被切**
	{
		FractionalLineHeightTextBox frac("a\nb");    // 2 行
		frac.SetSize(120, 10);
		const Size preferred = frac.GetPreferredSize();
		EXPECT_NEAR(preferred.height, 39.0f, kEps);          // ★ ceil(38.4) = 39
		EXPECT_TRUE(preferred.height >= 2.0f * 19.2f);       // ★ 绝不小于真实内容高
		// 且 `AutoSize` 之后仍然装得下（截断实现会在这里留下 0.4px 的溢出）
		EXPECT_TRUE(frac.AutoSize());
		EXPECT_EQ(frac.GetHeight(), 39);
		EXPECT_TRUE(static_cast<float>(frac.GetHeight()) >= 2.0f * 19.2f);
	}

	// ⑥ ★ 细节②守卫：**无测量器 / 无窗口 ⇒ 退回当前尺寸**。
	//    与 wrap 分支「无有限宽 ⇒ 退化单行」（`IsWrapLayoutActive` 三条件）同构：
	//    没有测量能力就没有内容高可言，此时返回当前尺寸是唯一诚实的答案。
	//    ★ 这条同时是**无头测试的零污染契约**（Phase 7.2）：裸 `TextBox`（无窗口、
	//      不注入测量器）调用 `GetPreferredSize()` 必须与改前**逐位一致**。
	{
		TextBox bare("ab\ncd");                      // 裸 TextBox：ResolveMeasurer() 返回 nullptr
		bare.SetSize(100, 30);
		const Size preferred = bare.GetPreferredSize();
		EXPECT_NEAR(preferred.width, 100.0f, kEps);  // = 当前尺寸
		EXPECT_NEAR(preferred.height, 30.0f, kEps);  // ★ 不是 2×16=32，而是退回当前高
		EXPECT_FALSE(bare.AutoSize());               // 当前尺寸 == preferred ⇒ no-op
	}
}

void TestTextBoxPreferredSingleLine()
{
	// §3.6 验证项：单行 "Hi"（2 码点 × 8 = 16 宽）+ padding 4 → {16+8, 16+8} = {24, 24}——
	// height == lineH + padding×2 → AutoSize 后上下对称自然垂直居中（不做 VerticalCentered API）
	TestableTextBox box("Hi");
	box.SetSize(999, 999);
	box.SetStyle(TextBoxStyleOverride{ .padding = 4.0f });
	const Size preferred = box.GetPreferredSize();
	EXPECT_NEAR(preferred.width, 24.0f, kEps);
	EXPECT_NEAR(preferred.height, 24.0f, kEps);
	EXPECT_TRUE(box.AutoSize());
	EXPECT_EQ(box.GetWidth(), 24);
	EXPECT_EQ(box.GetHeight(), 24);
}

void TestTextBoxShapeRounded()
{
	TextBox box;
	box.SetSize(100, 30);
	box.SetStyle(TextBoxStyleOverride{
		.background = Color::White(),
		.cornerRadius = 6.0f,
	});
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 0, 0);
	// 背景命令 = DrawRoundedRectCommand（radius 6）——命令流 = PushClip → 背景 → PopClip
	EXPECT_EQ(commands.size(), 3);
	EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[1]));
	const auto& rr = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_NEAR(rr.cornerRadius, 6.0f, kEps);
}

void TestTextBoxShapeBorderRing()
{
	TextBox box;
	box.SetSize(100, 30);
	box.SetStyle(TextBoxStyleOverride{
		.background = Color::FromRGBA8(28, 33, 43, 255),
		.cornerRadius = 6.0f,
		.borderWidth = 1.0f,
		.borderColor = Color::FromRGBA8(42, 49, 64, 255),
	});
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 0, 0);
	// 双矩形序：PushClip → ① 外层 borderColor（radius 6）② 内层背景（内缩 1，radius 5）→ PopClip
	EXPECT_EQ(commands.size(), 4);
	const auto& outer = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_EQ(outer.color, Color::FromRGBA8(42, 49, 64, 255));
	EXPECT_NEAR(outer.cornerRadius, 6.0f, kEps);
	const auto& inner = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_EQ(inner.color, Color::FromRGBA8(28, 33, 43, 255));
	EXPECT_NEAR(inner.cornerRadius, 5.0f, kEps);
}


// ── scrollbar-placement-and-appearance v1.1：垂直滚动条（T1–T8；契约见 docs/dev/scrollbar-placement-and-appearance.md §6 / §7）──

/// @brief 造 N 行文本（每行一个 'a'，`\n` 分隔）——行数驱动内容高（假测量器行高恒 16）
std::string MakeLines(size_t n)
{
	std::string text;
	for (size_t i = 0; i < n; ++i){
		if (i != 0)
			text += '\n';
		text += 'a';
	}
	return text;
}

/// @brief 取命令流中第 n 个（0-based）`PushClipCommand` 的宽度；找不到返回 -1
/// @details TextBox 命令流次序：第 0 个 = `Widget::Paint` 的**控件边界**，
/// 第 1 个 = `TextBox::OnPaint` 的**文本区**（条自身的 clip 在其后 ⇒ 同前例 ClipTests `commands[2]`）
float NthClipWidth(const CommandBuffer& commands, size_t n)
{
	size_t seen = 0;
	for (const auto& command : commands){
		if (const auto* clip = std::get_if<PushClipCommand>(&command)){
			if (seen == n)
				return clip->rect.width;
			++seen;
		}
	}
	return -1.0f;
}

/// T1：多行 + 内容溢出 ⇒ 条可见
void TestTextBoxScrollBarVisibleOnOverflow()
{
	TextBox box(MakeLines(10));   // 10 行 × 16 = 160 内容高
	box.SetSize(200, 40);         // 视口 40 ⇒ 溢出
	EXPECT_TRUE(box.GetVerticalScrollBar() != nullptr);
	EXPECT_TRUE(box.GetVerticalScrollBar()->IsVisible());
}

/// T2：多行 + 内容不溢出 ⇒ 条隐藏，且**不参与命中**（Widget::HitTest:117 既有语义）
void TestTextBoxScrollBarHiddenWhenFits()
{
	TextBox box(MakeLines(2));    // 32 < 40
	box.SetSize(200, 40);
	EXPECT_FALSE(box.GetVerticalScrollBar()->IsVisible());
	// 条区域内命中到的**不是条**（不可见 ⇒ 不参与命中）——不断言具体返回谁（依赖基类 ContainsPoint）
	EXPECT_TRUE(box.HitTest(194, 20) != box.GetVerticalScrollBar());
}

/// T3：单行 ⇒ 条恒隐藏（即便视口极小、文本很长）
void TestTextBoxScrollBarHiddenSingleLine()
{
	TextBox box(MakeLines(10));
	box.SetSingleLine(true);
	box.SetSize(200, 20);         // 即便视口只有 20
	EXPECT_FALSE(box.GetVerticalScrollBar()->IsVisible());
}

/// T4：条**可命中**（最上层）+ ★ **偏移权威归 TextBox**（D9 / K10——条不是第二控制权）
void TestTextBoxScrollBarHitAndAuthority()
{
	TestableTextBox box(MakeLines(10));
	box.SetSize(200, 40);
	ScrollBar* bar = box.GetVerticalScrollBar();
	EXPECT_TRUE(bar->IsVisible());

	// ① 可命中：条是**最后** AddChild ⇒ HitTest 逆序优先命中条（Widget.cpp:139-149）
	EXPECT_TRUE(box.HitTest(194, 20) == bar);

	// ② 权威归容器：条自身 SetOffset **不回触发回调**（K10）⇒ 手动改条不改 TextBox 的滚动状态
	box.MoveCaretToEnd();
	const float caretDriven = box.GetScrollOffsetY();
	EXPECT_TRUE(caretDriven > 0.0f);
	bar->SetOffset(0);
	EXPECT_EQ(bar->GetOffset(), 0);
	EXPECT_NEAR(box.GetScrollOffsetY(), caretDriven, kEps);   // TextBox 侧不受条影响
}

/// T5：**滚轮路径**（鼠标驱动 ⇒ 条跟随；口径 = int(TextBox 偏移)）
void TestTextBoxScrollBarFollowsWheel()
{
	TestableTextBox box(MakeLines(10));
	box.SetSize(200, 40);
	ScrollBar* bar = box.GetVerticalScrollBar();
	EXPECT_EQ(bar->GetOffset(), 0);

	box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -120));   // 向下滚一行（kScrollLinePx = 16）
	EXPECT_NEAR(box.GetScrollOffsetY(), 16.0f, kEps);
	EXPECT_EQ(bar->GetOffset(), 16);
	EXPECT_EQ(bar->GetOffset(), static_cast<int>(box.GetScrollOffsetY()));
}

/// T6：条可见时**文本区让位**（△5）——经命令流观测**用户可见后果**：文本区 Clip 真的变窄
/// @note 原稿直测 `GetTextAreaWidth()`，但该函数是 **private**（`using` 不能提升访问权，
/// 不像 protected 的 `CaretIndexFromPosition`）⇒ 改为观测其后果，反而更强端到端。
/// 用「同尺寸有/无条」两态**相减**，不依赖 padding 默认值。
void TestTextBoxScrollBarNarrowsTextArea()
{
	RecordingBackend backend;

	TestableTextBox fits(MakeLines(2));                      // 32 < 40 ⇒ 无条
	fits.SetSize(200, 40);
	CommandBuffer fitCommands;
	{
		PaintContext ctx(fitCommands, backend);
		fits.Paint(ctx, 0, 0);
	}
	const float wide = NthClipWidth(fitCommands, 1);         // 文本区 Clip 宽
	EXPECT_TRUE(wide > 0.0f);

	TestableTextBox over(MakeLines(10));                     // 160 > 40 ⇒ 有条
	over.SetSize(200, 40);
	ScrollBar* bar = over.GetVerticalScrollBar();
	EXPECT_TRUE(bar->IsVisible());
	CommandBuffer overCommands;
	{
		PaintContext ctx(overCommands, backend);
		over.Paint(ctx, 0, 0);
	}
	const float narrow = NthClipWidth(overCommands, 1);      // 让位后的文本区 Clip 宽
	EXPECT_TRUE(narrow > 0.0f);

	// ★ v1.5：让位量 = 条厚 **+ 条的内缩**（内缩 = max(padding, 2px)）——条不再贴边，内缩段也要让出
	const float inset = over.Padding() > 2.0f ? over.Padding() : 2.0f;
	EXPECT_NEAR(wide - narrow, static_cast<float>(bar->GetThickness()) + inset, kEps);
}

/// T7：★ **光标路径**（TextBox **自己**改 offset ⇒ 条跟随）——T5 只证明了鼠标驱动那一条
void TestTextBoxScrollBarFollowsCaret()
{
	TextBox box(MakeLines(10));
	box.SetSize(200, 40);
	ScrollBar* bar = box.GetVerticalScrollBar();
	EXPECT_EQ(bar->GetOffset(), 0);

	box.MoveCaretToEnd();                                    // EnsureCaretVisible → 滚到底
	EXPECT_TRUE(box.GetScrollOffsetY() > 0.0f);
	EXPECT_EQ(bar->GetOffset(), static_cast<int>(box.GetScrollOffsetY()));
}

/// T8：★ **惰性同步兜底**（△6 / D10）——改文本**不走任何显式同步**，仅靠一次 Paint 送达条
/// @note v1.1 原稿的 T8 写的是「字体路径」，但**无头环境下 `GetLineHeight()` 恒走 16px 兜底**
/// （无 Window 分支）⇒ 字体变化在测试里不可观测。本用例改为验证**同一机制**：
/// 「绕过全部显式钩子的变化，靠 OnPaint 起始处的惰性同步兜住」；字体路径留 A2 人工观感验收。
void TestTextBoxScrollBarLazySyncOnPaint()
{
	TextBox box(MakeLines(2));                               // 32 < 40 ⇒ 不溢出
	box.SetSize(200, 40);
	ScrollBar* bar = box.GetVerticalScrollBar();
	EXPECT_FALSE(bar->IsVisible());

	box.SetText(MakeLines(10));                              // SetText 只置行缓存失效 + Invalidate
	EXPECT_FALSE(bar->IsVisible());                          // ⇒ 未绘制前条仍未同步（证明无别处顺带同步）

	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 0, 0);                                    // ← 仅这一次绘制

	EXPECT_TRUE(bar->IsVisible());                           // 内容高变化经惰性同步送达条
}

/// T9：★ **手势隔离**——按下落在**条**上时不得进入文本拖选（缺陷修复 v1.4）
/// @details 根因：Application 的鼠标派发在 `HitTest` 命中后**沿父链冒泡**（`Application.cpp:294-302`）
/// ⇒ 按在条上时 `TextBox::OnMouseButtonDown` 照样被调用，曾被当成"文本点击"并进入拖选状态
/// ⇒ 拖条时文字被拖选（蓝底白字）、单击条还会移动光标。
void TestTextBoxScrollBarIgnoresPressOnBar()
{
	TestableTextBox box(MakeLines(10));
	box.SetSize(200, 40);
	ScrollBar* bar = box.GetVerticalScrollBar();
	EXPECT_TRUE(bar->IsVisible());

	// 条占据控件最右侧 thickness 宽（事件坐标 = 窗口客户区绝对）
	const int onBarX  = static_cast<int>(bar->GetAbsolutePosition().x) + 5;
	const int onTextX = 10;

	// ① 按在**条**上 → 再拖到文本区 ⇒ ★ **不得**产生选区（手势不属于文本框）
	box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, onBarX, 20, MouseButton::Left));
	box.OnMouseMove(MouseMoveEvent(nullptr, onTextX, 20));
	EXPECT_FALSE(box.GetSelection().has_value());

	// ② 对照：按在**文本区** → 拖到另一处 ⇒ 应当产生选区（守卫没有误伤正常拖选）
	box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, onTextX, 5, MouseButton::Left));
	box.OnMouseMove(MouseMoveEvent(nullptr, 60, 20));
	EXPECT_TRUE(box.GetSelection().has_value());
}

/// T10：★ 条**四边内缩**（v1.5）——不贴右缘、上下留边 ⇒ 不压 TextBox 的边框环 / 圆角
/// @details 内缩量 = `max(padding, 2px)`：`padding = 0`（框架默认）⇒ 保底 2px；
/// ModelProbe 的 `padding = 8` ⇒ 内缩 8px，与文本区内边距**对称**。
void TestTextBoxScrollBarInset()
{
	// ① 默认（padding = 0）⇒ 保底内缩 2px：右 / 上 / 下都不贴边
	{
		TestableTextBox box(MakeLines(10));
		box.SetSize(200, 40);
		ScrollBar* bar = box.GetVerticalScrollBar();
		EXPECT_TRUE(bar->IsVisible());

		const Point boxAbs = box.GetAbsolutePosition();
		const Point barAbs = bar->GetAbsolutePosition();
		const float inset = 2.0f;   // padding = 0 ⇒ 保底

		EXPECT_NEAR(barAbs.x + static_cast<float>(bar->GetWidth()),
		            boxAbs.x + static_cast<float>(box.GetWidth()) - inset, kEps);   // 右缘留边
		EXPECT_NEAR(barAbs.y - boxAbs.y, inset, kEps);                              // 顶边留边
		EXPECT_NEAR(static_cast<float>(bar->GetHeight()),
		            static_cast<float>(box.GetHeight()) - 2.0f * inset, kEps);      // 上下各留边
	}

	// ② ★ 仿 ModelProbe 的**调用次序**：先 SetSize、后 SetStyle 给 padding / 边框
	//    ⇒ 摆位必须随样式变更重算（只在 SetSize 里算会漏，条会停在旧内缩）
	{
		TestableTextBox box(MakeLines(10));
		box.SetSize(200, 40);
		box.SetStyle(TextBoxStyleOverride{ .padding = 8.0f, .borderWidth = 1.0f });
		ScrollBar* bar = box.GetVerticalScrollBar();
		EXPECT_TRUE(bar->IsVisible());

		const Point boxAbs = box.GetAbsolutePosition();
		const Point barAbs = bar->GetAbsolutePosition();

		EXPECT_NEAR(barAbs.y - boxAbs.y, 8.0f, kEps);                               // 上下内缩 8
		EXPECT_NEAR(barAbs.x + static_cast<float>(bar->GetWidth()),
		            boxAbs.x + static_cast<float>(box.GetWidth()) - 8.0f, kEps);    // 右侧内缩 8
		// ★ 用户报的原始症状：条**不得**压住 1px 边框环（环占 [宽−1, 宽)）
		EXPECT_TRUE(barAbs.x + static_cast<float>(bar->GetWidth())
		            <= boxAbs.x + static_cast<float>(box.GetWidth()) - 1.0f);
	}
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════
// Phase 30 批一：T30-1 caretColor（真窗口装置——caret 绘制需 GetWindow()->GetTextMeasurer()）
// ══════════════════════════════════════════════════════════════════════

namespace {

/// @brief caret 探针：真窗口（caret 绘制路径读 `GetWindow()->GetTextMeasurer()`——无窗口会空解引用，
///        故本用例**必须**真窗口；形态沿 WidgetTests 的 FingerprintProbe/WrapProbe）
struct CaretProbe{
	Application app;
	Window* window = nullptr;
	TestableTextBox* box = nullptr;

	CaretProbe() : CaretProbe(std::string("abc"), 150, 30) {}

	/// @brief ★ Phase 38 T38-11/T38-16：可配文本与尺寸的形态（默认构造委托到本构造 ⇒ 既有用例零改动）
	/// @details 折行用例必须能自己选「文本长度 × 控件宽」，否则折行宽（= 宽 − 条槽 14）无从设定。
	CaretProbe(std::string initialText, int width, int height){
		RenderServices services{
			std::make_unique<GDIBackend>(),
			std::make_unique<GDITextMeasurer>()
		};
		window = &app.Create("ECDI_T30_Caret", 200, 120, std::move(services));

		auto owned = std::make_unique<TestableTextBox>(std::move(initialText));
		box = owned.get();
		window->GetRootWidget().AddChild(std::move(owned));
		box->SetSize(width, height);
		box->OnFocusGained();   // m_showCaret = true（真窗口 ⇒ SyncTextInputCaret/StartTimer 均可达）
	}
	~CaretProbe(){ window->Release(); }

	/// @brief Paint 后收集「宽 == caretWidth」的 DrawRect 颜色（光标是唯一该宽的矩形——宽度即指纹）
	[[nodiscard]] std::vector<Color> PaintCaretColors() const{
		CommandBuffer commands;
		PaintContext ctx(commands, window->GetTextMeasurer());
		box->Paint(ctx, 0, 0);
		std::vector<Color> out;
		for (const auto& cmd : commands){
			if (const auto* rect = std::get_if<DrawRectCommand>(&cmd)){
				if (rect->rect.width == 2.0f) { out.push_back(rect->color); }   // 2.0f = 主题缺省 caretWidth
			}
		}
		return out;
	}
};

/// @brief 记录型假测量器（★ T38-14 的量具）
/// @details 与 `CountingFakeMeasurer` 同型（三个虚函数均**非 const**，见 `Render/TextMeasurer.h`），
///   额外把 `FitText` 每次收到的**布局串**按原样追加进 `fed`——掩码维（h）要求「喂进去的是不是显示串」
///   在**串内容层**可判，计数不够。
class RecordingFakeMeasurer final : public TextMeasurer{
public:
	int fitCalls = 0;
	int measureCalls = 0;
	std::vector<std::string> fed;   ///< 每次 FitText 收到的串（同一轮重建里的各段都是同一个整串）

	void Reset(){ fitCalls = 0; measureCalls = 0; fed.clear(); }

	Size MeasureText(const Font&, const std::string& text) override{
		++measureCalls;
		return Size{ static_cast<float>(CountCp(text)) * 8.0f, 16.0f };
	}

	/// ★ 必须覆写：默认体用码点二分反调 `MeasureText` ⇒ 两个计数互相污染，且 `fed` 拿不到整串。
	TextFit FitText(const Font&, const std::string& text, std::size_t startCp, float maxWidth) override{
		++fitCalls;
		fed.push_back(text);
		const std::size_t total = CountCp(text);
		if (startCp >= total)
			return TextFit{ 0, 0.0f };
		const std::size_t byWidth = static_cast<std::size_t>((std::max)(0.0f, maxWidth) / 8.0f);
		const std::size_t n = (std::min)(byWidth, total - startCp);   // fitCp 相对、不越串尾（C29-1）
		return TextFit{ n, static_cast<float>(n) * 8.0f };
	}

	float LineHeight(const Font&) override{ return 16.0f; }

	/// @brief 全部喂入串拼成一条（跨行检测子串/明文用）
	[[nodiscard]] std::string Joined() const{
		std::string out;
		for (const auto& one : fed)
			out += one;
		return out;
	}

private:
	static std::size_t CountCp(const std::string& s){
		std::size_t n = 0;
		for (char c : s)
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
				++n;
		return n;
	}
};

/// @brief 记录型可测 TextBox（理由同 `CountingTextBox`：`TestableTextBox` 的测量器焊死为静态 `ms_fake`）
class RecordingTextBox : public TextBox{
public:
	using TextBox::TextBox;
	using TextBox::RecalculateLines;
	using TextBox::LineRange;
	using TextBox::LineDrawRange;
	// ★ 组合入口是 protected（Window 转发口）——T38-14i 要用它翻组合三维
	using TextBox::UpdateComposition;
	using TextBox::CancelComposition;

	/// @brief 视觉行数（`m_lines` 是基类 private ⇒ 经 preferred 反推；同 `TestableTextBox` 的理由）
	[[nodiscard]] size_t RowCountForTest() const{
		return static_cast<size_t>(GetPreferredSize().height / 16.0f + 0.5f);
	}

	[[nodiscard]] std::pair<size_t, size_t> OwnForTest(size_t i){ RecalculateLines(); return LineRange(i); }

	static RecordingFakeMeasurer& Recorder() noexcept{ return ms_rec; }

protected:
	TextMeasurer* ResolveMeasurer() const override{ return &ms_rec; }

private:
	static RecordingFakeMeasurer ms_rec;
};

RecordingFakeMeasurer RecordingTextBox::ms_rec;

} // anonymous namespace

/// T30-1：caretColor 默认 Black / Override 生效 / ApplyTheme 不覆盖 Override（C30-1）
void Test30CaretColorStyle(){
	CaretProbe probe;

	// ① 默认（仅主题注入）：恰一条 caret 矩形，色 = Black（与硬编码期逐位一致——C30-1 零变化锚）
	{
		const std::vector<Color> colors = probe.PaintCaretColors();
		EXPECT_EQ(colors.size(), static_cast<std::size_t>(1));
		if (!colors.empty()) { EXPECT_TRUE(colors[0] == Color::Black()); }
	}

	// ② Override 自定义色 ⇒ 生效
	const Color red = Color::FromRGBA8(220, 30, 30);
	{
		TextBoxStyleOverride o;
		o.caretColor = red;
		probe.box->SetStyle(o);
	}
	{
		const std::vector<Color> colors = probe.PaintCaretColors();
		EXPECT_EQ(colors.size(), static_cast<std::size_t>(1));
		if (!colors.empty()) { EXPECT_TRUE(colors[0] == red); }
	}

	// ③ 再 ApplyTheme ⇒ **仍红**（StyleField D7：Override 不被主题覆盖——T30-1 的核心语义）
	probe.box->ApplyTheme(GetDefaultTheme());
	{
		const std::vector<Color> colors = probe.PaintCaretColors();
		EXPECT_EQ(colors.size(), static_cast<std::size_t>(1));
		if (!colors.empty()) { EXPECT_TRUE(colors[0] == red); }
	}
}

// ══════════════════════════════════════════════════════════════════════
// Phase 37 · T37-1：Top 逐位基准（A1 红线判据）
// 基准采集记录：详设 §3.4 第 5 步——源码基准 5adcbedb、FakeTextMeasurer（L≡16）采集，
// 改前/批一后两轮探针读数逐位一致（0 / 4,20 / 4,20,36,52）；常量表只断言、不重算公式。
// ══════════════════════════════════════════════════════════════════════

/// T37-1：缺省 Top 路径 pos.y 序列逐格等于常量表（B1/B2/B3——含单行 / 多行不足一屏 / 多行超屏）
void Test37BaselineTextY(){
	struct Case{ const char* text; int h; float pad; size_t n; const float* ys; };
	const float y1[] = { 0.0f };
	const float y2[] = { 4.0f, 20.0f };
	const float y4[] = { 4.0f, 20.0f, 36.0f, 52.0f };
	const Case cases[] = {
		{ "abc",        30, 0.0f, 1, y1 },
		{ "abc\ndef",   60, 4.0f, 2, y2 },
		{ "a\nb\nc\nd", 60, 4.0f, 4, y4 },
	};
	for (const auto& c : cases){
		TestableTextBox box(std::string(c.text));
		box.SetSize(80, c.h);
		if (c.pad != 0.0f){
			TextBoxStyleOverride o; o.padding = c.pad; box.SetStyle(o);
		}
		CommandBuffer commands;
		FakeTextMeasurer measurer;
		PaintContext ctx(commands, measurer);
		box.Paint(ctx, 0, 0);   // 命令坐标 = 控件局部 ⇒ 期望 pos.y = padding + i×L
		std::vector<float> ys;
		for (const auto& cmd : commands){
			if (const auto* t = std::get_if<DrawTextCommand>(&cmd))
				ys.push_back(t->pos.y);
		}
		EXPECT_EQ(ys.size(), c.n);
		const size_t check = (ys.size() < c.n) ? ys.size() : c.n;
		for (size_t i = 0; i < check; ++i)
			EXPECT_NEAR(ys[i], c.ys[i], kEps);
	}
}

/// T37-7：主题缺省 = Top（A5；C37-4 双保险——显式注入 + 枚举零值一致，缺省路径零行为变化）
void Test37ThemeDefaultTop(){
	// ① DefaultTheme 注入值直查（TextBoxStyle 公共结构体，字段 public 可读）
	EXPECT_EQ(GetDefaultTheme().GetTextBoxStyle().verticalAlign.value, VerticalAlignment::Top);
	// ② 缺省路径：ApplyTheme 后 Top 快路径 ⇒ inset == padding。
	//    判别力：H=30 / p=0 / n=1 若误为 Center 则 O=(30-16)/2=7 ⇒ inset=7 ≠ 0 ⇒ 断言必失败。
	TestableTextBox box(std::string("abc"));
	box.SetSize(80, 30);
	box.ApplyTheme(GetDefaultTheme());
	EXPECT_NEAR(box.GetTextTopInset(), box.Padding(), kEps);
}

/// T37-2：Center 生效（A2）——inset = padding + O，O = 7 / 10 / 0（v1.1 详设 §5 订正后的期望值）
void Test37CenterInset(){
	struct Case{ int h; float pad; const char* text; float expectInset; const char* note; };
	const Case cases[] = {
		{ 30, 0.0f, "abc",      7.0f,  "A=30 C=16 O=7" },          // 单行不足一屏
		{ 60, 4.0f, "abc\ndef", 14.0f, "A=52 C=32 O=10" },         // 多行不足一屏
		{ 40, 4.0f, "abc\ndef", 4.0f,  "A=32 C=32 O=0 上边界" },   // C=A ⇒ O=0
	};
	for (const auto& c : cases){
		TestableTextBox box(std::string(c.text));
		box.SetSize(80, c.h);
		TextBoxStyleOverride o; o.padding = c.pad;
		o.verticalAlign = VerticalAlignment::Center;
		box.SetStyle(o);
		EXPECT_NEAR(box.GetTextTopInset(), c.expectInset, kEps);   // note: c.note
	}
}

/// T37-3：三态原点（A3）——C=A / C>A / C>A+滚动（C37-1 不变量 2 + C37-7 行为等价）
void Test37CenterDegenerate(){
	// ① C=A：H=40 / p=4 / n=2 ⇒ A=32 C=32 ⇒ O=0 ⇒ inset == padding
	{
		TestableTextBox box(std::string("abc\ndef"));
		box.SetSize(80, 40);
		TextBoxStyleOverride o; o.padding = 4.0f;
		o.verticalAlign = VerticalAlignment::Center;
		box.SetStyle(o);
		EXPECT_NEAR(box.GetTextTopInset(), 4.0f, kEps);
	}
	// ② C>A：H=40 / p=4 / n=4 ⇒ C=64>A=32 ⇒ O=0（超屏退化）
	{
		TestableTextBox box(std::string("a\nb\nc\nd"));
		box.SetSize(80, 40);
		TextBoxStyleOverride o; o.padding = 4.0f;
		o.verticalAlign = VerticalAlignment::Center;
		box.SetStyle(o);
		EXPECT_NEAR(box.GetTextTopInset(), 4.0f, kEps);
	}
	// ③ C>A + 预置滚动（点击行 3 → EnsureCaretVisible；scrollOffsetY 为 private，点击是唯一无头预置手段）：
	//    滚动自证（点击前后序列必须变化）+ Top/Center 滚动后绘制序列逐位同（O 未混入滚动范围——C37-7）
	{
		auto paintTextY = [](TestableTextBox& b){
			CommandBuffer commands;
			FakeTextMeasurer measurer;
			PaintContext ctx(commands, measurer);
			b.Paint(ctx, 0, 0);
			std::vector<float> ys;
			for (const auto& cmd : commands)
				if (const auto* t = std::get_if<DrawTextCommand>(&cmd))
					ys.push_back(t->pos.y);
			return ys;
		};
		std::vector<float> topBefore, topAfter, centerAfter;
		{
			TestableTextBox box(std::string("a\nb\nc\nd"));   // H=60 / p=4 / n=4 ⇒ A=52 C=64 超屏
			box.SetSize(80, 60);
			TextBoxStyleOverride o; o.padding = 4.0f; box.SetStyle(o);
			topBefore = paintTextY(box);                       // 未滚动：4,20,36,52
			box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 1, 59, MouseButton::Left));   // (1,59) 控件内、行 3 ⇒ caret=6 ⇒ 滚动 s=12
			topAfter = paintTextY(box);
		}
		{
			TestableTextBox box(std::string("a\nb\nc\nd"));
			box.SetSize(80, 60);
			TextBoxStyleOverride o; o.padding = 4.0f;
			o.verticalAlign = VerticalAlignment::Center;
			box.SetStyle(o);
			box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 1, 59, MouseButton::Left));
			centerAfter = paintTextY(box);
		}
		EXPECT_EQ(topBefore.size(), static_cast<size_t>(4));   // 4 行全可见（滚动前）
		EXPECT_EQ(topAfter.size(), centerAfter.size());
		bool scrolled = topBefore.size() != topAfter.size();   // 行数变化 = 滚动发生的充分自证之一
		if (!scrolled){
			for (size_t i = 0; i < topBefore.size() && i < topAfter.size(); ++i)
				if (topBefore[i] != topAfter[i]) { scrolled = true; break; }
		}
		EXPECT_TRUE(scrolled);   // EnsureCaretVisible 确实滚动了（否则 ③ 退化为 ②）
		for (size_t i = 0; i < topAfter.size() && i < centerAfter.size(); ++i)
			EXPECT_NEAR(topAfter[i], centerAfter[i], kEps);   // C37-7：O 未混入滚动状态（s=12 时 O=0 ⇒ 两配置同序列）
	}
}

/// T37-4：光标与绘制同源（A4-1——真窗口；GetCaretClientGeometry 无条件解引用 GetWindow，TextBox.cpp:682）
void Test37CaretAlignsWithPaint(){
	CaretProbe probe;   // "abc" 1 行 / 150×30 ⇒ Center 时 O=(30−16)/2=7（判别力：光标若未跟随则 caretY=0 ≠ 7）
	TextBoxStyleOverride o;
	o.verticalAlign = VerticalAlignment::Center;
	probe.box->SetStyle(o);
	const Point abs = probe.box->GetAbsolutePosition();
	CommandBuffer commands;
	PaintContext ctx(commands, probe.window->GetTextMeasurer());
	probe.box->Paint(ctx, abs.x, abs.y);   // 命令坐标 = 客户区绝对（与 GetCaretClientGeometry 同系——初设 §1.7）
	float firstTextY = -1.0f;
	for (const auto& cmd : commands){
		if (const auto* t = std::get_if<DrawTextCommand>(&cmd)) { firstTextY = t->pos.y; break; }
	}
	EXPECT_NEAR(probe.box->GetCaretClientGeometry().rect.y, firstTextY, kEps);
}

/// T37-5：Center 下点击命中跟随绘制（A4-2）——y=22 = p+O+L/2（O=10 ≥ L/2=8，判别窗口内）
void Test37HitFollowsAlignCenter(){
	TestableTextBox box(std::string("abc\ndef"));   // H=60 / p=4 / n=2 ⇒ O=10
	box.SetSize(80, 60);
	TextBoxStyleOverride o; o.padding = 4.0f;
	o.verticalAlign = VerticalAlignment::Center;
	box.SetStyle(o);
	box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 1, 22, MouseButton::Left));
	EXPECT_EQ(box.GetCaret(), 0);   // 行 0 首码点（若命中未跟随 ⇒ (22−4)/16=1 ⇒ 得 4 ⇒ 失败）
}

/// T37-6：同坐标 Top 必得行 1（A4-3——判别力配对，排除 T37-5 因坐标选错而假通过）
void Test37HitFollowsAlignTop(){
	TestableTextBox box(std::string("abc\ndef"));
	box.SetSize(80, 60);
	TextBoxStyleOverride o; o.padding = 4.0f;   // Top（缺省，显式不设）
	box.SetStyle(o);
	box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 1, 22, MouseButton::Left));
	EXPECT_EQ(box.GetCaret(), 4);   // 行 1 首码点（floor((22−4)/16)=1；行 1 首 = "abc\n" 之后 = 码点 4）
}

/// T37-8：SetStyle 覆盖链（A5）——override 生效 / ApplyTheme 不覆盖 override（D7）/ 回 Top
void Test37SetStyleOverrideChain(){
	TestableTextBox box(std::string("abc\ndef"));
	box.SetSize(80, 60);
	TextBoxStyleOverride base; base.padding = 4.0f; box.SetStyle(base);
	{
		TextBoxStyleOverride c; c.verticalAlign = VerticalAlignment::Center;
		box.SetStyle(c);
	}
	EXPECT_NEAR(box.GetTextTopInset(), box.Padding() + 10.0f, kEps);   // ① Center 生效（O=10）
	box.ApplyTheme(GetDefaultTheme());                                  // ② 主题不覆盖 override（D7）
	EXPECT_NEAR(box.GetTextTopInset(), box.Padding() + 10.0f, kEps);
	{
		TextBoxStyleOverride t; t.verticalAlign = VerticalAlignment::Top;
		box.SetStyle(t);
	}
	EXPECT_NEAR(box.GetTextTopInset(), box.Padding(), kEps);           // ③ 回 Top
}

// ══════════════════════════════════════════════════════════════════════
// Phase 38 · 批 0：非折行态逐位基准（红线① 的判据 / 实施前置，**只加测试**）
// 采集口径 = 详设 §6 批 0：产品码一字未动（源码基准 6a3807b；Phase 38 本轮只入库文档）。
// 夹具 = TestableTextBox + FakeTextMeasurer（8 宽 / L≡16）+ padding 0 ⇒ 命令坐标 = (0, i×L)，
// 与 T37-1 同一手法（CommandBuffer 直读，不经 Window）。★ 三批实现后本块数值必须逐位不变。
// ══════════════════════════════════════════════════════════════════════

/// T38-1：非折行态命令级基准——`DrawTextCommand(pos.x / pos.y / text)` 与 `PushClipCommand.rect` 逐字段
/// @details 三组：① 含 `\n` 两行 · ② 单行超宽（整串一次发射，超宽交 clip）· ③ 空文本（OnPaint 早退 ⇒ 零命令）
void Test38BaselineOffPaint(){
	struct Case{ const char* text; int w; int h; size_t nText; const char* line0; const char* line1; };
	const Case cases[] = {
		{ "abc\ndef",   80, 60, 2, "abc", "def" },           // pos.y = 0 / 16
		{ "abcdefghij", 40, 30, 1, "abcdefghij", nullptr },  // 80 > 40：非折行 ⇒ 仍单条命令
		{ "",           80, 30, 0, nullptr, nullptr },       // :1345 早退 ⇒ 无 DrawText / 无 PushClip
	};
	for (const auto& c : cases){
		TestableTextBox box(std::string(c.text));
		box.SetSize(c.w, c.h);
		CommandBuffer commands;
		FakeTextMeasurer measurer;
		PaintContext ctx(commands, measurer);
		box.Paint(ctx, 0, 0);   // 命令坐标 = 控件局部（传 0,0 ⇒ pos 即内缩量）

		std::vector<const DrawTextCommand*> texts;
		std::vector<const PushClipCommand*> clips;
		for (const auto& cmd : commands){
			if (const auto* t = std::get_if<DrawTextCommand>(&cmd))
				texts.push_back(t);
			else if (const auto* p = std::get_if<PushClipCommand>(&cmd))
				clips.push_back(p);
		}

		EXPECT_EQ(texts.size(), c.nText);
		// ★ clip 条数 = Widget 自身边界（`Widget.cpp:506`）+ TextBox 文本区（`TextBox.cpp:1369`）；
		//   空文本时 OnPaint 早退（`:1345`）⇒ 只剩 Widget 那条。p=0 且条不可见时两者数值相同，
		//   故取 **最后一条**（= TextBox 文本区）作判据，条数本身也一并钉住。
		EXPECT_EQ(clips.size(), (c.nText == 0) ? static_cast<std::size_t>(1) : static_cast<std::size_t>(2));
		if (!clips.empty()){
			const Rect& r = clips.back()->rect;
			EXPECT_NEAR(r.x, 0.0f, kEps);
			EXPECT_NEAR(r.y, 0.0f, kEps);
			EXPECT_NEAR(r.width, static_cast<float>(c.w), kEps);
			EXPECT_NEAR(r.height, static_cast<float>(c.h), kEps);
		}

		const size_t check = (texts.size() < c.nText) ? texts.size() : c.nText;
		for (size_t i = 0; i < check; ++i){
			EXPECT_NEAR(texts[i]->pos.x, 0.0f, kEps);
			EXPECT_NEAR(texts[i]->pos.y, static_cast<float>(i) * 16.0f, kEps);   // L_geom ≡ 16
			const char* want = (i == 0) ? c.line0 : c.line1;
			if (want != nullptr)
				EXPECT_TRUE(texts[i]->text == std::string(want));
		}
	}
}

/// T38-13：非折行态 preferred 逐位基准——宽 = 当前宽（多行）/ 测量宽（单行）、高 = n×L + 2p
void Test38BaselineOffPreferred(){
	// ① 单行超宽：宽 = 测量宽（内容宽口径）、高 = L
	{
		TestableTextBox box(std::string("abcdefghij"));
		box.SetSize(40, 30);
		const Size s = box.GetPreferredSize();
		EXPECT_NEAR(s.width, 80.0f, kEps);
		EXPECT_NEAR(s.height, 16.0f, kEps);
	}
	// ② 含 \n（p = 4）：宽 = 当前宽、高 = 2×16 + 8 = 40（ceil 归属本函数——契约①）
	{
		TestableTextBox box(std::string("abc\ndef"));
		box.SetSize(80, 60);
		TextBoxStyleOverride o; o.padding = 4.0f;
		box.SetStyle(o);
		const Size s = box.GetPreferredSize();
		EXPECT_NEAR(s.width, 80.0f, kEps);
		EXPECT_NEAR(s.height, 40.0f, kEps);
	}
	// ③ 单行开关 on（无 \n）：仍走单行支——该开关在非折行态不影响 preferred
	{
		TestableTextBox box(std::string("abc"));
		box.SetSize(80, 30);
		box.SetSingleLine(true);
		const Size s = box.GetPreferredSize();
		EXPECT_NEAR(s.width, 24.0f, kEps);
		EXPECT_NEAR(s.height, 16.0f, kEps);
	}
	// ④ 空文本：宽 0（诚实测量）+ 高 = L
	{
		TestableTextBox box(std::string(""));
		box.SetSize(80, 30);
		const Size s = box.GetPreferredSize();
		EXPECT_NEAR(s.width, 0.0f, kEps);
		EXPECT_NEAR(s.height, 16.0f, kEps);
	}
}


// ══════════════════════════════════════════════════════════════════════
// Phase 38 · 批 2：行表换芯（T38-3..7 / T38-15 / T38-18）——全无头
// 期望值全部来自 kWrapSamples（= 详设 §3.1/§3.4 的脚本实算）
// ══════════════════════════════════════════════════════════════════════

/// @brief 逐行比对样本表：行数 + 每行 draw/own 三字段
/// @details ★ 行数用 `RowCountForTest()`（preferred 反推）而不是行表 `.size()`——
///   `m_lines` 是基类 private，派生类不可访问；顺带把「行表行数 == preferred 行数」钉死。
static void ExpectRows(TestableTextBox& box, const WrapSample& sample){
	EXPECT_EQ(box.RowCountForTest(), sample.lineCount);
	const std::size_t check = (sample.lineCount < 4) ? sample.lineCount : 4;
	for (std::size_t i = 0; i < check; ++i){
		const auto draw = box.DrawForTest(i);
		const auto own  = box.OwnForTest(i);
		EXPECT_EQ(draw.first,  sample.lines[i].drawStart);
		EXPECT_EQ(draw.second, sample.lines[i].drawEnd);
		EXPECT_EQ(own.first,   sample.lines[i].drawStart);   // 两口径起点同值
		EXPECT_EQ(own.second,  sample.lines[i].ownEnd);
	}
}

/// T38-3：S1@48 —— 绘制 / 归属 / 点击 / 行 y / Up-Down **四路同源**
/// @details 「四路」= `LineDrawRange`（绘制）· `LineRange`（归属）· `CaretIndexFromPosition`（Y 定行）·
///   `DrawTextCommand.pos.y`（发射行 y）。任一路用了自己的行数/行高口径，本条就分叉。
void Test38SoftWrapS1FourWaySameSource(){
	const WrapSample& sample = FindSample("ab cd ef", 48.0f);
	WrapProbe probe;
	probe.Set(sample);

	EXPECT_TRUE(probe.box.WrapRequested());
	EXPECT_TRUE(probe.box.IsWrapActive());   // 无头 + 注入测量器 ⇒ 折行**生效**（K6 的直接证据）
	ExpectRows(probe.box, sample);           // ① 绘制口径 + ② 归属口径

	// ③ Y 定行：L1 的 y 带 ⇒ 无头下退化为「返回行首」（`CaretIndexFromLineX` 的 GetWindow() 守卫）
	EXPECT_EQ(probe.box.CaretIndexFromPosition(Point{ 10.0f, 8.0f }), 0u);
	EXPECT_EQ(probe.box.CaretIndexFromPosition(Point{ 10.0f, 16.0f + 8.0f }), 6u);

	// ④ 行 y：命令级。★ 不用 `CalculateCaretPosition`（private，派生类不可暴露）——
	//    DrawText 的 pos.y 更硬：它同时证明布局行高与发射行高是同一份。
	CommandBuffer commands;
	FakeTextMeasurer measurer;
	PaintContext ctx(commands, measurer);
	probe.box.Paint(ctx, 0, 0);
	std::vector<float> ys;
	for (const auto& cmd : commands){
		if (const auto* text = std::get_if<DrawTextCommand>(&cmd))
			ys.push_back(text->pos.y);
	}
	EXPECT_EQ(ys.size(), 2u);
	if (ys.size() == 2){
		EXPECT_NEAR(ys[0], 0.0f, kEps);    // 行 0：textY = padding 0
		EXPECT_NEAR(ys[1], 16.0f, kEps);   // 行 1：+1 × lineH
	}

	// ⑤ Up/Down 行号：L1 → Up → L0；再 Down → L1 行首。
	//    ★ 只断行号：无头下 `CaretIndexFromLineX` 恒返回行首，「跨行保持列」必须在
	//    真窗口相验（T38-16②）——这里不假装验了它。
	probe.box.MoveCaretToEnd();
	EXPECT_EQ(probe.box.GetCaret(), 8u);   // 行 1
	probe.box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
	EXPECT_EQ(probe.box.GetCaret(), 0u);   // 行 0 行首
	probe.box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
	EXPECT_EQ(probe.box.GetCaret(), 6u);   // 行 1 行首
}

/// T38-4：S2@40 —— 纯 CJK 无空格 ⇒ 两口径**逐行同值**
/// @details 这一条是「归属规则不凭空吞码点」的对照：没有分隔空格可归还，`drawEnd == ownedEnd`。
void Test38SoftWrapS2CjkNoSpace(){
	const WrapSample& sample = FindSample("一二三四五六", 40.0f);
	WrapProbe probe;
	probe.Set(sample);
	ExpectRows(probe.box, sample);
	for (std::size_t i = 0; i < sample.lineCount; ++i){
		EXPECT_EQ(probe.box.DrawForTest(i).second, probe.box.OwnForTest(i).second);
		EXPECT_EQ(probe.box.DrawForTest(i).second - probe.box.DrawForTest(i).first,
		          probe.box.OwnForTest(i).second - probe.box.OwnForTest(i).first);
	}
}

/// T38-5：S3@40 / S3@24 —— 硬行与软行在**同一张表**里混存
/// @details 判据只两条：软行 `ownedEnd == 下一行 start`、硬行 `ownedEnd + 1 == 下一行 start`。
///   之所以值得单独一条：这两种相邻关系**同时**出现在一张表里，任何「统一 +1」或「统一 −1」
///   的实现都会在其中一半上红。
void Test38SoftWrapS3HardSoftMixed(){
	{
		const WrapSample& sample = FindSample("ab cd \nef gh", 40.0f);   // 3 行：软 · 硬
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		EXPECT_EQ(probe.box.OwnForTest(0).second, probe.box.OwnForTest(1).first);       // 软行：空格归前行
		EXPECT_EQ(probe.box.OwnForTest(1).second + 1, probe.box.OwnForTest(2).first);   // 硬行：`\n` 无主
	}
	{
		const WrapSample& s24 = FindSample("ab cd \nef gh", 24.0f);      // 4 行：软 · 硬 · 软
		WrapProbe probe;
		probe.Set(s24);
		ExpectRows(probe.box, s24);
		EXPECT_EQ(probe.box.OwnForTest(1).second + 1, probe.box.OwnForTest(2).first);   // 仍在 `\n` 处
		EXPECT_EQ(probe.box.OwnForTest(2).second, probe.box.OwnForTest(3).first);       // 第 4 行是软行
	}
}

/// T38-6：五个禁则样本的四路同源（S4 悬挂 / S5 行首禁则 / S5b 守卫 / S5c 对照 / S6 超长词）
/// @details 禁则的三条语义（悬挂闭标点、开括号回退、不产生空行）在引擎里只有一份实现；
///   本条验的是「`TextBox` 把它原样用上了」，不是「引擎对不对」（那归 Phase 29）。
void Test38SoftWrapKinsokuFiveSamples(){
	struct Case{ const char* text; float width; };
	const Case cases[] = {
		{ "一二三。四", 24.0f },   // S4：悬挂行宽 32 > 折行宽 24
		{ "ab（cd",     24.0f },   // S5：④ 行尾开括号回退 ⇒ `（` 挪到 L1
		{ "（a",         8.0f },   // S5b：④b 守卫 ⇒ 不产生空行
		{ "一二三（四",  32.0f },   // S5c：对照组——④ 在纯 CJK 串上不可达
		{ "abcdefgh",   26.0f },   // S6：超长词硬断（无软断点）
	};
	for (const auto& one : cases){
		const WrapSample& sample = FindSample(one.text, one.width);
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		// 四路同源之二：每行 y 带点击 ⇒ 落该行起点（Y 定行与行表同源）
		for (std::size_t i = 0; i < sample.lineCount; ++i){
			EXPECT_EQ(probe.box.CaretIndexFromPosition(
				Point{ 0.0f, static_cast<float>(i) * 16.0f + 8.0f }), sample.lines[i].drawStart);
		}
	}
}

/// T38-7：★★ **码点守恒**（全表遍历，非抽样）——22 个样本 × 逐行
/// @details 判据（v1.1 换形，**只允许两态**）：
///   `gap = start(i+1) − ownedEnd(i)` **== 0**（软行）或 **== 1 且 `cps[ownedEnd] == '\n'`**（硬行）；
///   三条边界等式：`start(0) == 0` · `ownedEnd(last) == count` · `drawEnd(i) ≤ ownedEnd(i)`。
///   ★ v1.0 的 `gap ∈ {0,1}` 写法已作废：它在 `ownedEnd = b` 下把「gap==1 且 cps==' '」
///   （S8a/S8c 实测）判成合规，而那正是 A-15 的缺陷本体。
///   ★ 表里 23 项（18 + tail + S8 组 4）——含 `tail` 是因为尾部空行是 `ownedEnd == count` 的
///   独立形态（空行的 `ownedEnd` 取自身起点），漏它则边界等式② 少一半判别力。
void Test38SoftWrapCodepointConservation(){
	for (const auto& sample : kWrapSamples){
		WrapProbe probe;
		probe.Set(sample);
		const std::string text(sample.text);
		const std::size_t count = ByteOffsetToCodepointIndex(text, text.size());

		EXPECT_EQ(probe.box.RowCountForTest(), sample.lineCount);
		EXPECT_EQ(probe.box.OwnForTest(0).first, 0u);   // 边界等式①

		std::size_t lastOwn = 0;
		for (std::size_t i = 0; i < sample.lineCount; ++i){
			const auto draw = probe.box.DrawForTest(i);
			const auto own  = probe.box.OwnForTest(i);
			EXPECT_EQ(draw.first, own.first);
			EXPECT_TRUE(draw.second <= own.second);   // 边界等式③：裁剪 ⊆ 归属
			lastOwn = own.second;

			if (i + 1 < sample.lineCount){
				const std::size_t nextStart = probe.box.OwnForTest(i + 1).first;
				EXPECT_TRUE(nextStart >= own.second);   // 绝不重叠
				const std::size_t gap = (nextStart >= own.second) ? (nextStart - own.second) : 0;
				if (gap != 0){
					EXPECT_EQ(gap, static_cast<std::size_t>(1));        // 只允许 `\n` 那一个缺口
					EXPECT_EQ(CodepointAt(text, own.second), U'\n');   // 且缺口处必须是换行符
				}
			}
		}
		EXPECT_EQ(lastOwn, count);   // 边界等式②
	}
}

/// T38-15：键命中零测量 + 重建的线性判据
/// @details ★ 样本**必须**是无 `\n` 的单段串（A-16）：空段走引擎的 `continue`、**不调 `FitText`**
///   ⇒ 含 `\n` 的串会让「行数 == fitCalls」假红（`"ab\n"` 是 2 行但 `fitCalls == 1`）。
void Test38SoftWrapKeyHitZeroMeasure(){
	CountingTextBox box(std::string("abcdefgh"));   // S6@26 ⇒ 3 行
	box.SetSize(26 + kWrapBarSlot, 64);
	box.SetWordWrap(true);
	box.RecalculateLines();                          // 首次建表（键未命中）
	CountingFakeMeasurer& counter = CountingTextBox::Counter();

	// ① 键全同 ⇒ 零 FitText / 零 MeasureText（含 `GetPreferredSize` 这条每帧路径）
	counter.fitCalls = 0;
	counter.measureCalls = 0;
	box.RecalculateLines();
	EXPECT_EQ(counter.fitCalls, 0);
	EXPECT_EQ(counter.measureCalls, 0);
	EXPECT_EQ(box.RowCountForTest(), 3u);
	EXPECT_EQ(counter.fitCalls, 0);      // preferred 也走键守卫 ⇒ 仍为零

	// ② 重建一次 ⇒ `行数 == fitCalls`（线性：3 行 3 次，不是 O(行数²) 也不是 1 次全串）
	counter.fitCalls = 0;
	counter.measureCalls = 0;
	box.SetText(std::string("abcdefgh"));   // revision++ ⇒ 键变 ⇒ 必重建
	EXPECT_EQ(box.RowCountForTest(), 3u);
	EXPECT_EQ(counter.fitCalls, 3);
	EXPECT_EQ(counter.measureCalls, 0);     // 覆写 FitText ⇒ 不再反调 MeasureText
}

/// T38-18：★★ 分隔空格归属（A-15 的判别面）
/// @details 旧口径（`ownedEnd = b`）下：S8a/S8c 的分隔空格**不属于任何行** ⇒ 点不到、选不了、
///   `Left/Right` 跨不过去；S8d 的尾部空格串让末行 `ownedEnd < count`（边界等式② 破）。
///   ★ 负对照（手工）：注释掉 `TextWidget.cpp` 的 `layout.lines.back().ownedEnd = nextScan;`
///   ⇒ 本条 ①③ 必红——这是「回归到旧口径会被抓住」的直接证据，不是推测。
void Test38SoftWrapSeparatorSpaceOwned(){
	// ① S8a：分隔空格归前行、但不发射 —— 两口径分离在软断点空格上的实证
	{
		const WrapSample& sample = FindSample("一二三 四", 24.0f);
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		EXPECT_EQ(probe.box.OwnForTest(0).second, 4u);    // 归属含空格（cp3）
		EXPECT_EQ(probe.box.DrawForTest(0).second, 3u);   // 绘制不含（软断点行尾被裁）
		EXPECT_EQ(probe.box.OwnForTest(1).first, 4u);     // 与前行归属连续
	}
	// ② S8b：**连续两个**空格都归前行（旧口径下 gap == 2）
	{
		const WrapSample& sample = FindSample("一二三  四", 24.0f);
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		EXPECT_EQ(probe.box.OwnForTest(0).second, 5u);
		EXPECT_EQ(probe.box.OwnForTest(1).first, 5u);
	}
	// ③ S8c：空格归前行、`\n` 无主（gap == 1 且是换行符 —— 唯一的合法缺口）
	{
		const WrapSample& sample = FindSample("一二三 \n四", 24.0f);
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		EXPECT_EQ(probe.box.OwnForTest(0).second, 4u);
		EXPECT_EQ(probe.box.OwnForTest(1).first, 5u);
		EXPECT_EQ(CodepointAt(std::string(sample.text), probe.box.OwnForTest(0).second), U'\n');
	}
	// ④ S8d：尾部空格串不得丢 —— 末行归属终点 == 码点总数
	{
		const WrapSample& sample = FindSample("abc   ", 40.0f);
		WrapProbe probe;
		probe.Set(sample);
		ExpectRows(probe.box, sample);
		EXPECT_EQ(probe.box.OwnForTest(0).second,
		          ByteOffsetToCodepointIndex(std::string("abc   "), 6));   // == 6
	}
	// ⑤ 可达性：行内 X 的落点必须在本行**归属**区间内
	//    （无头下 `CaretIndexFromLineX` 走「返回行首」退化 ⇒ 只断区间归属，行内精确定位归 T38-16②）
	{
		const WrapSample& sample = FindSample("一二三 四", 24.0f);
		WrapProbe probe;
		probe.Set(sample);
		const std::size_t hit = probe.box.CaretIndexFromLineX(0, 24.0f);
		EXPECT_TRUE(hit >= probe.box.OwnForTest(0).first);
		EXPECT_TRUE(hit <  probe.box.OwnForTest(0).second);
	}
}


// ══════════════════════════════════════════════════════════════════════
// Phase 38 · 批 3：接线（T38-8 / 9 / 10 / 12 / 17）——全无头
// 产品码已随批 2 落地，本批只补断言
// ══════════════════════════════════════════════════════════════════════

/// @brief 一次 Paint 的命令级快照（无头）
/// @details ★★ **clip 必须按发射顺序收集，不能只取最后一条**——`Widget::Paint` 的顺序是
///   `PushClip(self)` → `OnPaint`（里面 PushClip 文本区 / PopClip）→ **遍历子控件**
///   （垂直条可见时它自己也 `PushClip`）→ `PopClip`。所以条出现时 `clips.back()` 是**条**
///   （宽 12），不是文本区。⇒ `clipWidths[0]` 恒 = 控件自身边界、`clipWidths[1]` = 文本区
///   （仅当 OnPaint 未早退，即文本非空）。★ 批 0 的 T38-1 用 `clips.back()` 是对的，
///   因为那三组样本的条都不可见——但那不是「取 back 总是对的」，是「那些样本里恰好对」。
struct PaintProbe{
	std::vector<DrawTextCommand>  texts;
	std::vector<DrawRectCommand>  rects;
	std::vector<float>            clipWidths;
};

static PaintProbe PaintOnce(Widget& widget, TextMeasurer& measurer){
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	widget.Paint(ctx, 0, 0);
	PaintProbe out;
	for (const auto& cmd : commands){
		if (const auto* text = std::get_if<DrawTextCommand>(&cmd))
			out.texts.push_back(*text);
		else if (const auto* rect = std::get_if<DrawRectCommand>(&cmd))
			out.rects.push_back(*rect);
		else if (const auto* clip = std::get_if<PushClipCommand>(&cmd))
			out.clipWidths.push_back(clip->rect.width);
	}
	return out;
}

/// T38-8：折行宽**不随垂直条显隐变化**（D38-C 常驻槽的判别面）
/// @details 判据 = 两态**行表逐项相等**。`GetTextAreaWidth()` 是 private，不能直接断它的值；
///   但它一旦依赖 `barVisible`，`H=24`（条可见）就会少 14px ⇒ 折行宽 12 ⇒ `"abcdefgh"` 变 8 行，
///   而行表相等这条会立刻红。⇒ 行表是折行宽的**行为指纹**，比读私有字段更结实。
void Test38SoftWrapWidthIndependentOfBar(){
	const std::string text("abcdefgh");   // 折行宽 26 ⇒ 3 行（S6 口径）
	TestableTextBox tall(text);
	TestableTextBox cramped(text);
	tall.SetWordWrap(true);
	cramped.SetWordWrap(true);
	tall.SetSize(26 + kWrapBarSlot, 64);      // 折行宽 26 ⇒ 3 行；内容高 48 ≤ 64 ⇒ 条隐藏
	cramped.SetSize(26 + kWrapBarSlot, 24);   // 同一折行宽；内容高 48 > 24 ⇒ 条可见

	EXPECT_EQ(tall.RowCountForTest(), 3u);
	EXPECT_EQ(cramped.RowCountForTest(), 3u);
	for (std::size_t i = 0; i < 3; ++i){
		const auto dt = tall.DrawForTest(i);
		const auto df = cramped.DrawForTest(i);
		EXPECT_EQ(dt.first,  df.first);
		EXPECT_EQ(dt.second, df.second);
		const auto ot = tall.OwnForTest(i);
		const auto of = cramped.OwnForTest(i);
		EXPECT_EQ(ot.first,  of.first);
		EXPECT_EQ(ot.second, of.second);
	}
	// preferred 宽恒 == 当前宽：折行不制造宽度约束（与基类 wrap 支同一条纪律）
	EXPECT_NEAR(tall.GetPreferredSize().width, static_cast<float>(tall.GetWidth()), kEps);
	EXPECT_NEAR(cramped.GetPreferredSize().width, static_cast<float>(cramped.GetWidth()), kEps);
}

/// T38-9：软断点处的分隔空格**可达**（A-15 的用户可见面）
/// @details 旧口径 `ownedEnd = b` 让 `"ab cd ef"@48` 的 L0 归属区间是 `[0,5)`，而空格在 cp5
///   ⇒ 光标停不到、`Right` 跨不过、点击落不回本行（三个消费点全走 `LineRange`）。
///   现在归属区间是 `[0,6)`，cp5 在内。
void Test38SoftWrapSpaceReachable(){
	const WrapSample& sample = FindSample("ab cd ef", 48.0f);
	WrapProbe probe;
	probe.Set(sample);

	const auto own0 = probe.box.OwnForTest(0);
	EXPECT_EQ(own0.first, 0u);
	EXPECT_EQ(own0.second, 6u);            // ★ 含分隔空格 cp5（旧口径是 5）

	// ① 光标能停在空格位 cp5
	for (int i = 0; i < 5; ++i)
		probe.box.MoveCaret(TextBox::CaretDirection::Right);
	EXPECT_EQ(probe.box.GetCaret(), 5u);
	// ② 该位落在本行**归属**区间内
	EXPECT_TRUE(probe.box.GetCaret() >= own0.first && probe.box.GetCaret() < own0.second);
	// ③ Right 跨过空格进下一行、Left 又回得来
	probe.box.MoveCaret(TextBox::CaretDirection::Right);
	EXPECT_EQ(probe.box.GetCaret(), 6u);
	probe.box.MoveCaret(TextBox::CaretDirection::Left);
	EXPECT_EQ(probe.box.GetCaret(), 5u);
	// ④ 点击 L0 行尾（x 远大于行宽）仍落回本行归属区间
	//    ★ 无头下 `CaretIndexFromLineX` 走「无窗口 ⇒ 返回行首」的守卫（`TextBox.cpp` 的
	//      `GetWindow() != nullptr` 判断）⇒ 只断区间归属，行内精确定位归 T38-16②
	const std::size_t hit = probe.box.CaretIndexFromPosition(Point{ 200.0f, 8.0f });
	EXPECT_TRUE(hit >= own0.first && hit < own0.second);
}

/// T38-10：跨软行的选区高亮连续 + IME 组合下划线切段
/// @details 两条都用**归属**口径求交（`ownEnd`），不是绘制口径：
///   ① 选区：L0 高亮宽 = 6 码点 × 8 = **48**（含分隔空格）；若按绘制口径（5 码点）只有 40
///      ⇒ 行尾留 8px 未高亮 = 用户看到的「高亮断口」。
///   ② 下划线：组合区间跨几行就应有几段，每段落在本行——段数由行表算出，不硬编码。
void Test38SoftWrapSelectionAndImeContinuous(){
	const Color kSel = Color::FromRGBA8(1, 2, 3);   // 独特色：不与任何主题色相撞

	// ① 全选（Shift+Right × 8）后按颜色取高亮矩形
	{
		const WrapSample& sample = FindSample("ab cd ef", 48.0f);
		WrapProbe probe;
		probe.Set(sample);
		TextBoxStyleOverride style;
		style.selection = kSel;
		probe.box.SetStyle(style);
		for (int i = 0; i < 8; ++i)
			probe.box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));

		FakeTextMeasurer measurer;
		const PaintProbe painted = PaintOnce(probe.box, measurer);
		std::vector<DrawRectCommand> hl;
		for (const auto& rect : painted.rects)
			if (rect.color == kSel)
				hl.push_back(rect);
		EXPECT_EQ(hl.size(), 2u);   // 两行各一段
		if (hl.size() == 2){
			EXPECT_NEAR(hl[0].rect.width, 48.0f, kEps);   // ★ 6 码点（含空格），不是 40
			EXPECT_NEAR(hl[1].rect.width, 16.0f, kEps);   // 2 码点
			// 两段左缘都在文本区起点（L1 的行内偏移从 0 起）
			EXPECT_NEAR(hl[0].rect.x, hl[1].rect.x, kEps);
		}
	}

	// ② IME 组合下划线：段数 == 组合区间跨越的行数
	{
		const Color kComp = Color::FromRGBA8(4, 5, 6);
		TestableTextBox box(std::string("ab cd ef"));
		box.SetWordWrap(true);
		box.SetSize(48 + kWrapBarSlot, 64);
		TextBoxStyleOverride style;
		style.composition = kComp;
		box.SetStyle(style);
		for (int i = 0; i < 4; ++i)
			box.MoveCaret(TextBox::CaretDirection::Right);   // caret 4
		box.UpdateComposition("一二三四");                    // 组合区间 = [4, 8)

		const std::size_t compStart = 4;
		const std::size_t compEnd   = 8;
		const std::size_t rows = box.RowCountForTest();
		std::size_t crossed = 0;
		for (std::size_t i = 0; i < rows; ++i){
			const auto own = box.OwnForTest(i);
			if (own.first < compEnd && own.second > compStart)
				++crossed;
		}
		EXPECT_TRUE(crossed >= 2);   // 前提：确实跨行（否则本条没有判别力）

		FakeTextMeasurer measurer;
		const PaintProbe painted = PaintOnce(box, measurer);
		std::size_t segments = 0;
		for (const auto& rect : painted.rects){
			if (rect.color == kComp){
				++segments;
				EXPECT_TRUE(rect.rect.width > 0.0f);   // 每段非空
			}
		}
		EXPECT_EQ(segments, crossed);
	}
}

/// T38-12：preferred 稳定 + 窄带无宽度棘轮（D38-D 的判别面）
/// @details `GetPreferredSize` 的闸若带「折行宽 > 0」合取项，`0 < W ≤ 2p+14` 的窄带会落回基类
///   wrap 支（宽 = 测量宽 64）⇒ `AutoSize` 把宽撑开 ⇒ 再按新宽重排 ⇒ **宽度自激励**。
///   判据 = 窄带两档 `AutoSize` 两次后宽都不变。
void Test38SoftWrapPreferredStable(){
	const WrapSample& sample = FindSample("abcdefgh", 26.0f);   // 3 行
	WrapProbe probe;
	probe.Set(sample);
	const Size pref = probe.box.GetPreferredSize();
	EXPECT_NEAR(pref.height, 48.0f, kEps);   // 3 × L_pref(16) + 2p(0)
	EXPECT_NEAR(pref.width, static_cast<float>(probe.box.GetWidth()), kEps);

	// preferred 行数 == OnPaint 发射的 DrawText 条数（本样本无空行 ⇒ 一一对应）
	FakeTextMeasurer measurer;
	EXPECT_EQ(PaintOnce(probe.box, measurer).texts.size(), 3u);

	// 连续两次 AutoSize：高 64 → 48，第二次 no-op；宽全程不变
	EXPECT_TRUE(probe.box.AutoSize());
	EXPECT_EQ(probe.box.GetHeight(), 48);
	EXPECT_EQ(probe.box.GetWidth(), 26 + kWrapBarSlot);
	EXPECT_FALSE(probe.box.AutoSize());
	EXPECT_EQ(probe.box.GetWidth(), 26 + kWrapBarSlot);

	// ★ 窄带两档：W = 2p + 槽（折行宽 0）与 W = 1（折行宽 −13）
	for (int width : { kWrapBarSlot, 1 }){
		TestableTextBox box(std::string("abcdefgh"));
		box.SetWordWrap(true);
		box.SetSize(width, 64);
		box.AutoSize();
		EXPECT_EQ(box.GetWidth(), width);   // 宽没被撑成测量宽
		box.AutoSize();
		EXPECT_EQ(box.GetWidth(), width);   // 第二次也不动 ⇒ 无棘轮
	}
}

/// T38-17：折行态 clip 取**控件内边界**（D38-I 的判别面）
/// @details 夹具 = §3.3：`W=38 / H=40 / p=0 / wrap on / "一二三。四"` ⇒ 折行宽 24 ⇒ 2 行，
///   L0 是 4 码点（行宽 32 > 24，悬挂闭标点）。折行期 `scrollOffsetX ≡ 0` ⇒ clip 必须放宽到
///   控件内边界 38，否则 `。` 整字永久不可见。负对照（clip 仍取 24）会让第一条断言直接红。
void Test38SoftWrapClipIsInnerBoundary(){
	TestableTextBox box(std::string("一二三。四"));
	box.SetWordWrap(true);
	box.SetSize(38, 40);
	EXPECT_EQ(box.OwnForTest(0).second, 4u);   // L0 悬挂（4 码点 = 32 > 24）
	EXPECT_EQ(box.RowCountForTest(), 2u);

	FakeTextMeasurer measurer;

	// ① 条隐藏半段（内容高 32 ≤ 40）
	{
		const PaintProbe painted = PaintOnce(box, measurer);
		EXPECT_EQ(painted.clipWidths.size(), 2u);    // Widget self + 文本区（条不可见 ⇒ 不参与）
		if (painted.clipWidths.size() >= 2)
			EXPECT_NEAR(painted.clipWidths[1], 38.0f, kEps);   // ★ 38，不是 GetTextAreaWidth() 的 24
		EXPECT_EQ(painted.texts.size(), 2u);
		if (!painted.texts.empty()){
			// 悬挂行完整落在 clip 内：pos.x(0) + 行宽(32) ≤ clip 右缘(38)
			EXPECT_TRUE(painted.texts[0].pos.x + 32.0f <= 38.0f + kEps);
		}
	}
	// ② 条可见半段（H=24 ⇒ 内容高 32 > 24）：clip 宽**不因条出现而变**，命令仍发射
	//    ★ 此时 clip 有三条（self / 文本区 / 条）——必须取 [1]，取 back 会读到条自身的高
	{
		box.SetSize(38, 24);
		const PaintProbe painted = PaintOnce(box, measurer);
		EXPECT_TRUE(painted.clipWidths.size() >= 2u);
		if (painted.clipWidths.size() >= 2)
			EXPECT_NEAR(painted.clipWidths[1], 38.0f, kEps);
		EXPECT_EQ(painted.texts.size(), 2u);
	}
	// ③ 非折行半段（H=10 ⇒ 单行 16 > 10 ⇒ 条可见）：clip 回到 `GetTextAreaWidth()`
	//    = 38 − (12 + 2) = 24 —— 与折行态的 38 形成对照，证明两态确实走了不同表达式
	{
		box.SetWordWrap(false);
		box.SetSize(38, 10);
		const PaintProbe painted = PaintOnce(box, measurer);
		EXPECT_EQ(painted.texts.size(), 1u);         // 非折行 ⇒ 整串单条命令
		EXPECT_TRUE(painted.clipWidths.size() >= 2u);
		if (painted.clipWidths.size() >= 2)
			EXPECT_NEAR(painted.clipWidths[1], 24.0f, kEps);   // GetTextAreaWidth()（条可见 ⇒ 让位 14）
	}
}


// ══════════════════════════════════════════════════════════════════════
// Phase 38 · 批 4：收口（T38-11 / T38-14 / T38-16）
// ══════════════════════════════════════════════════════════════════════

/// T38-11：Undo/Redo 后行表一致 + ★ 钉 0 的**切换相**（D38-G 写点 3 的判别面）
/// @details 切换相是 v1.0 两写点下的必红项：`SetWordWrap` 只置失效 + `Invalidate()`，
///   **不调** `EnsureCaretVisible`（全仓 25 个调用点无一在 setter 之后）⇒ 旧 `m_scrollOffsetX`
///   若不在这里归零，首帧 `viewX = textX − 旧 offset`，整段折行文本左移且左侧被 clip 吃掉。
///   ★ 本相**必须真窗口**——横向偏移只在 `EnsureCaretVisible` 的 `else if (Window*)` 支写入
///   （无头恒 0，连前置条件都立不起来）。这是本 Phase 第二条真窗口用例（详设 §9 v1.5 ②）。
void Test38SoftWrapUndoRedoAndPinX(){
	// ── ① Undo / Redo 后行表与「直接构造同文本」逐项相等（无头）──
	{
		TestableTextBox box(std::string(""));
		box.SetSize(26 + kWrapBarSlot, 64);
		box.SetWordWrap(true);
		box.InsertText(std::string("abcdef"));   // 6 码点 @26 ⇒ 3+3 ⇒ 2 行
		box.InsertText(std::string("ghij"));     // 10 码点 ⇒ 3+3+3+1 ⇒ 4 行
		EXPECT_EQ(box.RowCountForTest(), 4u);

		box.Undo();                              // 撤销第二次插入 ⇒ 回到 "abcdef"
		EXPECT_EQ(box.RowCountForTest(), 2u);

		TestableTextBox ref(std::string("abcdef"));   // 同配置的干净实例（无缓存 ⇒ 必是正解）
		ref.SetSize(26 + kWrapBarSlot, 64);
		ref.SetWordWrap(true);
		for (std::size_t i = 0; i < 2; ++i){
			EXPECT_EQ(box.OwnForTest(i).first,   ref.OwnForTest(i).first);
			EXPECT_EQ(box.OwnForTest(i).second,  ref.OwnForTest(i).second);
			EXPECT_EQ(box.DrawForTest(i).first,  ref.DrawForTest(i).first);
			EXPECT_EQ(box.DrawForTest(i).second, ref.DrawForTest(i).second);
		}

		box.Redo();                              // 重做 ⇒ 回到 10 码点
		EXPECT_EQ(box.RowCountForTest(), 4u);
	}

	// ── ② 切换相三段（真窗口）──
	{
		CaretProbe probe(std::string("abcdefghijklmnopqrst"), 40, 64);   // 20 × 8 = 160 > 40
		EXPECT_NEAR(probe.box->GetScrollOffsetX(), 0.0f, kEps);

		// ① 前置：非折行态把光标推到行尾 ⇒ 横向偏移真的 > 0
		probe.box->MoveCaretToEnd();
		EXPECT_TRUE(probe.box->GetScrollOffsetX() > 0.0f);

		// ② 只改判据（不经光标移动、不经 EnsureCaretVisible）⇒ 折行重建把偏移钉 0
		probe.box->SetWordWrap(true);
		probe.box->RecalculateLines();            // 写点 3 所在的重建分支
		EXPECT_NEAR(probe.box->GetScrollOffsetX(), 0.0f, kEps);

		// ③ 同一帧内绘制：viewX = textX − 0（若残留旧偏移，这里会是负的大数）
		const PaintProbe painted = PaintOnce(*probe.box, probe.window->GetTextMeasurer());
		EXPECT_TRUE(painted.texts.size() >= 2u);   // 20 码点 @26 ⇒ 多行（前置成立）
		if (!painted.texts.empty())
			EXPECT_NEAR(painted.texts[0].pos.x, probe.box->Padding(), kEps);
		EXPECT_NEAR(probe.box->GetScrollOffsetX(), 0.0f, kEps);   // 绘制后仍恒 0（不变式 U）
	}
}

/// T38-14：10 维自比键**逐维**判别（每维「只变这一维 ⇒ 必须重算」）
/// @details 判据 = `FitText` 被再次调用（= 键未命中 ⇒ 行表重建）。★ **不用「行数变了」当判据**：
///   假测量器忽略字体 ⇒ b/c/d 三维改字体不会改布局，旧表与新表**逐位相同**——那时「行数变了」
///   是假绿（残表看不出来），而「量了没有」才是真判据。而 e/f/g 三维会改布局，另配表形断言。
///   ★ f/g 两维（`SetWordWrap(false)` / `SetSingleLine(true)`）走非折行支，**不调 `FitText`** ⇒
///   判据换成「表形变成单行」（旧表是折行表 ⇒ 不重建就露馅）。
void Test38SoftWrapKeyPerDimension(){
	RecordingTextBox box(std::string("abcdefgh"));
	box.SetSize(26 + kWrapBarSlot, 64);
	box.SetWordWrap(true);
	RecordingFakeMeasurer& rec = RecordingTextBox::Recorder();
	box.RecalculateLines();
	EXPECT_EQ(box.RowCountForTest(), 3u);        // 8 码点 @26 ⇒ 3+3+2

	// 负对照：什么都不改 ⇒ 零测量（与 T38-15 同源；下面每一维都必须打破它）
	rec.Reset();
	box.RecalculateLines();
	EXPECT_EQ(rec.fitCalls, 0);

	// 字体基线显式化：三维只能从**已知值**出发才谈得上「只变一维」
	Font base;
	base.size   = 14.0f;
	base.family = std::string();
	base.weight = 400;
	box.SetFont(base);
	box.RecalculateLines();

	// a) 维 1 文本版本（编辑 ⇒ revision++）
	rec.Reset();
	box.InsertCodepoint(U'z');                   // 9 码点
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);

	// b) 维 2 font.size
	Font f = base;
	f.size = 16.0f;
	rec.Reset();
	box.SetFont(f);
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);

	// c) 维 3 font.family
	f = base;
	f.family = std::string("Consolas");
	rec.Reset();
	box.SetFont(f);
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);

	// d) 维 4 font.weight（Phase 36 K8-④ 同族账：漏加过一维）
	f = base;
	f.weight = 700;
	rec.Reset();
	box.SetFont(f);
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);

	// e) 维 6 折行宽（改控件宽 ⇒ 条槽恒定 14 ⇒ 折行宽 26 → 40）
	rec.Reset();
	box.SetSize(40 + kWrapBarSlot, 64);
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	EXPECT_EQ(box.RowCountForTest(), 2u);        // 9 码点 @40 ⇒ 5+4

	// f) 维 8 的 SetWordWrap 分量（折行 → 非折行：不调 FitText，改断表形）
	rec.Reset();
	box.SetWordWrap(false);
	box.RecalculateLines();
	EXPECT_EQ(rec.fitCalls, 0);                  // 非折行支只扫 `\n`
	EXPECT_EQ(box.OwnForTest(0).second, 9u);     // 单行表覆盖全串（旧表 2 行 ⇒ 这里是 5）

	// g) 维 8 的 SetSingleLine 分量（★ 该维已删、断言保留：判别的是「维 8 覆盖了它」）
	box.SetWordWrap(true);
	box.RecalculateLines();
	EXPECT_EQ(box.RowCountForTest(), 2u);
	rec.Reset();
	box.SetSingleLine(true);                     // WrapRequested() == false
	box.RecalculateLines();
	EXPECT_EQ(rec.fitCalls, 0);
	EXPECT_EQ(box.OwnForTest(0).second, 9u);     // 又是单行表 ⇒ 重建确实发生了
	box.SetSingleLine(false);
	box.RecalculateLines();
	EXPECT_EQ(box.RowCountForTest(), 2u);        // 回到折行表

	// h) 维 9 回显模式（★ 判据在**串内容层**：喂进去的必须是掩码串，一个明文字节都不许有）
	rec.Reset();
	box.SetEchoMode(TextBox::EchoMode::Password);
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	const std::string fedMask = rec.Joined();
	EXPECT_TRUE(fedMask.find("\xE2\x80\xA2") != std::string::npos);   // U+2022 •
	for (char plain : std::string("abcdefghz"))
		EXPECT_TRUE(fedMask.find(plain) == std::string::npos);

	// i) 维 10 组合三维（★ 必须 Password：Normal 态显示串直通 m_text ⇒ 组合标志不入串 ⇒ 无判别力）
	//    i-a 组合开（isComposing 0→1，且组合区显示**原文**）
	rec.Reset();
	box.UpdateComposition(std::string("一二"));
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	EXPECT_TRUE(rec.Joined().find("一二") != std::string::npos);
	//    i-c 组合长度（2 → 3）
	rec.Reset();
	box.UpdateComposition(std::string("一二三"));
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	EXPECT_TRUE(rec.Joined().find("一二三") != std::string::npos);
	//    i-a 组合关（1→0 ⇒ 原文重新被打点）
	rec.Reset();
	box.CancelComposition();
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	EXPECT_TRUE(rec.Joined().find("一") == std::string::npos);
	//    i-b 组合起点：★ **无法只变这一维**——`m_compositionStart` 只在 `!m_isComposing` 时写入
	//    （`UpdateComposition` 首次分支）⇒ 换起点必须「先取消再重新组合」，中间 `isComposing` 必然翻转。
	//    因此本维只能断言「换起点那一轮必重建、且新位置的原文确实进了布局串」，隔离性由键的
	//    单条 `&&` 链结构保证（三项写在同一个 return 表达式里），不能由行为隔离证明 —— 已登记在详设 §9 v1.5 ③。
	rec.Reset();
	box.MoveCaretToStart();
	box.MoveCaret(TextBox::CaretDirection::Right);
	box.MoveCaret(TextBox::CaretDirection::Right);
	box.UpdateComposition(std::string("四"));
	box.RecalculateLines();
	EXPECT_TRUE(rec.fitCalls > 0);
	EXPECT_TRUE(rec.Joined().find("四") != std::string::npos);
}

/// T38-16：真窗口白名单四件合一（唯一需要真窗口的一组——`Invalidate()` 无头是 no-op，A-10）
/// @details 四件各自有一条**无头不可替代**的理由：
///   ① 焦点态绘制路径（`m_showCaret` 为真才走 caret 分支）· ② `CaretIndexFromLineX` 的
///   `GetWindow() != nullptr` 守卫（无头恒返回行首）· ③ 无测量器 → 挂窗后有测量器（键维 7 翻转）
///   ④ `Invalidate()` 的真实语义（无头 no-op ⇒ 只能在窗口里验）。
void Test38SoftWrapRealWindowWhitelist(){
	// ① 焦点态绘制：折行态光标落在**末行**（y 与末行 DrawText 同源，不是行 0）
	{
		CaretProbe probe(std::string("abcdef"), 40, 64);   // 折行宽 26 ⇒ 2 行（3+3）
		probe.box->SetWordWrap(true);
		probe.box->MoveCaretToEnd();
		probe.box->RecalculateLines();
		const PaintProbe painted = PaintOnce(*probe.box, probe.window->GetTextMeasurer());
		EXPECT_EQ(painted.texts.size(), 2u);
		EXPECT_EQ(probe.PaintCaretColors().size(), static_cast<std::size_t>(1));   // 聚焦 ⇒ 光标被绘制
		if (painted.texts.size() == 2){
			const float caretY = probe.box->GetCaretClientGeometry().rect.y;
			EXPECT_NEAR(caretY, painted.texts[1].pos.y, kEps);   // 与末行同源
			EXPECT_TRUE(caretY > painted.texts[0].pos.y);
		}
	}

	// ② 行内 x 精确定位（真窗口才走测量支；无头恒返回行首 ⇒ 本件区分得出两种实现）
	{
		CaretProbe probe(std::string("abcdef"), 40, 64);
		probe.box->SetWordWrap(true);
		probe.box->RecalculateLines();
		const auto line0 = probe.box->OwnForTest(0);
		EXPECT_EQ(line0.first, 0u);
		EXPECT_EQ(line0.second, 3u);   // L0 = "abc"
		const float lineWidth = probe.window->GetTextMeasurer().MeasureText(
			probe.box->FontForTest(), std::string("abc")).width;
		EXPECT_TRUE(lineWidth > 0.0f);
		const std::size_t mid = probe.box->CaretIndexFromLineX(0, lineWidth / 2.0f);
		EXPECT_TRUE(mid > 0u);         // 无头 ⇒ 恒 0
		EXPECT_TRUE(mid < 3u);         // 中点必须落在行内，不许越到行尾
		const std::size_t tail = probe.box->CaretIndexFromLineX(0, lineWidth * 4.0f);
		EXPECT_EQ(tail, 3u);           // 行尾外 ⇒ 本行末（不属于归属区间的空白不越界）
	}

	// ③ 无头求 preferred → 挂窗 ⇒ 重排（键维 7：hasMeasurer 翻转）
	{
		Application app;
		RenderServices services{
			std::make_unique<GDIBackend>(),
			std::make_unique<GDITextMeasurer>()
		};
		Window& window = app.Create("ECDI_T38_Attach", 200, 120, std::move(services));

		auto owned = std::make_unique<TextBox>(std::string("abcdefghijklmnopqrst"));
		TextBox* raw = owned.get();
		raw->SetWordWrap(true);
		raw->SetSize(40, 64);

		// 未挂窗 ⇒ `ResolveMeasurer()` == nullptr（基类：无窗口返回 nullptr）⇒ 折行不生效 ⇒ 单条命令
		FakeTextMeasurer fake;
		EXPECT_EQ(PaintOnce(*raw, fake).texts.size(), static_cast<std::size_t>(1));
		// ★ 无测量能力时 preferred = **当前尺寸**（不是编出来的内容高）：
		//   `TextBox::GetPreferredSize` 走单行支 → `TextWidget::GetPreferredSize` 的
		//   「nullptr ⇒ Widget::GetPreferredSize()」兜底 ⇒ {GetWidth(), GetHeight()}（`p = 0`）
		EXPECT_EQ(raw->GetPreferredSize().height, 64.0f);
		EXPECT_EQ(raw->GetPreferredSize().width, 40.0f);

		window.GetRootWidget().AddChild(std::move(owned));   // ★ 此刻起有窗口测量器

		const PaintProbe attached = PaintOnce(*raw, window.GetTextMeasurer());
		EXPECT_TRUE(attached.texts.size() >= 2u);            // 重排发生了（维 7 翻转 ⇒ 键未命中）
		EXPECT_TRUE(raw->GetPreferredSize().height > 64.0f);  // 折行后内容高真的长出来了

		window.Release();
	}

	// ④ `SetSingleLine` 切换后**无需外部失效**即重排（C38-9；无头 `Invalidate()` 是 no-op）
	//    ★ 可观察的是「下一次绘制用的是新判据」；「是否请求了重绘」本身没有公开观察面
	//      （`Window` 不暴露待绘制标志）⇒ 那一半由 C38-9 的机检（3 个 setter 体内均有 Invalidate）兜住。
	{
		CaretProbe probe(std::string("abcdef"), 40, 64);
		probe.box->SetWordWrap(true);
		EXPECT_EQ(PaintOnce(*probe.box, probe.window->GetTextMeasurer()).texts.size(), 2u);
		probe.box->SetSingleLine(true);      // 折行请求 ⇒ false ⇒ 单行表
		EXPECT_EQ(PaintOnce(*probe.box, probe.window->GetTextMeasurer()).texts.size(), 1u);
		probe.box->SetSingleLine(false);     // 回来 ⇒ 又是两行
		EXPECT_EQ(PaintOnce(*probe.box, probe.window->GetTextMeasurer()).texts.size(), 2u);
	}
}

// ════════════════════════════════════════════════════════════════════════════
// ★ Phase 39 批 0：T39-1..7——**全部断言改前行为**，改后（批 1）必须仍绿。
//   这是本 Phase 判定「逐位不变」的全部依据；批 0 在 HEAD 上先绿 = 采 golden。
// ════════════════════════════════════════════════════════════════════════════

/// @brief T39-1（A5）：默认 `FitText` 的等价性基线——表 A 全 8 组
static void Test39Utf8ScanGoldenFit(){
	FakeTextMeasurer m;
	Font f{};
	for (const FitGold& g : kFitGolden){
		const TextFit fit = m.FitText(f, std::string(g.text), g.startCp, g.maxWidth);
		EXPECT_EQ(fit.fitCp, g.fitCp);
		EXPECT_NEAR(fit.width, g.width, kEps);
	}
}

/// @brief T39-2（A1/A6）：非折行支行表逐位——表 B 全 13 行
static void Test39Utf8ScanSamples(){
	TestableTextBox box;
	for (const LineGold& g : kLineGolden)
		ExpectLines(box, g);
}

/// @brief T39-3：overlong `U+000A` 专项（表 B #8–#10）+ `C0 4A`（overlong × 非法续字节）
/// @details ★ 用「只看首字节」的实现（`lead == 0x0A`）过不了这一条——R39-3 的负对照靶点。
static void Test39Utf8ScanOverlongLf(){
	TestableTextBox box;
	for (size_t i = 7; i <= 9; ++i)
		ExpectLines(box, kLineGolden[i]);

	// `C0 4A`：不属 A6 十一类（overlong 前导 × **非法续字节**），故 inline 构造。
	// 改前行为：`substr(0, 2)` 解码 ((0xC0 & 0x1F) << 6) | (0x4A & 0x3F) = U+000A ⇒ **断行**。
	const LineGold c04a = { "\xC0\x4A", 2, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } } };
	ExpectLines(box, c04a);
}

/// @brief T39-4：`\n` 边界族 + 尾截断（`byte` 越过 `textSize` 不产生额外行）
static void Test39Utf8ScanBoundaries(){
	TestableTextBox box;
	for (size_t i = 0; i < 7; ++i)          // 空串 / \n / \n\n / ab\n / a\r\nb / 非法首字节 / 截断续字节
		ExpectLines(box, kLineGolden[i]);

	// "a\xC0"：末次迭代 byte 1 → 3 越过 textSize（= 2），不产生额外行
	box.SetWordWrap(false);
	box.SetSize(120, 200);
	box.SetText("a\xC0");
	EXPECT_EQ(box.LineCountForTest(), static_cast<size_t>(1));
	EXPECT_EQ(box.OwnForTest(0).second, CountViaCaret(box));

	// "\n\n"：尾部空行也在表内，末行 ownedEnd == 码点总数
	box.SetText("\n\n");
	const size_t n = box.LineCountForTest();
	EXPECT_EQ(n, static_cast<size_t>(3));
	EXPECT_EQ(box.OwnForTest(n - 1).second, CountViaCaret(box));
}

/// @brief T39-5：跨支对照（overlong 的 `\n` 在折行/非折行两支都断行）+ Down 键守卫两侧
/// @details ★ 折行宽也是 **120**（不是 106）：无头夹具 `m_vBar == nullptr`
///          ⇒ `GetTextAreaWidth` 的条槽让位分支（`TextBox.cpp:772`）不进；
///          且默认 `padding == 0`（`DefaultTheme.cpp:43`）。
///          ⇒ 本用例**没有**覆盖条槽让位路径（那条路要有真窗口 + 条可见）。
static void Test39Utf8ScanCrossBranch(){
	const char* kCross[] = { "\xC0\x8A", "\xE0\x80\x8A", "\xF0\x80\x80\x8A" };
	for (const char* s : kCross){
		TestableTextBox plain;
		plain.SetWordWrap(false);
		plain.SetSize(120, 200);
		plain.SetText(s);
		const size_t nPlain = plain.LineCountForTest();

		TestableTextBox wrap;
		wrap.SetWordWrap(true);
		wrap.SetSize(120, 200);
		wrap.SetText(s);
		const size_t nWrap = wrap.LineCountForTest();

		EXPECT_EQ(nPlain, static_cast<size_t>(2));
		EXPECT_EQ(nWrap,  static_cast<size_t>(2));
		EXPECT_EQ(nPlain, nWrap);
	}

	// ② 2 行箱：Down 越过行边界 ⇒ caret 0 → 1（走 `TextBox.cpp:1349`）
	{
		TestableTextBox box;
		box.SetWordWrap(false);
		box.SetSize(120, 200);
		box.SetText("\xC0\x8A");
		box.MoveCaretToStart();
		box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
		EXPECT_EQ(box.GetCaret(), static_cast<size_t>(1));
	}
	// ③ 单行负例：末行守卫（`TextBox.cpp:1345`）⇒ Down 是 no-op，caret 停在 0
	//    ★ 只断 ② 时「Down 永远返回 1」的错误实现也能过 ⇒ ③ 是这条判据的另一半。
	{
		TestableTextBox box;
		box.SetWordWrap(false);
		box.SetSize(120, 200);
		box.SetText("abc");
		box.MoveCaretToStart();
		box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
		EXPECT_EQ(box.GetCaret(), static_cast<size_t>(0));
	}
}

/// @brief T39-6：两口径一致性（非折行支）
/// @details ① 无软断点可裁 ⇒ `draw.second == own.second`（逐位相等，不是 ≤）；
///          ② 非末行 `own.second + 1 == 下一行 own.first`——**硬行**形态：
///          `\n` 不属任何一行，行末 `ownedEnd` 恰为该 `\n` 的码点索引（Phase 38 A-15 在非折行支的重写）。
static void Test39Utf8ScanTwoRanges(){
	TestableTextBox box;
	for (const LineGold& g : kLineGolden){
		box.SetWordWrap(false);
		box.SetSize(120, 200);
		box.SetText(std::string(g.text));
		const size_t n = box.LineCountForTest();
		EXPECT_EQ(n, g.lineCount);
		if (n != g.lineCount)
			continue;
		for (size_t i = 0; i < n; ++i){
			const auto own  = box.OwnForTest(i);
			const auto draw = box.DrawForTest(i);
			EXPECT_EQ(draw.second, own.second);
			if (i + 1 < n)
				EXPECT_EQ(own.second + 1, box.OwnForTest(i + 1).first);
		}
	}
}

/// @brief T39-7：`count` 一致性——单趟累计的 `cp`（经末行 `ownedEnd` 读出）== 外部口径码点总数
/// @details 直接验证 L2（迭代次数 == `count`）。20k 两条只断该等式，不查逐行（耗时）。
static void Test39Utf8ScanCountAgrees(){
	TestableTextBox box;
	box.SetWordWrap(false);
	box.SetSize(120, 200);
	for (const LineGold& g : kLineGolden){
		box.SetText(std::string(g.text));
		const size_t n = box.LineCountForTest();
		EXPECT_EQ(n, g.lineCount);
		if (n == 0)
			continue;
		EXPECT_EQ(box.OwnForTest(n - 1).second, CountViaCaret(box));
	}

	// 两条 20k 长串（非折行 ⇒ 1 行）：`ownedEnd` 必须恰好等于 20000
	{
		std::string cjk;
		cjk.reserve(20000 * 3);
		for (int i = 0; i < 20000; ++i)
			cjk += "\xE5\xAD\x97";          // U+5B57「字」
		box.SetText(cjk);
		EXPECT_EQ(box.LineCountForTest(), static_cast<size_t>(1));
		EXPECT_EQ(box.OwnForTest(0).second, CountViaCaret(box));
		EXPECT_EQ(box.OwnForTest(0).second, static_cast<size_t>(20000));
	}
	{
		std::string ascii(20000, 'a');
		box.SetText(ascii);
		EXPECT_EQ(box.LineCountForTest(), static_cast<size_t>(1));
		EXPECT_EQ(box.OwnForTest(0).second, CountViaCaret(box));
		EXPECT_EQ(box.OwnForTest(0).second, static_cast<size_t>(20000));
	}
}

void ECDI::Test::RegisterTextBoxTests()
{
    GetTestRegistry().Add("TextBox.CaretColorStyle", &Test30CaretColorStyle);   // T30-1（Phase 30 批一）
    GetTestRegistry().Add("TextBox.InsertDelete", &TestTextBoxInsertDelete);
    GetTestRegistry().Add("TextBox.CaretMovement", &TestTextBoxCaretMovement);
    GetTestRegistry().Add("TextBox.GetSelection", &TestTextBoxGetSelection);
    GetTestRegistry().Add("TextBox.Boundary", &TestTextBoxBoundary);
    GetTestRegistry().Add("TextBox.Callback", &TestTextBoxCallback);
    GetTestRegistry().Add("TextBox.SelectionKeyboard", &TestTextBoxSelectionKeyboard);
    GetTestRegistry().Add("TextBox.SelectionEdit", &TestTextBoxSelectionEdit);
    GetTestRegistry().Add("TextBox.TextSystem2", &TestTextBoxTextSystem2);   // 8.5.1：F1-F15
    GetTestRegistry().Add("TextBox.Multiline", &TestTextBoxMultiline);       // 8.5.2：F16-F30
    GetTestRegistry().Add("TextBox.UndoRedo", &TestTextBoxUndoRedo);         // 8.5.3：F35-F45
    GetTestRegistry().Add("TextBox.EchoMasked", &TestTextBoxEchoMasked);     // P1：掩码绘制
    GetTestRegistry().Add("TextBox.EchoGetTextReal", &TestTextBoxEchoGetTextReal);  // P1：数据层真实值
    GetTestRegistry().Add("TextBox.ReadOnlyEditNoOp", &TestTextBoxReadOnlyEditNoOp);  // P1：只读门禁
    GetTestRegistry().Add("TextBox.ReadOnlyCompositionRefuse", &TestTextBoxReadOnlyCompositionRefuse);  // P1：只读拒 IME
    GetTestRegistry().Add("TextBox.SingleLineEnterNoOp", &TestTextBoxSingleLineEnterNoOp);  // P1：单行 Enter 不换行
    GetTestRegistry().Add("TextBox.MultilinePreferred", &TestTextBoxPreferredMultilineParticipates);  // 9.8/2026-10-21：多行参与（旧「不参与」翻转）
    GetTestRegistry().Add("TextBox.SingleLinePreferred", &TestTextBoxPreferredSingleLine);  // 9.8：padding 修正 + 自然居中验证
    GetTestRegistry().Add("TextBox.ShapeRounded", &TestTextBoxShapeRounded);  // P1：圆角背景
    GetTestRegistry().Add("TextBox.ShapeBorderRing", &TestTextBoxShapeBorderRing);  // P1：双矩形描边环
    // scrollbar-placement-and-appearance v1.1：垂直滚动条（T1–T8）
    GetTestRegistry().Add("TextBox.ScrollBarVisibleOnOverflow", &TestTextBoxScrollBarVisibleOnOverflow);
    GetTestRegistry().Add("TextBox.ScrollBarHiddenWhenFits", &TestTextBoxScrollBarHiddenWhenFits);
    GetTestRegistry().Add("TextBox.ScrollBarHiddenSingleLine", &TestTextBoxScrollBarHiddenSingleLine);
    GetTestRegistry().Add("TextBox.ScrollBarHitAndAuthority", &TestTextBoxScrollBarHitAndAuthority);
    GetTestRegistry().Add("TextBox.ScrollBarFollowsWheel", &TestTextBoxScrollBarFollowsWheel);
    GetTestRegistry().Add("TextBox.ScrollBarNarrowsTextArea", &TestTextBoxScrollBarNarrowsTextArea);
    GetTestRegistry().Add("TextBox.ScrollBarFollowsCaret", &TestTextBoxScrollBarFollowsCaret);
    GetTestRegistry().Add("TextBox.ScrollBarLazySyncOnPaint", &TestTextBoxScrollBarLazySyncOnPaint);
    GetTestRegistry().Add("TextBox.ScrollBarIgnoresPressOnBar", &TestTextBoxScrollBarIgnoresPressOnBar);
    GetTestRegistry().Add("TextBox.ScrollBarInset", &TestTextBoxScrollBarInset);
    GetTestRegistry().Add("TextBox.BaselineTextY", &Test37BaselineTextY);       // T37-1（Phase 37 批一）
    GetTestRegistry().Add("TextBox.ThemeDefaultTop", &Test37ThemeDefaultTop);   // T37-7（Phase 37 批一）
    GetTestRegistry().Add("TextBox.CenterInset", &Test37CenterInset);           // T37-2（Phase 37 批二）
    GetTestRegistry().Add("TextBox.CenterDegenerate", &Test37CenterDegenerate); // T37-3（Phase 37 批二）
    GetTestRegistry().Add("TextBox.CaretAlignsWithPaint", &Test37CaretAlignsWithPaint);   // T37-4（Phase 37 批二）
    GetTestRegistry().Add("TextBox.HitFollowsAlignCenter", &Test37HitFollowsAlignCenter); // T37-5（Phase 37 批二）
    GetTestRegistry().Add("TextBox.HitFollowsAlignTop", &Test37HitFollowsAlignTop);       // T37-6（Phase 37 批二）
    GetTestRegistry().Add("TextBox.SetStyleOverrideChain", &Test37SetStyleOverrideChain); // T37-8（Phase 37 批二）
    GetTestRegistry().Add("TextBox.SoftWrapOffPaintBaseline", &Test38BaselineOffPaint);           // T38-1（Phase 38 批 0）
    GetTestRegistry().Add("TextBox.SoftWrapOffPreferredBaseline", &Test38BaselineOffPreferred);   // T38-13（Phase 38 批 0）
    GetTestRegistry().Add("TextBox.SoftWrapS1FourWaySameSource", &Test38SoftWrapS1FourWaySameSource);       // T38-3（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapS2CjkNoSpace", &Test38SoftWrapS2CjkNoSpace);                     // T38-4（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapS3HardSoftMixed", &Test38SoftWrapS3HardSoftMixed);               // T38-5（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapKinsokuFiveSamples", &Test38SoftWrapKinsokuFiveSamples);         // T38-6（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapCodepointConservation", &Test38SoftWrapCodepointConservation);   // T38-7（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapKeyHitZeroMeasure", &Test38SoftWrapKeyHitZeroMeasure);           // T38-15（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapSeparatorSpaceOwned", &Test38SoftWrapSeparatorSpaceOwned);      // T38-18（批 2）
    GetTestRegistry().Add("TextBox.SoftWrapWidthIndependentOfBar", &Test38SoftWrapWidthIndependentOfBar);   // T38-8（批 3）
    GetTestRegistry().Add("TextBox.SoftWrapSpaceReachable", &Test38SoftWrapSpaceReachable);                 // T38-9（批 3）
    GetTestRegistry().Add("TextBox.SoftWrapSelectionAndImeContinuous", &Test38SoftWrapSelectionAndImeContinuous); // T38-10（批 3）
    GetTestRegistry().Add("TextBox.SoftWrapPreferredStable", &Test38SoftWrapPreferredStable);               // T38-12（批 3）
    GetTestRegistry().Add("TextBox.SoftWrapClipIsInnerBoundary", &Test38SoftWrapClipIsInnerBoundary);       // T38-17（批 3）
    GetTestRegistry().Add("TextBox.SoftWrapUndoRedoAndPinX", &Test38SoftWrapUndoRedoAndPinX);                // T38-11（批 4）
    GetTestRegistry().Add("TextBox.SoftWrapKeyPerDimension", &Test38SoftWrapKeyPerDimension);                // T38-14（批 4）
    GetTestRegistry().Add("TextBox.SoftWrapRealWindowWhitelist", &Test38SoftWrapRealWindowWhitelist);        // T38-16（批 4）
    // Phase 39 批 0：UTF-8 单趟扫描的改前基线（531 → 538）
    GetTestRegistry().Add("TextBox.Utf8ScanGoldenFit", &Test39Utf8ScanGoldenFit);        // T39-1
    GetTestRegistry().Add("TextBox.Utf8ScanSamples", &Test39Utf8ScanSamples);            // T39-2
    GetTestRegistry().Add("TextBox.Utf8ScanOverlongLf", &Test39Utf8ScanOverlongLf);      // T39-3
    GetTestRegistry().Add("TextBox.Utf8ScanBoundaries", &Test39Utf8ScanBoundaries);      // T39-4
    GetTestRegistry().Add("TextBox.Utf8ScanCrossBranch", &Test39Utf8ScanCrossBranch);    // T39-5
    GetTestRegistry().Add("TextBox.Utf8ScanTwoRanges", &Test39Utf8ScanTwoRanges);        // T39-6
    GetTestRegistry().Add("TextBox.Utf8ScanCountAgrees", &Test39Utf8ScanCountAgrees);    // T39-7
}
