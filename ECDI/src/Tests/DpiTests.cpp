#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10：本文件含 Windows.h——防 DrawTextW 宏污染）
#endif

#include "Platform/Win32/DpiConversion.h"

using namespace ECDI;

// ── Phase 20：DPI 换算（纯函数层——T20-1..T20-6）────────────────────────────────
// ★ 本组**不经窗口、不经翻译器**：三个换算函数住在**中立内部头** `DpiConversion.h`
//   （`namespace ECDI` 的 `inline` 自由函数）⇒ **测试直接调用，零测试缝、零窗口、
//   零平台依赖**。
// ★ 这正是详设 §1.3 **修3** 的直接收益：`DipToPixels` 由 `(int, HWND)` 改为 `(int, int)`
//   之后，换算才**能在无头环境逐位验证**（原签名收 HWND ⇒ 纯换算根本测不了）。
// ★ 该头被两个**生产消费者**共用：`Win32PlatformWindow`（窗口创建 / NCHITTEST / IME /
//   GetClientSize）与 `WindowMessageHandler`（鼠标与尺寸事件的坐标翻译）——**唯一真相源**。
// ★ 翻译路径的用例（T20-7..T20-11）在 `EventTests.cpp`（`FakeHost` + `Handle()`）。

namespace{

using DipFn = int (*)(int, int);   // 两个换算函数的共同签名（便于真值表驱动）

/// @brief 真值表一行：输入 / DPI / 期望值
struct TruthCase{ int input; int dpi; int expected; };

/// @brief 真值表驱动的逐点核对（失败逐条上报——EXPECT_EQ 语义为「记录 + 继续」）
void CheckTruthTable(DipFn fn, const TruthCase* cases, int count){

	for (int i = 0; i < count; ++i){

		EXPECT_EQ(fn(cases[i].input, cases[i].dpi), cases[i].expected);

	}

}

// ── T20-1：DipToPixels 真值表（含负数）─────────────────────────────────────────
// 期望值取自数学定义 x = dip × dpi ÷ 96，再按 sign(x)·floor(|x| + 0.5) 舍入（已逐点核算）。

void TestDipToPixelsTruthTable(){

	static const TruthCase kCases[] = {
		// dpi = 96（100%）：恒等
		{   0,  96,   0 }, {   1,  96,   1 }, {  14,  96,  14 }, {  -1,  96,  -1 }, {  -2,  96,  -2 },
		// dpi = 120（125%）
		{   1, 120,   1 }, {   2, 120,   3 }, {   4, 120,   5 }, {  10, 120,  13 },
		{  -1, 120,  -1 }, {  -2, 120,  -3 },
		// dpi = 144（150%）★ 14 DIP ⇒ 21 px ——「Font::size 语义兑现」的数值锚点
		{   1, 144,   2 }, {   2, 144,   3 }, {   3, 144,   5 }, {  14, 144,  21 },
		{  -1, 144,  -2 }, {  -2, 144,  -3 },
		// dpi = 192（200%）
		{   1, 192,   2 }, {   3, 192,   6 }, {  14, 192,  28 },
		{  -1, 192,  -2 }, {  -2, 192,  -4 },
	};

	CheckTruthTable(&ECDI::DipToPixels, kCases,
	                static_cast<int>(sizeof(kCases) / sizeof(kCases[0])));

}

// ── T20-2：dpi == 96 ⇒ 恒等（★ G5 护栏，扫描含负数）──────────────────────────

void TestDipToPixelsIdentityAt96(){

	// ★ G5：dpi == 96 ⇒ px == dip（**逐位恒等，含负数**）。
	// 这是「100% DPI 下本阶段所有换算是恒等变换」的结构性依据 ⇒ 既有 236 用例零回归的根。
	for (int d = -500; d <= 500; ++d){

		EXPECT_EQ(ECDI::DipToPixels(d, 96), d);

	}

}

// ── T20-3：PixelsToDip 真值表（含负数）────────────────────────────────────────

void TestPixelsToDipTruthTable(){

	static const TruthCase kCases[] = {
		// dpi = 96（100%）：恒等
		{   0,  96,   0 }, {   1,  96,   1 }, {  21,  96,  21 }, {  -1,  96,  -1 }, {  -2,  96,  -2 },
		// dpi = 120（125%）
		{   1, 120,   1 }, {   2, 120,   2 }, {   3, 120,   2 }, {   5, 120,   4 },
		{  -1, 120,  -1 }, {  -2, 120,  -2 }, {  -3, 120,  -2 },
		// dpi = 144（150%）★ 21 px ⇒ 14 DIP（与 T20-1 的 14 DIP ⇒ 21 px 互为反向）
		{   1, 144,   1 }, {   2, 144,   1 }, {   3, 144,   2 }, {  21, 144,  14 },
		{  -1, 144,  -1 }, {  -2, 144,  -1 }, {  -3, 144,  -2 },
		// dpi = 192（200%）
		{   1, 192,   1 }, {   2, 192,   1 }, {   3, 192,   2 }, {  28, 192,  14 },
		{  -1, 192,  -1 }, {  -2, 192,  -1 }, {  -3, 192,  -2 },
	};

	CheckTruthTable(&ECDI::PixelsToDip, kCases,
	                static_cast<int>(sizeof(kCases) / sizeof(kCases[0])));

}

// ── T20-4：dpi == 96 ⇒ 恒等（★ G5 护栏，扫描含负数）──────────────────────────

void TestPixelsToDipIdentityAt96(){

	// ★ G5：反向亦恒等——注意这是「dpi == 96 特例」，**不是普遍性质**（见 T20-5 的 C9）。
	for (int px = -500; px <= 500; ++px){

		EXPECT_EQ(ECDI::PixelsToDip(px, 96), px);

	}

}

// ── T20-5：★ C9——往返**不恒等**（契约，不是 bug）────────────────────────────

void TestRoundTripNotIdentity(){

	// ★★ 契约 C9：两个方向**各自保精度，但不得追求往返恒等**。
	// 反例（dpi = 120）：2 px → 2 DIP → **3 px** ≠ 2 px。
	// 根因：dpi > 96 时物理像素网格**比 DIP 网格更密** ⇒ 多对一 ⇒ 反向放大无法还原
	//       （信息已丢失，数学上不可能恒等）。
	// ⚠️ 本用例的存在意义 = **告诉未来维护者「这个不相等是契约」**，防止有人当 bug 去改公式。
	EXPECT_EQ(ECDI::PixelsToDip(2, 120), 2);
	EXPECT_EQ(ECDI::DipToPixels(2, 120), 3);
	EXPECT_NE(ECDI::DipToPixels(ECDI::PixelsToDip(2, 120), 120), 2);

	// ★ 但 dpi == 96 时**往返恒等**必须成立（G5——两个方向都是恒等变换）
	EXPECT_EQ(ECDI::DipToPixels(ECDI::PixelsToDip(37, 96), 96), 37);

}

// ── T20-6：★★ C10——负数**对称**舍入（远离零）────────────────────────────────

void TestNegativeRoundHalfAwayFromZero(){

	// ★★ 契约 C10：负数必须**对称**舍入（`sign(x) · floor(|x| + 0.5)`，远离零）。
	// 历史反例（v1.0 的「加偏移再除」写法，已修）：C++ 整数除法**向 0 截断** ⇒
	//   `(num + den/2) / den` 对负数得到的是"向零"而非"远离零"——
	//   实测 dpi = 144 的 -20..0 区间两个函数各 20/20 **全部**偏差（系统性错误）。

	// ① 详设评审给出的两个算例（硬编码期望值）
	EXPECT_EQ(ECDI::PixelsToDip(-1, 144), -1);
	EXPECT_EQ(ECDI::DipToPixels(-1, 144), -2);

	// ② ★★ 对称性（C10 的**本质**）：f(-x) == -f(x)。
	//    这条断言是「契约的**独立表述**」——它不依赖任何参考实现，只依赖
	//    「正负无偏」这一定义本身 ⇒ 即使将来换掉公式，只要仍满足 C10 就依然成立。
	static const int kDpis[] = { 96, 120, 144, 192, 168, 105 };   // 含非常规 168/105
	for (int dpi : kDpis){

		for (int x = 1; x <= 200; ++x){

			EXPECT_EQ(ECDI::DipToPixels(-x, dpi),
			          -ECDI::DipToPixels(x, dpi));

			EXPECT_EQ(ECDI::PixelsToDip(-x, dpi),
			          -ECDI::PixelsToDip(x, dpi));

		}

	}

	// ③ dpi == 96 时负值仍恒等（G5 与 C10 的交集）
	EXPECT_EQ(ECDI::DipToPixels(-14, 96), -14);
	EXPECT_EQ(ECDI::PixelsToDip(-21, 96), -21);

}

}   // namespace

void ECDI::Test::RegisterDpiTests(){

	GetTestRegistry().Add("Dpi.DipToPixelsTruthTable",            &TestDipToPixelsTruthTable);       // T20-1
	GetTestRegistry().Add("Dpi.DipToPixelsIdentityAt96",          &TestDipToPixelsIdentityAt96);     // T20-2
	GetTestRegistry().Add("Dpi.PixelsToDipTruthTable",            &TestPixelsToDipTruthTable);       // T20-3
	GetTestRegistry().Add("Dpi.PixelsToDipIdentityAt96",          &TestPixelsToDipIdentityAt96);     // T20-4
	GetTestRegistry().Add("Dpi.RoundTripNotIdentity",             &TestRoundTripNotIdentity);        // T20-5
	GetTestRegistry().Add("Dpi.NegativeRoundHalfAwayFromZero",    &TestNegativeRoundHalfAwayFromZero);   // T20-6

}
