#include "RunAllTests.h"
#include "TestFramework.h"

#include "Render/BackendContract.h"
#include "ECDI/Core/Image.h"

#include <cstddef>
#include <cstdint>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// 审计 #62 / §7.2：后端契约（GDI 与 GL 共用的规则与守卫）
// ══════════════════════════════════════════════════════════════════════
// ★ 背景（`framework-implementation-audit.md` §7.2）：GL 与 GDI 是两套独立实现，各写
//   各的守卫与降级 ⇒ 审计逐条核对出 8 条、共性模式是「**GDI 有、GL 没有**」。
//   本组把「**判据本身可平台无关化**」的部分抽出来，直接钉住共享判据。
// ★★ **本组为什么能覆盖 GL 侧**：抽取的判据全是**纯函数**（无 WGL、无 HDC）⇒
//   **不需要创建真实 GL context 也能测**——这正面解决了审计 §7.2 里
//   「同一命令序列两端产生等价结果」写不出来（全库无测试创建 WGL context）的困局：
//   把**判据**测住，「两端调用同一判据」自然等价。
//   ⚠️ 反过来：真正的**像素级等价**仍需人工目视（Phase 26 T26-9 同款），本组不假装覆盖。

namespace {

/// @brief 造一个几何自洽的基准图像（w×h、标准 stride）——各用例按需改单字段
Image MakeWellFormed(int w, int h)
{
	Image img;
	img.width = w;
	img.height = h;
	img.stride = w * 4;
	img.pixels.assign(static_cast<std::size_t>(img.stride) * static_cast<std::size_t>(h), 0xFF);
	return img;
}

}   // namespace

// ══════════════════════════════════════════
// H3：图像可用性守卫（GL 越界读 / GDI 越界读 的共同判据）
// ══════════════════════════════════════════

// ── H3-①：自洽图像可绘制（正向基线）───────────────────────────────
void Test62ImageWellFormedDrawable()
{
	const Image img = MakeWellFormed(4, 3);
	EXPECT_TRUE(BackendContract::ImageIsDrawable(img));

	// 边界：1×1（最小合法图像）
	EXPECT_TRUE(BackendContract::ImageIsDrawable(MakeWellFormed(1, 1)));
}

// ── H3-②：零尺寸 ⇒ 不可绘制（空图像不产生绘制——Core/Image.h:12）──
void Test62ImageZeroSizeNotDrawable()
{
	Image zeroW = MakeWellFormed(4, 3); zeroW.width = 0;
	EXPECT_FALSE(BackendContract::ImageIsDrawable(zeroW));

	Image zeroH = MakeWellFormed(4, 3); zeroH.height = 0;
	EXPECT_FALSE(BackendContract::ImageIsDrawable(zeroH));

	// 负尺寸（`pixels` 空的前提下同样不可绘制——负几何无意义）
	Image negW = MakeWellFormed(4, 3); negW.width = -1;
	EXPECT_FALSE(BackendContract::ImageIsDrawable(negW));

	Image negH = MakeWellFormed(4, 3); negH.height = -2;
	EXPECT_FALSE(BackendContract::ImageIsDrawable(negH));
}

// ── H3-③：stride 不足 ⇒ 不可绘制（★ 越界读的直接成因）───────────────
//   `stride < width * 4` ⇒ 逐行读取 `row = pixels.data() + j*stride` 后取
//   `row[i*4 .. i*4+3]`，当 i 接近 width 时**越过缓冲尾**。
void Test62ImageSmallStrideNotDrawable()
{
	Image img = MakeWellFormed(4, 3);
	img.stride = 15;   // < 4*4 = 16
	EXPECT_FALSE(BackendContract::ImageIsDrawable(img));

	// 恰好等于 width*4 ⇒ 合法（下界是「大于等于」）
	img.stride = 16;
	EXPECT_TRUE(BackendContract::ImageIsDrawable(img));
}

// ── H3-④：缓冲不足 ⇒ 不可绘制（`pixels.size() < stride*height`）───────
void Test62ImageSmallBufferNotDrawable()
{
	Image img = MakeWellFormed(4, 3);   // 需要 16*3 = 48 字节
	img.pixels.resize(47);              // 少一字节即越界
	EXPECT_FALSE(BackendContract::ImageIsDrawable(img));

	img.pixels.resize(48);              // 恰好 ⇒ 合法
	EXPECT_TRUE(BackendContract::ImageIsDrawable(img));

	// 空缓冲
	img.pixels.clear();
	EXPECT_FALSE(BackendContract::ImageIsDrawable(img));
}

// ── H3-⑤：**合法 padding** ⇒ 仍可绘制（stride 可大于 width*4——对齐行）──
//   这是 H3-④ 的必要反面：`stride > width*4` 是正常用法（行对齐），
//   守卫**不得**把 padding 判成非法。
void Test62ImagePaddedStrideDrawable()
{
	Image img = MakeWellFormed(4, 3);
	img.stride = img.width * 4 + 12;   // 每行多 12 字节 padding
	img.pixels.resize(static_cast<std::size_t>(img.stride) * 3);   // 按 padding 后尺寸给缓冲
	EXPECT_TRUE(BackendContract::ImageIsDrawable(img));
}

// ── H3-⑥：三者**同时**不成立（多重缺陷）⇒ 仍安全（不可绘制）────────────
void Test62ImageMultipleDefectsNotDrawable()
{
	Image img;
	img.width = 0;
	img.height = 0;
	img.stride = 0;
	// 全部零 + 空缓冲（默认构造等价）
	EXPECT_FALSE(BackendContract::ImageIsDrawable(img));
}

// ══════════════════════════════════════════
// M17：边量化口径（GL 与 GDI 共用的边规则）
// ══════════════════════════════════════════

// ── M17-①：正数边 ⇒ 与旧 GL `floor` 逐位一致（零回归）────────────────
//   ★ 这条是**回归锚**：本批把 GL 的 `Snap` 从自写 `std::floor` 改为调用共享件，
//     正数域行为必须一字不变。
void Test62QuantizeEdgePositiveMatchesFloor()
{
	EXPECT_EQ(BackendContract::QuantizeEdge(0.0f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(0.4f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(0.5f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(0.9f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(1.0f), 1);
	EXPECT_EQ(BackendContract::QuantizeEdge(1.5f), 1);
	EXPECT_EQ(BackendContract::QuantizeEdge(19.99f), 19);
	EXPECT_EQ(BackendContract::QuantizeEdge(200.0f), 200);
}

// ── M17-②：负数边 ⇒ `floor`（**非**向零截断——审计点名的 M17 病灶）───────
//   ★ 负坐标是真实场景：水平滚动（`drawX < 0`）、子控件越界绘制。
//   `static_cast<int>(-0.5f) == 0`（向零截断）把 -0.5 归到像素 0，
//   但它 5/6 的覆盖在像素 -1 ⇒ **符号相关的不一致**。
void Test62QuantizeEdgeNegativeUsesFloor()
{
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.1f), -1);
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.5f), -1);
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.9f), -1);
	EXPECT_EQ(BackendContract::QuantizeEdge(-1.0f), -1);   // 整边界不偏
	EXPECT_EQ(BackendContract::QuantizeEdge(-1.5f), -2);
	EXPECT_EQ(BackendContract::QuantizeEdge(-100.25f), -101);
}

// ── M17-③：`SnapVertex` = `QuantizeEdge` 的 float 形态（数值同一）──────────
//   ★ GL 的 `Snap` 直接薄封装它 ⇒ 除「整数→float 的表示」外无第二处逻辑。
void Test62SnapVertexMirrorsQuantizeEdge()
{
	EXPECT_EQ(BackendContract::SnapVertex(0.5f), 0.0f);
	EXPECT_EQ(BackendContract::SnapVertex(3.75f), 3.0f);
	EXPECT_EQ(BackendContract::SnapVertex(-3.25f), -4.0f);
	EXPECT_EQ(BackendContract::SnapVertex(17.0f), 17.0f);
}

// ── M17-④：**区间一致性**（本判据的核心不变量）────────────────────────
//   依据：矩形 `[x, x+w)` 覆盖的像素列 = `[floor(x), floor(x+w))`。
//   宽度**由两条量化边相减导出**，绝不对 `w` 独立取整——否则
//   `lround(x) + lround(w) != floor(x+w)`（同一条右边出现两个值）。
void Test62QuantizeEdgeIntervalConsistent()
{
	// 全为整数的矩形：三条口径（truncate / floor / lround）必须一致
	const float x = 10.0f, w = 37.0f;
	const int left = BackendContract::QuantizeEdge(x);
	const int right = BackendContract::QuantizeEdge(x + w);
	EXPECT_EQ(left, 10);
	EXPECT_EQ(right, 47);
	EXPECT_EQ(right - left, 37);   // ★ 宽 = 右边 − 左边（不是独立取整）

	// 分数矩形：宽度由边界导出，保证 `left + width == right` 自洽
	const float fx = -12.7f, fw = 55.3f;
	const int fl = BackendContract::QuantizeEdge(fx);
	const int fr = BackendContract::QuantizeEdge(fx + fw);
	EXPECT_EQ(fl, -13);
	EXPECT_EQ(fr, 42);
	EXPECT_EQ(fr - fl, 55);
}

// ── M17-⑤：**自相矛盾的宽度**不会出现（对照 H3 的 positive control）────────
//   这条把「独立取整宽度」的错法与规范口径放在一起做差，
//   证明规范口径在小数宽下不会偏。
//   （若有人改成对 w 独立 lround，此用例会红。）
void Test62QuantizeEdgeWidthNeverIndependentlyRounded()
{
	// 0.5 宽：lround(0.5f) = 1（向远离零取整）⇒ 会凭空多出 0 像素宽；
	// 规范口径：右 − 左 = floor(10.5) − floor(10.0) = 10 − 10 = 0（空矩形，正确）
	const int l = BackendContract::QuantizeEdge(10.0f);
	const int r = BackendContract::QuantizeEdge(10.5f);
	EXPECT_EQ(r - l, 0);   // ★ 0.5 宽矩形不产生像素

	// 1.5 宽：floor(10+1.5) − floor(10) = 11 − 10 = 1（覆盖 1 列像素，正确）
	const int l2 = BackendContract::QuantizeEdge(10.0f);
	const int r2 = BackendContract::QuantizeEdge(11.5f);
	EXPECT_EQ(r2 - l2, 1);
}

void ECDI::Test::RegisterBackendContractTests()
{
	// H3：图像可用性守卫
	GetTestRegistry().Add("BackendContract.ImageWellFormedDrawable",     &Test62ImageWellFormedDrawable);
	GetTestRegistry().Add("BackendContract.ImageZeroSizeNotDrawable",    &Test62ImageZeroSizeNotDrawable);
	GetTestRegistry().Add("BackendContract.ImageSmallStrideNotDrawable", &Test62ImageSmallStrideNotDrawable);
	GetTestRegistry().Add("BackendContract.ImageSmallBufferNotDrawable", &Test62ImageSmallBufferNotDrawable);
	GetTestRegistry().Add("BackendContract.ImagePaddedStrideDrawable",   &Test62ImagePaddedStrideDrawable);
	GetTestRegistry().Add("BackendContract.ImageMultipleDefectsNotDrawable", &Test62ImageMultipleDefectsNotDrawable);

	// M17：边量化口径
	GetTestRegistry().Add("BackendContract.QuantizeEdgePositiveMatchesFloor",    &Test62QuantizeEdgePositiveMatchesFloor);
	GetTestRegistry().Add("BackendContract.QuantizeEdgeNegativeUsesFloor",       &Test62QuantizeEdgeNegativeUsesFloor);
	GetTestRegistry().Add("BackendContract.SnapVertexMirrorsQuantizeEdge",       &Test62SnapVertexMirrorsQuantizeEdge);
	GetTestRegistry().Add("BackendContract.QuantizeEdgeIntervalConsistent",      &Test62QuantizeEdgeIntervalConsistent);
	GetTestRegistry().Add("BackendContract.QuantizeEdgeWidthNeverIndepRounded",  &Test62QuantizeEdgeWidthNeverIndependentlyRounded);
}
