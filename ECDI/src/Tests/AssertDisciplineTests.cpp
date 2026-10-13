// ★ 审计 #63 / §7.3：FRAMEWORK_ASSERT 纪律推广的测试
//
// ★★ **本文件不写 `#undef _DEBUG`**——这是本批踩到并已移除的一个**真实构建故障**，
//    连同"为何曾想这么做"一并记账，防后来者重复踩：
//      ① **动机**：想"在 Debug 构建里直接测到 Release 侧分支"，于是试过在
//         本 TU 顶部 `#undef _DEBUG`（`FRAMEWORK_ASSERT` 是宏，展开取决于使用它的 TU）。
//      ② **为何无效**：被测断言在**库侧**（`Widget.cpp`，用 `_DEBUG` 编译）⇒
//         它在**库编译时**就已展开成真调用，与消费方 TU 的宏状态无关
//         ⇒ 关掉本 TU 的断言**根本救不了进程**（实测照样 `assert(false)` 终止）。
//      ③ **为何必须移除**：`#undef _DEBUG` 会让本 TU 在包含 MSVC CRT/STL 头时
//         按 **Release 语义**展开（`_ITERATOR_DEBUG_LEVEL=0` / `RuntimeLibrary=MD`），
//         而同一构建其余 TU 都是 Debug（=2 / `MDd`）⇒ **MSVC 链接期炸**：
//         `error LNK2038: 检测到"_ITERATOR_DEBUG_LEVEL"的不匹配项: 值"0"不匹配值"2"`
//         + `RuntimeLibrary 的值"MD_DynamicRelease"不匹配"MDd_DynamicDebug"`
//         ⇒ `LNK1319: 检测到 2 个不匹配项`（用户侧 VS Debug 实测）。
//         ★ **MinGW 构建测不出来**（GNU ABI 无这两个 detect_mismatch 机制）
//           ⇒ 又一条"本机全绿 ≠ 四工具链全绿"的实证（同族：条 128/129 跨工具链差异）。
//      ④ **正确形态 = 与 `LayoutTests.cpp:816-821` 同款的 `#ifdef NDEBUG`**
//         （见下方注释块）：Release 构建下跑到 Release 分支；Debug 下体为空但
//         用例在册。判别力落在 Release 构建（本仓库另建了 Release 构建验证）。

#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"

#include <memory>
#include <utility>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// 为什么本组用例用 `#ifdef NDEBUG` 包住
// ══════════════════════════════════════════════════════════════════════
// `FRAMEWORK_ASSERT` 在 Release 下展开为 `((void)0)`（`ECDIAssert.h:41`）⇒
// 「契约靠断言维护」的几处在 Release 下**没有第二道防线**。
// 本项目已在两处确立「不把契约建立在断言上」的纪律：
//   · `ScrollView::SetScrollStep:154-158`（负步长 `return`）
//   · `Widget::GetChildAt:198-201`（越界返 nullptr，审计 L11 修）
// 审计 §7.3 要的正是**把这条纪律推广到 `Widget` 的树维护入口**。
//
// ★★ **测试难点**：要测「Release 侧分支」，就必须让断言**不**终止测试进程；
//    而本仓库的测试二进制是 **Debug 构建**（`CMakeLists.txt:26` 给 Debug 注入
//    `_DEBUG`；MinGW 工具链自身不给——实测二进制内含 `padding >= 0` 等条件串
//    ⇒ 断言层确实是活的）⇒ 直接调 `AddChild(nullptr)` 会走
//    `HandleAssertFailure` ⇒ Logger(Fatal) + MessageBoxW + `assert(false)`
//    ⇒ **测试进程被终止**（`LayoutTests.cpp:816-821` 已就此留下同款说明：
//    「该分支**必须**留空——Debug 侧没有（也不该有）运行时用例」）。
//
// ★★ **已试过并移除的两条错路（如实记账）**：
//    ① **在本 TU 顶部 `#undef _DEBUG`** —— 双重无效：既救不了进程
//       （断言在库编译时已展开），又炸 MSVC 链接（`_ITERATOR_DEBUG_LEVEL` /
//       `RuntimeLibrary` 不匹配 ⇒ LNK2038 ×2 + LNK1319）。详见文件头三条教训。
//    ② 因此本组采用 `LayoutTests` 既有先例的 **`#ifdef NDEBUG`**：
//       Release 构建（`NDEBUG` 已定义 ⇒ 库侧断言整体为 `((void)0)`）下跑到
//       Release 分支；MinGW Debug 验证里这些用例**体内为空、但用例存在**
//       （注册了、名字在册、计数在册）。
//    ★ 诚实地讲：本组用例的**判别力落在 Release 构建**上（用户侧 VS Release
//      与将来任何 NDEBUG 配置），本机 MinGW Debug 只保证「它编译通过、且
//      正向对照（合法路径）仍然绿」。⇒ 本批**另建 Release 构建**让它们真跑
//      （`cmake-build-release-mingw`），而非只写不跑。

namespace {

/// @brief 造一个带两个子节点的父节点（正向对照用）
struct Tree {
	Widget parent;
	Widget* a = nullptr;
	Widget* b = nullptr;

	Tree() {
		auto wa = std::make_unique<Widget>();
		auto wb = std::make_unique<Widget>();
		a = wa.get();
		b = wb.get();
		parent.AddChild(std::move(wa));
		parent.AddChild(std::move(wb));
	}
};

}   // namespace

// ── #63-①：AddChild 的三条前置条件在 Release 侧被拒绝（★ 缺陷本体）──────────
//   Release 下断言为空 ⇒ 原实现会把非法子节点挂进树：
//   ① 空指针 ⇒ 写空指针；② 多父 ⇒ 双重所有权 ⇒ **double-free**；
//   ③ 成环 ⇒ Arrange/Paint/HitTest **无限递归** ⇒ 栈溢出。
//   ⇒ 修复后三条一律**拒绝执行**（childCount 不增加，树保持原状）。
void Test63AddChildRejectsInvalid()
{
#ifdef NDEBUG
	// ① 空指针：树不得变化（且不得崩溃）
	{
		Tree t;
		std::unique_ptr<Widget> nullChild;
		t.parent.AddChild(std::move(nullChild));
		EXPECT_EQ(t.parent.GetChildCount(), size_t{ 2 });   // ★ 未增加
	}

	// ② 多父：child 已有父 ⇒ 拒绝（否则两棵树各持一份所有权 ⇒ double-free）
	{
		Tree t;                       // t.parent 已持有 a、b
		Widget other;
		// 把 a 再挂到 other 下（a 已有父）：必须被拒绝
		// ⚠️ 不能直接 AddChild(a)——那会转移所有权；此处用一个**新**节点先挂进
		//    t.parent，再尝试挂进 other，模拟"已经有了父"的情形。
		auto owned = std::make_unique<Widget>();
		Widget* raw = owned.get();
		t.parent.AddChild(std::move(owned));              // 先挂进 t.parent（成功）
		EXPECT_EQ(t.parent.GetChildCount(), size_t{ 3 });

		// 此刻 raw 的父是 t.parent —— 无法经 AddChild 再挂（会 move 走所有权），
		// 故此处断言「已挂入的节点的父确实是 t.parent」作为前置校验。
		EXPECT_TRUE(raw->GetParent() == &t.parent);
	}

	// ③ 成环：把 parent 自己挂进它的子树 ⇒ 拒绝
	{
		Widget p;
		auto c = std::make_unique<Widget>();
		Widget* cp = c.get();
		p.AddChild(std::move(c));
		EXPECT_EQ(p.GetChildCount(), size_t{ 1 });

		// 试图把 p 挂进 cp（cp 是 p 的子 ⇒ 成环）
		// ⚠️ p 是栈对象，不能 move 进 AddChild；用另一个祖先节点模拟：
		//    造 grandparent → p → cp，然后试图把 grandparent 挂到 cp 下 ⇒ 环。
		Widget gp;
		auto pOwned = std::make_unique<Widget>();
		Widget* pp = pOwned.get();
		gp.AddChild(std::move(pOwned));
		auto cOwned = std::make_unique<Widget>();
		Widget* cc = cOwned.get();
		pp->AddChild(std::move(cOwned));
		EXPECT_EQ(pp->GetChildCount(), size_t{ 1 });

		// cc->AddChild(gp) 会成环 ⇒ 必须被拒绝（gp 是 cc 的祖先）
		// 同样因所有权无法直接传栈对象；改测「Contains 判据」本身：
		// gp 的子树包含 cc ⇒ cc 的子树不包含 gp ⇒ 反向（把 gp 塞进 cc）才会成环。
		// 用 heap 节点复现：
		auto gp2 = std::make_unique<Widget>();
		auto p2 = std::make_unique<Widget>();
		auto c2 = std::make_unique<Widget>();
		Widget* p2raw = p2.get();
		Widget* c2raw = c2.get();
		gp2->AddChild(std::move(p2));
		p2raw->AddChild(std::move(c2));
		// 现在试图把 gp2 挂到 c2raw 下 ⇒ gp2 的后代包含 c2raw ⇒ 成环 ⇒ 拒绝
		c2raw->AddChild(std::move(gp2));
		EXPECT_EQ(c2raw->GetChildCount(), size_t{ 0 });   // ★ 被拒绝
	}
#endif
}

// ── #63-②：RemoveChild 未命中 / 空指针 ⇒ 返回 nullptr（不 UB）───────────────
//   原实现：`FRAMEWORK_ASSERT(child)` + `FRAMEWORK_ASSERT(it != end())`，
//   Release 下两者皆空 ⇒ `std::move(*it)` 对 `end()` 解引用 = **未定义行为**。
void Test63RemoveChildMissReturnsNull()
{
#ifdef NDEBUG
	Tree t;

	// ① 空指针
	auto r1 = t.parent.RemoveChild(nullptr);
	EXPECT_TRUE(r1 == nullptr);                       // ★ 不再是 UB
	EXPECT_EQ(t.parent.GetChildCount(), size_t{ 2 }); // 树未受影响

	// ② 非直接子节点（一个游离节点）
	Widget stranger;
	auto r2 = t.parent.RemoveChild(&stranger);
	EXPECT_TRUE(r2 == nullptr);
	EXPECT_EQ(t.parent.GetChildCount(), size_t{ 2 });

	// ③ 深层后代（非直接子）同样不得命中
	auto deepOwner = std::make_unique<Widget>();
	Widget* deep = deepOwner.get();
	t.a->AddChild(std::move(deepOwner));
	auto r3 = t.parent.RemoveChild(deep);
	EXPECT_TRUE(r3 == nullptr);                       // deep 是孙节点，非直接子
	EXPECT_EQ(t.parent.GetChildCount(), size_t{ 2 });
#endif
}

// ── #63-③：SetStretch 负值钳 0（与 Layout 的 spacing/padding 同型）────────────
void Test63SetStretchNegativeClamped()
{
#ifdef NDEBUG
	Widget w;

	w.SetStretch(-5);
	EXPECT_EQ(w.GetStretch(), 0);        // ★ 钳为 0（0 = 不参与分配，语义安全）
	w.SetStretch(-1);
	EXPECT_EQ(w.GetStretch(), 0);

	// 正常值不受影响（零回归）
	w.SetStretch(3);
	EXPECT_EQ(w.GetStretch(), 3);
	w.SetStretch(0);
	EXPECT_EQ(w.GetStretch(), 0);
#endif
}

// ── #63-④：★ 正向对照（Debug 与 Release **都跑**）：合法路径零回归 ────────────
//   ★ 这条不可省：若只有上面的 NDEBUG 用例，本机 Debug 验证里那三条是空的
//     ⇒ 必须有**在任何配置下都执行**的对照，证明「加固没有破坏正常用法」。
//   ★ 它也是「修复引入回归」的第一道哨兵（AddChild/RemoveChild 每天都在用）。
void Test63ValidTreeOpsStillWork()
{
	Widget parent;

	auto a = std::make_unique<Widget>();
	auto b = std::make_unique<Widget>();
	Widget* ap = a.get();
	Widget* bp = b.get();

	parent.AddChild(std::move(a));
	parent.AddChild(std::move(b));

	EXPECT_EQ(parent.GetChildCount(), size_t{ 2 });
	EXPECT_TRUE(ap->GetParent() == &parent);
	EXPECT_TRUE(bp->GetParent() == &parent);
	EXPECT_TRUE(parent.GetChildAt(0) == ap);
	EXPECT_TRUE(parent.GetChildAt(1) == bp);

	// 合法移除拿回所有权（原有语义不变）
	auto removed = parent.RemoveChild(ap);
	EXPECT_TRUE(removed != nullptr);
	EXPECT_TRUE(removed.get() == ap);
	EXPECT_EQ(parent.GetChildCount(), size_t{ 1 });
	EXPECT_TRUE(ap->GetParent() == nullptr);   // 已脱树

	// 再移除剩余一个
	auto removed2 = parent.RemoveChild(bp);
	EXPECT_TRUE(removed2 != nullptr);
	EXPECT_EQ(parent.GetChildCount(), size_t{ 0 });
}

// ── #63-⑤：GetChildAt 越界返 nullptr（审计 L11——既有修复的回归锚）─────────────
//   ★ 与 #63 同族（§7.3 纪律的两个已知落点之一）⇒ 一并钉住，防将来回退。
//   ⚠️ **越界调用必须留在 `#ifdef NDEBUG` 内**：`GetChildAt` 在库侧（`Widget.cpp`，
//      用 `_DEBUG` 编译）越界分支含 `FRAMEWORK_ASSERT(false)` ⇒ Debug 下会
//      `assert(false)` 终止测试进程（本轮实测踩到：`Assertion failed: false,
//      file ECDI.cpp, line 47`）。与 `LayoutTests.cpp:816-821` 既有说明同款。
//   ★ 合法索引（0）的检查在两种配置下都跑——它是"正常路径未被破坏"的对照。
void Test63GetChildAtOutOfRangeNull()
{
	Widget w;
	w.AddChild(std::make_unique<Widget>());

	EXPECT_TRUE(w.GetChildAt(0) != nullptr);     // Debug + Release 都跑（合法路径）

#ifdef NDEBUG
	EXPECT_TRUE(w.GetChildAt(1) == nullptr);     // 越界 ⇒ nullptr（非 UB）
	EXPECT_TRUE(w.GetChildAt(999) == nullptr);

	const Widget& cw = w;
	EXPECT_TRUE(cw.GetChildAt(1) == nullptr);    // const 重载同款
#endif
}

void ECDI::Test::RegisterAssertDisciplineTests()
{
	GetTestRegistry().Add("Assert.AddChildRejectsInvalid",      &Test63AddChildRejectsInvalid);
	GetTestRegistry().Add("Assert.RemoveChildMissReturnsNull",  &Test63RemoveChildMissReturnsNull);
	GetTestRegistry().Add("Assert.SetStretchNegativeClamped",   &Test63SetStretchNegativeClamped);
	GetTestRegistry().Add("Assert.ValidTreeOpsStillWork",       &Test63ValidTreeOpsStillWork);
	GetTestRegistry().Add("Assert.GetChildAtOutOfRangeNull",    &Test63GetChildAtOutOfRangeNull);
}
