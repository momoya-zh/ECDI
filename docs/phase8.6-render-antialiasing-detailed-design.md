# Phase 8.6 渲染抗锯齿（圆角覆盖度）详细设计（v1.4）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-11（v1.4 同日修复 `DrawFocusRect` 四角弧心偏移）
> 状态：**✅ 已实现（2026-09-11）**——`ecdi_tests` **174/174 通过**（158 既有零回归 + 15 AA 新增 + 1 ModelProbe 回归）；验证构建 = MinGW g++ 16.1（本项目支持的四工具链之一）；MSVC / Clang / ClangCL 待用户在 VS 确认
> ⚠️ **v1.4 缺陷修复**：修正 `DrawFocusRect` 的**四角弧心偏移**——原实现把「为避开 `PushClip` 而内缩 1px 的可见像素边界」同时用于**圆角弧心定位** ⇒ 右上 / 左下 / 右下三角的弧心各内移 1px（左上角恰好对齐）→ 与背景 `DrawRoundedRect` 的弧错位，在圆角与直线交接处表现为焦点框「内收」。**修复**：引入几何边界 `gRight`/`gBottom` 供**半径与四角弧心**使用（直线端点仍用可见边界），并对弧的采样坐标 clamp 到可见像素范围。**实证**：四角与背景弧理论值最大偏差 **1.34px**（1px 线斜向栅格化容差内）；右上角 `y=54` 由 `148`（差 1.19）→ **`149`（差 0.19）**；回归 **173/173**（本次修复时点；后续 +1 ModelProbe 回归 ⇒ 见 v1.4 条目末）。
> ⚠️ **v1.3 缺陷修复**：修正 v1.2 落地版本的 **`FillPatchFromMask` 行宽缺陷**——`stride` 误用 `R * 4`（当前半径）而非 `m_patchSurface.size * 4`（DIB 实际行宽）。**症状**：同一后端「先画大半径控件、再画小半径控件」时后者的圆角补丁整体行错位（被垂直压缩）→ 圆角渲染错乱；ModelProbe 的 hover 重绘触发该失效路径。**实证**：对照实验差异 **40/289 → 修复后 0/289**；回归测试维持 **173/173**。
> 前置：`phase8.6-render-antialiasing-requirements.md` **v1.1**（封版）/ `phase8.6-render-antialiasing-preliminary-design.md` **v1.1**（封版）
> 一句话：把初设的「算法方向」写成**可直接实施的规格**——数据结构 / 生成算法 / 缓存 API / 三条合成路径 / 索引变换 / 测试逐用例期望值 / 文件改动与验收
> 定位：**本文档不含架构讨论**（初设已定案）；只做「数学与实现契约写死」
> v1.1 修订：**7 项全采纳**——修正 §3.4 的 `R=1` 手算错误（**219 → 207**，并升级为硬断言）+ 新增精确字节锚点表与浮点精确性论证（§3.4b/c）/ §5.2 类型链显式化 / **§6.4 `tol` 改用参考实现实测重写**（`R/S` → **`0.5·√R/S`**）/ §5.3 整数边界语义钉死 + R4 失效边界 / `samples` 契约明确 / §5.6 有效区域声明 / §6.2 L2-a 清底注意点

---

## 1. 范围映射与待定项收口

### 1.1 初设 §11.2 的 11 项待定项（全部**形成实施决策**）

> **措辞说明（v1.1 修订——评审 §10）**：v1.0 写「全部收口」，但其中 `S=8`、`tol`、AlphaBlend 是否合并三项**保留了「验证后调整」的机制**——它们是**默认值/实现参数已定稿**，而非永久不可变的数学契约。故作「全部形成实施决策」。

| # | 待定项（初设） | **详设定稿** | 节 |
|---|---|---|---|
| 1 | `ScratchDIB` 封装形态 | **`GDIBackend` 私有嵌套结构 `PatchSurface`**（不独立成文件/类——它是纯实现细节） | §5.1 / §5.6 |
| 2 | 角方向变换实现 | **索引变换辅助函数 `MaskIndexX/Y`**（单份掩码；**不预生成 4 份**） | §5.5 |
| 3 | `S` 默认值 | **8**；可经 `CornerMaskCache::SetSamples` 变更。若 §6 的目视对比 V2 发现台阶 → 改 16，**属参数调整、非契约变更** | §4 |
| 4 | `tol(S,R)` 最终值 | **`tol(S,R) = 0.5·sqrt(R)/S`**（v1.1 由参考实现实测扫描定稿——v1.0 的 `R/S` 已废弃）+ 扩展规则 | §6.4 |
| 5 | 4 次 `AlphaBlend` 是否合并 | **不合并**——每次仅 R×R，其调用开销远小于「全表面 blit」；若 §6 性能实测显示调用开销显著，**回到本文档修订** | §5.3 |
| 6 | AA 开关暴露方式 | **`GDIBackend` 公有方法**（`GDIBackend.h` 是 Internal 头 → **非公共 API**）+ 默认**开启** | §5.7 |
| 7 | 缓存诊断阈值 | **32 条**；超阈值记**一次** Warning（防刷屏） | §4 |
| 8 | `a < 1` 全矩形 DIB 是否迁移 | **不迁移**（保持零回归；沿用现状 per-call 建销） | §5.4 |
| 9 | 1×1 源拉伸 `AlphaBlend` | **不采用**（记录为潜在优化，需先验证跨版本可靠性） | §5.4 |
| 10 | 建刷是否引入 HBRUSH 缓存 | **不引入**——沿用决策 24「GDI 对象每次创建/销毁」；本设计 **1 次建刷服务 3 条带**（比现状 `DrawRect` 每次建销更省；字体是既有唯一例外） | §5.3 |
| 11 | L1 断言代码形态 | §6.1 逐用例定稿（含精确期望值与容差） | §6.1 |

### 1.2 需求 / 初设 → 详设节

| 来源 | 内容 | 详设节 |
|---|---|---|
| R7 / 初设 §9 | 覆盖度生成器可测试接缝（新建 Internal 头） | §2 / §3 / §4 |
| 初设 §3 | `CornerCoverageMask` 离散几何（pixel-square coverage） | §2 / §3 |
| 初设 §4 | 形状装配（3 带 + 4 角补丁） | §5.3 |
| 初设 §5 | 三条合成路径 | §5.2 / §5.3 / §5.4 |
| 初设 §6.1 | `CornerMaskCache` 规格 | §4 |
| 初设 §6.2 | `ScratchDIB`（→ `PatchSurface`）规格 | §5.6 |
| 初设 §8 | L1 / L2 测试方向 | §6 |
| 初设 §9 | 影响面与文件清单 | §7 |
| 初设 §11 | 待定项与风险 | §1.1 / §8 / §9 |

---

## 2. `CornerCoverageMask` 数据结构定稿（新建 Internal 头全文）

**文件**：`ECDI/src/Render/CornerCoverageMask.h`（Internal——**不进公共 API**，与 `RecordingBackend.h` 同例）

```cpp
﻿#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace ECDI {

/// @brief 单象限覆盖度掩码（Phase 8.6 · 第一层「覆盖度生成」）
/// @details
/// 采用 **pixel-square coverage** 模型：每个掩码像素代表单位像素方格
/// `[i, i+1) × [j, j+1)`；coverage = 该方格与圆盘相交面积占比的 S×S 采样估计。
/// **不是**「像素中心是否在圆内」的二值判定（既有 BlendAlphaSolid 用的是后者）。
///
/// 几何：掩码为 `R × R`（R = 半径）；圆心位于掩码局部坐标 `(R, R)`（即掩码右下角），
/// 故圆盘只占用掩码的左上象限。像素 `(0,0)` = 最外侧角点，`(R-1,R-1)` = 紧邻圆心。
///
/// 复用：圆角矩形四角 / 真圆四象限 / 胶囊两端 —— 边界几何都是同一个四分之一圆盘，
/// 故四个角共享**同一份 canonical 掩码**，通过 `MaskIndexX/Y` 做索引变换映射。
///
/// 纯几何、零 GDI 依赖（不碰 HDC/HBITMAP）→ 可被无窗口单元测试直接调用。
struct CornerCoverageMask {

	int radius = 0;                      ///< 半径 R（同时也是掩码边长）；<= 0 表示空掩码
	std::vector<std::uint8_t> coverage;  ///< R×R 行连续，stride = radius（无对齐填充）

	/// @brief 读取掩码像素（**不做边界检查**——调用方保证 0 <= i, j < radius）
	std::uint8_t At(int i, int j) const noexcept {

		return coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(radius)
		                + static_cast<std::size_t>(i)];

	}

	bool Empty() const noexcept { return radius <= 0 || coverage.empty(); }

};

/// @brief 角标识（决定索引变换方向——不是「旋转」，只是哪一角）
enum class CornerId { TopLeft, TopRight, BottomLeft, BottomRight };

/// @brief 掩码索引变换：x 轴（右侧两角需水平反向）
/// @param local  该角补丁内的局部列（0 .. radius-1）
inline int MaskIndexX(int local, int radius, CornerId corner) noexcept {

	const bool flip = (corner == CornerId::TopRight || corner == CornerId::BottomRight);
	return flip ? (radius - 1 - local) : local;

}

/// @brief 掩码索引变换：y 轴（下方两角需垂直反向）
/// @param local  该角补丁内的局部行（0 .. radius-1）
inline int MaskIndexY(int local, int radius, CornerId corner) noexcept {

	const bool flip = (corner == CornerId::BottomLeft || corner == CornerId::BottomRight);
	return flip ? (radius - 1 - local) : local;

}

/// @brief 生成单象限覆盖度掩码（Phase 8.6）
/// @param radius  半径 R（见 §3 算法）
/// @param samples 超采样倍率 S。**推荐取 2 的幂（4 / 8 / 16）**：
///                (a) 舍入恒为 round-half-up；(b) 运算全程为二进制有理数 → `float` 精确
///                （见 §3.4c）。**奇数合法**，但舍入退化为 round-half-down 且失去跨平台精确性。
/// @return R×R 掩码；`radius <= 0` 或 `samples <= 0` → 空掩码（Empty() == true）
CornerCoverageMask GenerateCornerMask(int radius, int samples);

/// @brief 覆盖度掩码缓存（键 = **effective 整数半径**）
/// @details
/// 键为调用侧完成 `lround` **且** `clamp` 之后的最终半径 R（不是入参 float）——
/// 否则 `3.1 / 3.2 / 3.3` 三个入参会重复生成同一几何，且条目数统计失真。
/// 永不主动失效（掩码与尺寸/位置/颜色/主题无关）；随宿主析构。
/// ⚠️ 非线程安全——与 `GDIBackend` 同生命周期、同线程（同步顺序绘制）。
class CornerMaskCache {

public:

	/// @brief 取掩码（未命中则生成并缓存）
	/// @param radius effective 整数半径（**调用方保证 > 0**）
	/// @return 掩码常引用——`std::map` 保证元素引用稳定（后续插入不影响已返回的引用）
	const CornerCoverageMask& Get(int radius);

	/// @brief 设置超采样倍率 S
	/// @details ⚠️ **变更会清空缓存**——不同 S 生成的掩码不可混用（否则量化级别不一致）。
	/// **契约（v1.1 明确——评审 §5）**：`samples <= 0` → **no-op**；
	/// `samples > 0` → **一律接受（含奇数）**，仅**推荐**取 2 的幂（理由见 §3.4c）。
	void SetSamples(int samples);

	int GetSamples() const noexcept { return m_samples; }

	std::size_t Size() const noexcept { return m_masks.size(); }

	/// @brief 条目数诊断阈值（超出记一次 Warning——防异常半径导致缓存无界增长）
	static constexpr std::size_t kDiagnosticThreshold = 32;

private:

	std::map<int, CornerCoverageMask> m_masks;   ///< 键 = effective 整数半径
	int m_samples = 8;                           ///< 超采样倍率（默认 8，见初设 §2.3）
	bool m_thresholdLogged = false;              ///< 阈值告警只记一次

};

}
```

**设计说明**：

| 项 | 定稿依据 |
|---|---|
| `stride = radius`（无 padding） | `R` 是整数，行宽即列数；对齐优化属 YAGNI |
| `std::map` 而非 `unordered_map` | 条目数极小（个位到十余条）；`map` 保证**元素引用稳定**（返回 `const&` 安全），且有序便于调试 |
| `At` 不做边界检查 | 调用方（GDIBackend）的索引由几何保证在界内；热路径不加分支（`radius` 不匹配属内部不变量违反，见 §5.4 防御） |
| `CornerId` 而非「旋转/镜像」 | 它回答「这是哪一角」，翻转只是**索引变换的结果**（初设 §3.2 评审 §十三） |

---

## 3. `GenerateCornerMask` 算法定稿

**文件**：`ECDI/src/Render/CornerCoverageMask.cpp`

### 3.1 算法全文

```cpp
CornerCoverageMask GenerateCornerMask(int radius, int samples) {

	CornerCoverageMask mask;

	// 参数校验：非法 → 空掩码（不抛异常、不产生半成品）
	if (radius <= 0 || samples <= 0) {

		return mask;

	}

	const int R = radius;
	const float fR = static_cast<float>(R);
	const float invS = 1.0f / static_cast<float>(samples);
	const int total = samples * samples;
	const float radiusSq = fR * fR;

	mask.radius = R;
	mask.coverage.assign(static_cast<std::size_t>(R) * static_cast<std::size_t>(R), 0);

	for (int j = 0; j < R; ++j) {

		for (int i = 0; i < R; ++i) {

			int inside = 0;

			// S×S 规则网格子样本（子样本中心 = (s+0.5)/S）
			for (int sy = 0; sy < samples; ++sy) {

				const float py = static_cast<float>(j) + (static_cast<float>(sy) + 0.5f) * invS;
				const float dy = py - fR;
				const float dySq = dy * dy;

				for (int sx = 0; sx < samples; ++sx) {

					const float px = static_cast<float>(i) + (static_cast<float>(sx) + 0.5f) * invS;
					const float dx = px - fR;

					// 圆心 (R, R)；边界含圆周（<=）——与初设 §3.1 规格 5 一致
					if (dx * dx + dySq <= radiusSq) {

						++inside;

					}

				}

			}

			// 舍入：round(255 * inside / total)——整数实现（确定性、无浮点误差累积）
			// ⚠️ `total = S²`；S 取 2 的幂时 total 为偶数 → 该式即严格 round-half-up。
			//    S 为奇数时 total 为奇数，`total/2` 整数除截断 → 退化为 round-half-down
			//    （无害，但规则必须写死——初设 §3.1 规格 5 的确定性要求）
			const int byteValue = (255 * inside + total / 2) / total;

			mask.coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(R)
			              + static_cast<std::size_t>(i)]
				= static_cast<std::uint8_t>(byteValue);

		}

	}

	return mask;

}
```

### 3.2 关键规则（逐条写死）

| # | 规则 | 值 |
|---|---|---|
| 1 | 像素方格 | `[i, i+1) × [j, j+1)`，边长 1、面积 1 |
| 2 | 子样本坐标 | `(i + (sx + 0.5)/S, j + (sy + 0.5)/S)`，`sx, sy ∈ [0, S)` |
| 3 | 圆心 | 掩码局部 `(R, R)` |
| 4 | 判定 | `dx² + dy² <= R²`（**含**圆周；`R` 整数故 `R*R` 精确） |
| 5 | 舍入 | `byte = (255 * inside + total/2) / total`，`total = S²` |
| 6 | `S` 推荐值 | **2 的幂**（4/8/16）——(a) 舍入恒为 round-half-up (b) 运算精确（§3.4c）；默认 8 |
| 7 | 数组序 | 行连续 `coverage[j * R + i]` |
| 8 | 非法入参 | 返回**空掩码**（`Empty() == true`），不抛异常 |

### 3.3 不做「内外快速路径」（显式 YAGNI）

初设 §2.3 已论证掩码**一次性生成**。成本量级（最差情形，无快速路径）：

| R | S | 距离判定次数 `R²·S²` | 量级 |
|---|---|---|---|
| 8 | 8 | 4,096 | 可忽略 |
| 32 | 8 | 65,536 | 可忽略 |
| 32 | 16 | 262,144 | 可忽略（一次性） |

→ **不引入**「像素完全在圆内/圆外则跳过采样」的快速路径：它需要额外的不等式界（`距离 ± 半对角`），引入第二个代码分支与新的出错面，而收益在一次性的量级下不可感知。

（若未来 `R`/`S` 出现数量级增长，再回到本文档评估。）

### 3.4 精确字节锚点与浮点精确性（v1.1 重写——初稿手算错误已修正）

> ⚠️ **v1.0 的 `R = 1` 手工推导有误**（v1.0 原文：「圆外 9 / 圆内 55 / byte 219 / 误差 0.0734」）。**正确值见下**（由参考实现复算）。这是初稿中最严重的一处错误——它恰好是验证整个 coverage generator 的最小离散样本。

#### 3.4a `R = 1, S = 8` 逐项推导

掩码 1×1；子样本坐标 ∈ `{0.0625, …, 0.9375}`（8 个，关于 0.5 对称）；圆心 `(1,1)`。令 `u = 1−x`、`v = 1−y`，圆外条件 `u² + v² > 1`：

| `u` | `u²` | 需 `v² >` | 满足的 `v`（括号内为其 `v²`） | 计数 |
|---|---|---|---|---|
| 0.9375 | 0.8789 | 0.1211 | 0.9375(0.8789) / 0.8125(0.6602) / 0.6875(0.4727) / 0.5625(0.3164) / 0.4375(0.1914) | **5** |
| 0.8125 | 0.6602 | 0.3398 | 0.9375(0.8789) / 0.8125(0.6602) / 0.6875(0.4727) | **3** |
| 0.6875 | 0.4727 | 0.5273 | 0.9375(0.8789) / 0.8125(0.6602) | **2** |
| 0.5625 | 0.3164 | 0.6836 | 0.9375(0.8789) | **1** |
| 0.4375 | 0.1914 | 0.8086 | 0.9375(0.8789) | **1** |
| ≤ 0.3125 | ≤ 0.0977 | > 0.90 | 无 | 0 |

```text
圆外样本数 = 5 + 3 + 2 + 1 + 1 = 12        ← v1.0 误算为 9
圆内样本数 = 64 − 12 = 52                  ← v1.0 误算为 55
coverage   = 52/64 = 0.8125                ← v1.0 误写为 0.859375
byte       = round(255 × 0.8125) = round(207.1875) = 207    ← v1.0 误写为 219
连续解 π/4 ≈ 0.785398 → 采样误差 0.027102    ← v1.0 误写为 0.0734
```

**v1.0 手算错在哪**（三处漏项）：`u=0.9375` 行漏了 `v=0.9375`（0.8789 > 0.1211 ✓）；`u=0.8125` 行漏了 `v=0.9375` 与 `v=0.8125`；`u=0.6875` 行漏了 `v=0.8125`。

> **方法论教训（写入文档）**：这类「两两求和超过阈值」的计数表**必须用参考实现复算**，手算漏项的概率极高且不易自检。**§3.4b 的锚点表即由与本规格逐条对应的脚本生成**，不是手算。

#### 3.4b 精确字节锚点表（`S = 8`；行主序 `coverage[j*R + i]`）

| `R` | 掩码字节 | `Σ` |
|---|---|---|
| 1 | `[207]` | 207 |
| 2 | `[84, 235, 235, 255]` | 809 |
| 3 | `[4, 143, 243, 143, 255, 255, 243, 255, 255]` | 1796 |
| 4 | `[0, 40, 179, 247, 40, 243, 255, 255, 179, 255, 255, 255, 247, 255, 255, 255]` | 3215 |

抽样自检：`R = 4` 第 0 行 `[0, 40, 179, 247]`——最外角点全外 → `0`，向圆心方向单调增至 `247` ✓（与 L1-d 单调性一致）；`R = 4` 末元素 `(3,3)` 紧邻圆心 → `255`（全内）✓。

**用途**：这四条**精确相等**断言（无容差）能同时抓出**采样坐标错误**、**舍入规则错误**（truncate 代替 round 会立即偏移）、**判定边界错误**——比面积守恒（容差式）敏感得多。**面积守恒抓「几何整体对不对」，锚点抓「实现细节对不对」，二者互补**（见 §6.1）。

#### 3.4c 浮点精确性（精确断言之所以能成立）

`S` 取 **2 的幂**（4/8/16…）时，全部样本坐标与平方和都是**分母为 2 的幂的二进制有理数**：

```text
px  = i + (2·sx + 1) / (2S)              → 分母 2S（2 的幂）
dx  = px − R = [2S(i − R) + (2sx + 1)] / (2S)
dx² = k² / (2S)²      ，|k| ≤ 2S·R + 2S = 2S(R + 1)
```

只要 `k²` 的有效位数 ≤ 24（`float` 尾数位），`dx²` 在 `float` 下**精确无舍入**：

| `S` | `R` | `k` 上界 | `k²` 有效位数 | 精确 |
|---|---|---|---|---|
| 8 | 8 | 143 | 15 | ✅ |
| 8 | 64 | 1039 | 21 | ✅ |
| 8 | 128 | 2063 | 23 | ✅ |
| 8 | 256 | 4111 | 25 | ❌ 可能舍入 |
| 16 | 64 | 2079 | 23 | ✅ |

→ **在 `R ≤ 128`（`S = 8`）/ `R ≤ 64`（`S = 16`）范围内，掩码字节值跨编译器、跨平台完全确定**（本阶段实际 UI 半径远小于该范围）。§3.4b 的锚点取 `R ≤ 4`，余量充足。

> **这是「`S` 取 2 的幂」的第二个理由**（第一个是舍入恒为 round-half-up）：**运算全程精确**。`S` 为奇数时，`(2sx+1)/(2S)` 的分母非 2 的幂，坐标将出现二进制无限循环小数 → 引入浮点误差 → **精确断言不再跨平台稳定**（此时只能退化为容差式断言）。

---

## 4. `CornerMaskCache` 实现定稿

```cpp
const CornerCoverageMask& CornerMaskCache::Get(int radius) {

	const auto it = m_masks.find(radius);

	if (it != m_masks.end()) {

		return it->second;   // 命中（引用稳定）

	}

	// 诊断阈值：只在「首次越过」时告警一次（防异常半径来源导致刷屏）
	if (m_masks.size() >= kDiagnosticThreshold && !m_thresholdLogged) {

		Logger::Log(LogLevel::Warning,
			L"CornerMaskCache: entry count exceeded diagnostic threshold "
			L"(unexpected radius churn?)");

		m_thresholdLogged = true;

	}

	// std::map::emplace 返回的引用稳定——后续插入不影响本次返回的引用
	return m_masks.emplace(radius, GenerateCornerMask(radius, m_samples)).first->second;

}

void CornerMaskCache::SetSamples(int samples) {

	if (samples <= 0 || samples == m_samples) {

		return;   // 契约（§2）：samples <= 0 → no-op；与当前值相同 → 幂等 no-op

	}

	m_samples = samples;

	m_masks.clear();          // ⚠️ 必须清空：不同 S 生成的掩码量化级别不同，不可混用
	m_thresholdLogged = false;

}
```

**定稿要点**：

| 项 | 值 | 理由 |
|---|---|---|
| 键 | effective 整数半径 `R` | 初设 §6.1「键的规范化」（评审 §五） |
| 命中检查 | `std::map::find` | O(log n)，n 极小 |
| 引用稳定性 | `std::map` 元素引用在插入时稳定 | `Get` 返回 `const&` 后调用方立即使用，无失效风险 |
| `SetSamples` 语义 | **清空缓存** | 否则同一半径会同时存在 4×/8×/16× 三种量化版本的掩码 |
| 诊断阈值 | **32 条**，超阈值记**一次** Warning | 初设 §6.1「不设淘汰策略 + 轻量诊断阈值」的落定；只记一次避免刷屏 |
| 线程安全 | 否（文档声明） | 与 `GDIBackend` 同步顺序绘制一致 |

---

## 5. `GDIBackend` 实施规格

### 5.1 新增成员与辅助函数

**`ECDI/src/Render/GDIBackend.h`** 追加：

```cpp
public:
	/// @brief 设置抗锯齿开关（Phase 8.6；后端级，默认开启）
	/// @details Internal 头内的公有方法——**不属于公共 API**（`include/ECDI` 零改动）。
	///          用途：L2 测试需要「开/关对照」；生产代码无需调用。
	void SetAntiAliasing(bool enabled) noexcept { m_antiAliasing = enabled; }
	bool IsAntiAliasingEnabled() const noexcept { return m_antiAliasing; }

private:
	/// @brief 角补丁绘制面（Phase 8.6）：32bpp 顶降**预乘** DIB，单个复用、**只增不减**
	/// @details ⚠️ 仅用于同步、顺序的 GDIBackend 绘制流程；**不保证并发/重入安全**。
	struct PatchSurface {

		HDC dc = nullptr;
		HBITMAP bitmap = nullptr;
		HBITMAP oldBitmap = nullptr;   ///< SelectObject 返回值（释放时先恢复再删——GDI 铁律）
		void* bits = nullptr;
		int size = 0;                  ///< 当前边长（正方形；只增不减）

		/// @brief 确保边长 >= requiredSize（不足则重建；成功返回 true）
		bool Ensure(HDC reference, int requiredSize);

		/// @brief 释放（严格逆序：恢复 oldBitmap → DeleteObject → DeleteDC）
		void Release();

	};

	/// @brief 既有两条分支的搬移（行为逐位不变）——R == 0 或 AA 关闭时走此路
	void DrawRoundedRectLegacy(const Rect& rect, float cornerRadius, const Color& color);

	/// @brief a == 1 的 AA 路径：3 FillRect + 4 角补丁（§5.3）
	void DrawRoundedRectOpaqueAA(const Rect& rect, int radius, const Color& color,
	                             const CornerCoverageMask& mask);

	/// @brief a < 1 的 AA 路径：全矩形 DIB + 覆盖度（§5.4）
	void DrawRoundedRectBlendedAA(const Rect& rect, int radius, const Color& color,
	                              const CornerCoverageMask& mask);

	/// @brief 把掩码 + 颜色按角方向写入 PatchSurface（预乘 BGRA）
	void FillPatchFromMask(const CornerCoverageMask& mask, const Color& color, CornerId corner);

	/// @brief 把 PatchSurface 的 R×R 区域 AlphaBlend 到目标 (x, y)
	void BlendPatch(HDC target, int x, int y, int size);

	CornerMaskCache m_cornerMaskCache;   ///< Phase 8.6：覆盖度掩码缓存（键 = effective 整数半径）
	PatchSurface m_patchSurface;         ///< Phase 8.6：角补丁绘制面（复用，只增不减）
	bool m_antiAliasing = true;          ///< Phase 8.6：AA 开关（后端级；默认开）
```

**`#include` 追加**：`#include "Render/CornerCoverageMask.h"`（同目录 Internal 头，与 `GDIBackend.h` 现有 `Render/...` 引法一致）。

**析构追加**：`~GDIBackend()` 中调用 `m_patchSurface.Release()`（与既有 `ReleaseBackBuffer()` 并列）。

### 5.2 路径选择定稿

```cpp
void GDIBackend::DrawRoundedRect(const Rect& rect, float cornerRadius, const Color& color)
{
	// ── 既有守卫（不变）──
	if (rect.width <= 0.0f || rect.height <= 0.0f)
	{
		return;
	}

	// ── 既有钳制（显式类型链——见下方「类型链说明」）──
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);

	const LONG roundedRadius   = static_cast<LONG>(std::lround(cornerRadius));   // ① 量化
	const LONG maxRadius       = (std::min)(w, h) / 2;                           // ② 上界
	const LONG effectiveRadius = std::clamp(roundedRadius, 0L, maxRadius);       // ③ 钳制

	const int R = static_cast<int>(effectiveRadius);   // ④ effective 整数半径 = 缓存键（§4）

	// ① R == 0 或 AA 关闭 → legacy（与改造前逐位一致；初设 §7 B1/B2）
	if (R == 0 || !m_antiAliasing)
	{
		DrawRoundedRectLegacy(rect, cornerRadius, color);   // ⚠️ 传原始 cornerRadius（见下）
		return;
	}

	// ② a == 1 需要角补丁绘制面——准备失败则落 legacy（fail-safe：宁可无 AA，不可缺角）
	if (color.a >= 1.0f && !m_patchSurface.Ensure(m_memoryDC, R))
	{
		Logger::Log(LogLevel::Warning,
			L"GDIBackend: patch surface unavailable - rounded rect AA skipped");
		DrawRoundedRectLegacy(rect, cornerRadius, color);
		return;
	}

	// ③ 取掩码（effective 整数半径——§4）
	const CornerCoverageMask& mask = m_cornerMaskCache.Get(R);

	if (color.a >= 1.0f)
	{
		DrawRoundedRectOpaqueAA(rect, R, color, mask);      // §5.3
	}
	else
	{
		DrawRoundedRectBlendedAA(rect, R, color, mask);     // §5.4
	}
}
```

**类型链说明（v1.1 修订——评审 §2）**：v1.0 把它写成一行：

```cpp
const int R = static_cast<int>(std::clamp(std::lround(cornerRadius), 0L, (std::min)(w, h) / 2));
```

**核验结论：该式实际可以编译**——`std::lround` 返回 `long`、`0L` 是 `long`、`w`/`h` 为 `LONG`（Windows 下 `typedef long LONG`）故 `(std::min)(w,h)/2` 也是 `long`，三参类型一致 → `std::clamp<long>` 推导成立。（原实现的同一写法已在框架内编译通过，可作旁证。）

但采用上面的**显式三段式**更好，理由：

| # | 理由 |
|---|---|
| 1 | 把「effective integer radius」的链路（**量化 → 上界 → 钳制 → int**）**逐行写出来**，与 §4 的缓存键定义直接对应——初设 §6.1「键的规范化」正是这条链 |
| 2 | **消除类型推导的隐性依赖**：当前成立的前提是 `w`/`h` 恰为 `LONG`。若将来重构为 `int`（如浮点坐标落地后的调整），一字之差就会演变成 `clamp` 三参类型不一致的编译错误；显式链对类型变化免疫 |
| 3 | `maxRadius` 单独具名后，可在注释里直接挂上 §2.1「钳制保证 `2R <= min(w,h)` ⇒ 三条带尺寸非负」的论证，与 §5.3 的退化处理互相呼应 |

**两个定稿细节**：

1. **`DrawRoundedRectLegacy` 接收原始 `cornerRadius`（不是已钳制的 `R`）**——搬移时保持入参签名与原实现一致，让 legacy **内部自行钳制**。虽然钳制结果相同，但「原样搬移、零改动」是逐位一致的**最强保证**（避免「等价改写」引入的隐性差异）。
2. **`PatchSurface` 失败降级为 legacy（不是「画带不画角」）**——`Ensure` 失败意味着内存紧张；此时输出一个**完整的硬边圆角矩形**（视觉上仅无 AA）优于「缺角的形状」。与 R6 DWM 失败容忍同款思路。失败**不缓存**（下次调用重试）。

### 5.3 `a == 1` 路径定稿

```cpp
void GDIBackend::DrawRoundedRectOpaqueAA(const Rect& rect, int R, const Color& color,
                                         const CornerCoverageMask& mask)
{
	const LONG x = static_cast<LONG>(rect.x);
	const LONG y = static_cast<LONG>(rect.y);
	const LONG w = static_cast<LONG>(rect.width);
	const LONG h = static_cast<LONG>(rect.height);

	// ── ① 三条实心带（覆盖度恒为 1 → 与覆盖度混合路径数学等价，初设 §2.2 已证）──
	// 因 clamp 保证 2R <= min(w,h)，三条带的宽高恒 >= 0；零尺寸者跳过（初设 §4.3）
	const RECT bands[3] = {
		{ x,     y + R,     x + w,     y + h - R },   // 中带（全宽）
		{ x + R, y,         x + w - R, y + R     },   // 上带（去左右两角）
		{ x + R, y + h - R, x + w - R, y + h     }    // 下带（去左右两角）
	};

	HBRUSH brush = CreateSolidBrush(ToColorRef(color));
	if (!brush)
	{
		return;   // 决策 30：局部失败跳过
	}

	HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(m_memoryDC, brush));

	for (const RECT& rc : bands)
	{
		if (rc.right > rc.left && rc.bottom > rc.top)
		{
			FillRect(m_memoryDC, &rc, brush);
		}
	}

	SelectObject(m_memoryDC, oldBrush);
	DeleteObject(brush);   // 决策 24：1 次建销服务 3 条带（§1.1 待定项 10）

	// ── ② 四个角补丁 ──
	const struct { LONG x; LONG y; CornerId corner; } kCorners[4] = {
		{ x,         y,         CornerId::TopLeft     },
		{ x + w - R, y,         CornerId::TopRight    },
		{ x,         y + h - R, CornerId::BottomLeft  },
		{ x + w - R, y + h - R, CornerId::BottomRight }
	};

	for (const auto& c : kCorners)
	{
		FillPatchFromMask(mask, color, c.corner);         // 写入 PatchSurface（预乘 BGRA，R×R）
		BlendPatch(m_memoryDC, static_cast<int>(c.x), static_cast<int>(c.y), R);
	}
}
```

**角补丁坐标表（定稿）**：

| 角 | 目标位置 | 掩码索引变换 |
|---|---|---|
| TopLeft | `(x, y)` | `flipX = false`, `flipY = false` |
| TopRight | `(x + w − R, y)` | `flipX = true`,  `flipY = false` |
| BottomLeft | `(x, y + h − R)` | `flipX = false`, `flipY = true` |
| BottomRight | `(x + w − R, y + h − R)` | `flipX = true`,  `flipY = true` |

**「三条带覆盖度恒为 1」的语义钉死（v1.1 补充——评审 §4）**：

```text
由于 Phase 8.6 的渲染几何坐标为【整数像素边界】，三条带的边界与像素方格边界【重合】，
因此其 pixel-square coverage 恒为 1 —— 不需要（也无法从）掩码生成。
```

⚠️ **该结论的适用前提与失效边界**（必须写清楚，避免被直接套用到未来）：

| 前提 | 失效条件 |
|---|---|
| 矩形几何为整数像素边界（现有 `lround`/`static_cast<LONG>` 保证，初设 §1.3 已论证） | **Phase 8 R4「渲染坐标浮点化」落地后失效**——届时带边界将落在像素方格内部，其 coverage 不再恒为 1，**三条带也必须走覆盖度路径** |

因此本节的实现**不是**「直边永远不需要 AA」的证明，而是「**在当前整数几何下**，直边 coverage ≡ 1，故 `FillRect` 与覆盖度混合路径逐位等价」的落地。R4 落地时的正确做法见初设 §3.5（canonical 几何原则）与 D10：**覆盖度生成器保持与形状装配解耦**，届时新增「带」的覆盖度计算即可，掩码机制本身不变。

> **补一句语义区分（评审 §4 的原意）**：圆角区用「连续面积覆盖度」语义，矩形主体用「GDI 整数矩形填充」语义——**这不是不一致**，而是「整数边界下两者的覆盖度恰好都是 1」这一事实的两种等价表达（§2.2 已给等价性证明）。

### 5.4 `a < 1` 路径定稿（既有实现的**最小改动**）

**结构不变**：整张 `w×h` 32bpp 顶降预乘 DIB → 逐像素填充 → **1 次** `AlphaBlend`。**只把角部的二值判定换成覆盖度**。

```cpp
void GDIBackend::DrawRoundedRectBlendedAA(const Rect& rect, int R, const Color& color,
                                          const CornerCoverageMask& mask)
{
	// ... 前段（空矩形守卫 / 尺寸 / 预乘色分量 / DIB 创建 / SelectObject）与既有
	//     BlendAlphaSolid 完全相同，不再重复 ...

	// 预计算（循环外）：颜色分量 × color.a —— 每像素只再乘 c
	const float ba = color.b * color.a;
	const float ga = color.g * color.a;
	const float ra = color.r * color.a;
	const BYTE alphaByte = ToByte(color.a);

	// 不变量防御：掩码半径必须等于 R（同一缓存键必然成立）；不匹配则退化为硬边，不崩溃
	const bool maskUsable = (mask.radius == R);

	for (int row = 0; row < height; ++row)
	{
		BYTE* line = dst + static_cast<std::size_t>(row) * dibStride;
		const float cy = static_cast<float>(row) + 0.5f;

		for (int col = 0; col < width; ++col)
		{
			const float cx = static_cast<float>(col) + 0.5f;

			// 覆盖度：带内恒为 1；四角由掩码给出；其余为 0（透明）
			float cov = 1.0f;
			bool inside = true;

			CornerId corner = CornerId::TopLeft;
			int localI = col;
			int localJ = row;
			bool inCorner = false;

			if (cx < static_cast<float>(R) && cy < static_cast<float>(R))
			{
				inCorner = true; corner = CornerId::TopLeft;
				localI = col;                     localJ = row;
			}
			else if (cx > static_cast<float>(width - R) && cy < static_cast<float>(R))
			{
				inCorner = true; corner = CornerId::TopRight;
				localI = col - (width - R);       localJ = row;
			}
			else if (cx < static_cast<float>(R) && cy > static_cast<float>(height - R))
			{
				inCorner = true; corner = CornerId::BottomLeft;
				localI = col;                     localJ = row - (height - R);
			}
			else if (cx > static_cast<float>(width - R) && cy > static_cast<float>(height - R))
			{
				inCorner = true; corner = CornerId::BottomRight;
				localI = col - (width - R);       localJ = row - (height - R);
			}

			if (inCorner)
			{
				if (maskUsable)
				{
					cov = static_cast<float>(
						mask.At(MaskIndexX(localI, R, corner), MaskIndexY(localJ, R, corner)))
						/ 255.0f;
				}
				else
				{
					// 防御退化：退回既有二值语义（不崩溃、不产生越界读）
					const float dx = cx - (corner == CornerId::TopLeft || corner == CornerId::BottomLeft
					                       ? static_cast<float>(R)
					                       : static_cast<float>(width - R));
					const float dy = cy - (corner == CornerId::TopLeft || corner == CornerId::TopRight
					                       ? static_cast<float>(R)
					                       : static_cast<float>(height - R));
					cov = (dx * dx + dy * dy <= static_cast<float>(R * R)) ? 1.0f : 0.0f;
				}

				inside = (cov > 0.0f);
			}

			if (!inside)
			{
				line[col * 4 + 0] = 0;
				line[col * 4 + 1] = 0;
				line[col * 4 + 2] = 0;
				line[col * 4 + 3] = 0;   // 角外透明
				continue;
			}

			// 预乘 BGRA（约束 1）；覆盖度作为额外 alpha 调制
			const BYTE a = ToByte(color.a * cov);
			line[col * 4 + 0] = ToByte(ba * cov);   // B
			line[col * 4 + 1] = ToByte(ga * cov);   // G
			line[col * 4 + 2] = ToByte(ra * cov);   // R
			line[col * 4 + 3] = a;
		}
	}

	// ... 后段（AlphaBlend + 资源释放）与既有 BlendAlphaSolid 完全相同 ...
}
```

**与既有实现的差异（逐条）**：

| # | 既有 | 本设计 |
|---|---|---|
| 1 | 预乘色分量在循环外算好（`b/g/r`），循环内直接写 | 循环外算 `ba/ga/ra`，循环内**再乘 `cov`** |
| 2 | 角内用 `(dx²+dy²) <= radiusSq` 二值判定 | 角内取 `mask.At(...) / 255.0f` 作为覆盖度 |
| 3 | `alphaByte` 全图常量 | `a = ToByte(color.a * cov)` 逐像素 |
| 4 | `a == 0` 提前 return（`alphaByte == 0`） | **保留**（`color.a == 0` 时整图 alpha 0 → no-op 语义不变，属 B9） |
| 5 | — | 新增 `maskUsable` 防御分支（内部不变量违反时退化为硬边，**不越界读**） |

**性能定稿**：DIB 尺寸、像素循环次数、`AlphaBlend` 调用次数**与现状完全相同** → **零回归**。新增的每像素成本 = 1 次掩码查表 + 2 次乘法。

> **§1.1 待定项 8/9 的落定**：`a < 1` 的全矩形 DIB **不迁移**到复用缓冲（保持零回归）；**不采用** 1×1 源拉伸 `AlphaBlend` 服务带状填充（需先验证跨版本可靠性，记录为潜在优化）。

### 5.5 索引变换定稿

**规则**（`MaskIndexX/Y`，§2 已定稿公式）：

```text
补丁局部 (localI, localJ) → 掩码坐标
    maskI = (右侧角 ? R-1-localI : localI)
    maskJ = (下方角 ? R-1-localJ : localJ)
```

**几何依据（供实现者理解，不是实现手段）**：四个角的边界几何都是同一个四分之一圆盘（初设 §3.2）；canonical 掩码描述的是「圆心在掩码右下角」的那个象限，因此右侧角需水平反向、下方角需垂直反向。

**验证断言**（L1-e）：对四角分别遍历 `localI/localJ ∈ [0, R)`，检查 `MaskIndexX/Y` 返回值始终落在 `[0, R)`，且 TopRight 的结果等于 TopLeft 的逐行反转。

### 5.6 `PatchSurface` 生命周期定稿

```cpp
bool GDIBackend::PatchSurface::Ensure(HDC reference, int requiredSize)
{
	if (requiredSize <= 0 || reference == nullptr)
	{
		return false;
	}

	if (dc != nullptr && size >= requiredSize)
	{
		return true;   // 已够大（**只增不减**——初设 §6.2）
	}

	// 先建后替（与 EnsureBackBuffer 决策 38 同款：新资源就绪前不动旧资源）
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = requiredSize;
	bmi.bmiHeader.biHeight      = -requiredSize;   // 负 = 顶降（行序自上而下）
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC newDC = CreateCompatibleDC(reference);
	if (!newDC)
	{
		return false;
	}

	void* newBits = nullptr;
	HBITMAP newBitmap = CreateDIBSection(newDC, &bmi, DIB_RGB_COLORS, &newBits, nullptr, 0);
	if (!newBitmap || !newBits)
	{
		if (newBitmap) DeleteObject(newBitmap);
		DeleteDC(newDC);
		return false;
	}

	HBITMAP oldInNewDC = static_cast<HBITMAP>(SelectObject(newDC, newBitmap));

	Release();   // 释放旧资源（严格逆序）

	dc = newDC;
	bitmap = newBitmap;
	oldBitmap = oldInNewDC;
	bits = newBits;
	size = requiredSize;

	return true;
}

void GDIBackend::PatchSurface::Release()
{
	// 决策 20/31 严格逆序：不能删除仍被选中的对象
	if (dc)
	{
		SelectObject(dc, oldBitmap);
	}
	if (bitmap)
	{
		DeleteObject(bitmap);
	}
	if (dc)
	{
		DeleteDC(dc);
	}

	dc = nullptr;
	bitmap = nullptr;
	oldBitmap = nullptr;
	bits = nullptr;
	size = 0;
}
```

**定稿要点**：

| 项 | 值 | 理由 |
|---|---|---|
| 尺寸 | **正方形 `size × size`**（= 见过的最大 R） | 补丁恒为 R×R，无需宽高两条独立增长路径 |
| **有效区域** | **仅 `[0, R) × [0, R)`**（`R` = 本次调用的半径）；`size − R` 范围内的历史像素**无语义要求**——`FillPatchFromMask` 只写 `R×R`，`BlendPatch` 只 `AlphaBlend` `R×R`，故旧数据**不参与**本次合成。**调用方不得读取或混合该区域** | v1.1 补充（评审 §6）：避免后来者看到「没有清空 DIB」而误判为 bug |
| **行宽（stride）** | ⚠️ **`FillPatchFromMask` 内必须用 `m_patchSurface.size * 4`** 定位行，**不能用 `R * 4`** | **v1.3 修正（实测 bug）**：「有效区域是 `R×R`」说的是**写多少列**，而**行与行之间的跨距**由 **DIB 实际宽度**决定。`size >= R` 恒成立 ⇒ 用 `R*4` 会整体行错位。详见 §5.6 代码注释与 §10 v1.3 |
| 增长 | **只增不减**（`size >= required` 即复用） | GUI 半径集合稳定，反复重建无意义（初设 §6.2） |
| 重建策略 | 先建后替 | 与决策 38 同款；失败时旧资源不受影响 |
| 释放时机 | ① `GDIBackend` 析构 ② 重建前 | `Release()` 幂等（全部空指针检查） |
| 格式 | 32bpp 顶降**预乘** BGRA | 与 §5.3 写入格式、`AlphaBlend(AC_SRC_ALPHA)` 一致 |
| 重入 | ⚠️ **不保证并发/重入安全** | 文档明确声明（初设 §6.2） |

**`FillPatchFromMask` / `BlendPatch` 定稿**：

```cpp
void GDIBackend::FillPatchFromMask(const CornerCoverageMask& mask, const Color& color,
                                   CornerId corner)
{
	const int R = mask.radius;
	// ⚠️ v1.3 修正：行宽必须用 **DIB 实际行宽** = `m_patchSurface.size * 4`，**不是** `R * 4`。
	//    `PatchSurface` 只增不减（`Ensure` 仅在 `requiredSize > size` 时重建）⇒ `size >= R` 恒成立。
	//    若按 R 定位行，则「同一后端先画过大半径、再画小半径」时本节整体行错位，
	//    补丁内容被垂直压缩 → 圆角渲染错乱。每行只写前 R 个像素（`R*4` 字节），
	//    正好落在 `[0,R)×[0,R)` 有效区内。
	const int stride = m_patchSurface.size * 4;
	BYTE* dst = static_cast<BYTE*>(m_patchSurface.bits);

	// 颜色分量（0~255）在循环外算好；每像素只再做一次 × c / 255
	const int cb = static_cast<int>(ToByte(color.b));
	const int cg = static_cast<int>(ToByte(color.g));
	const int cr = static_cast<int>(ToByte(color.r));

	for (int j = 0; j < R; ++j)
	{
		const int my = MaskIndexY(j, R, corner);
		BYTE* line = dst + static_cast<std::size_t>(j) * stride;

		for (int i = 0; i < R; ++i)
		{
			const std::uint8_t c = mask.At(MaskIndexX(i, R, corner), my);

			// 预乘（约束 1）：color.a == 1 → A = c，RGB = colorByte × c（四舍五入）
			line[i * 4 + 0] = static_cast<BYTE>((cb * c + 127) / 255);
			line[i * 4 + 1] = static_cast<BYTE>((cg * c + 127) / 255);
			line[i * 4 + 2] = static_cast<BYTE>((cr * c + 127) / 255);
			line[i * 4 + 3] = c;
		}
	}
}

void GDIBackend::BlendPatch(HDC target, int x, int y, int size)
{
	BLENDFUNCTION blend{};
	blend.BlendOp             = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat         = AC_SRC_ALPHA;   // 源为预乘 BGRA

	AlphaBlend(target, x, y, size, size,
	           m_patchSurface.dc, 0, 0, size, size, blend);
}
```

> **预乘合规检查**：`A = c`、`RGB = round(colorByte × c / 255) ≤ c`（因 `colorByte ≤ 255`）→ **RGB ≤ A 恒成立**（预乘不变量）✓。四舍五入用 `(v*c + 127)/255`（等价 `round(v*c/255)`）。

### 5.7 AA 开关定稿

| 项 | 定稿 |
|---|---|
| 形态 | `GDIBackend::SetAntiAliasing(bool)` / `IsAntiAliasingEnabled()` —— **公有方法** |
| 是否属公共 API | **否**——`GDIBackend.h` 在 `ECDI/src/Render/`（Internal）；`include/ECDI` **零改动** |
| 默认值 | `true`（**默认开启**——AA 是能力层补完，不是可选实验） |
| 测试访问 | L2 测试 `#include "Render/GDIBackend.h"` 后直接调用（既有 `RendererTests.cpp` 已同款 include） |
| 生产代码 | 无需调用（框架内部不切换） |
| 替代方案（已否决） | `friend class`（污染类边界）/ `#ifdef` 分支（测试与生产代码分叉）——均不如「Internal 头内的公有方法」干净 |

---

## 6. 测试规格定稿

**文件**：`ECDI/src/Tests/AntiAliasingTests.cpp`（新建）+ `RunAllTests.h/.cpp` 登记

**include 策略**（对齐 `RendererTests.cpp` 惯例）：

```cpp
#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 规范 10：含 Windows.h 后必须防护（防 DrawTextW 宏污染 ECDI 头）
#endif

#include "Render/CornerCoverageMask.h"          // L1：Internal 头（纯几何）
#include "Render/GDIBackend.h"                  // L2：AA 开关 + 真实渲染
#include "Platform/Win32/Win32RenderContext.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"

#include <cmath>
```

### 6.1 L1 用例（零窗口依赖）

| # | 调用 | 断言 | 容差 / 期望 |
|---|---|---|---|
| **L1-a-1** | `GenerateCornerMask(1, 8)` | `abs(Σ(c/255) − π/4) <= tol(8,1)`；**且 `At(0,0) == 207`（硬断言）** | `tol = 0.5·√1/8 = 0.0625`；`207` 见 §3.4a（v1.1 修正） |
| **L1-a-8** | `GenerateCornerMask(8, 8)` | `abs(Σ(c/255) − π·64/4) <= tol(8,8)` | `tol = 0.5·√8/8 = 0.1768` |
| **L1-a-32** | `GenerateCornerMask(32, 8)` | `abs(Σ(c/255) − π·1024/4) <= tol(8,32)` | `tol = 0.5·√32/8 = 0.3536` |
| **L1-a-S16** | `GenerateCornerMask(8, 16)` | 同上（`R = 8`） | `tol = 0.5·√8/16 = 0.0884`（**验证容差随 S 变化**——初设 §8.1 要求） |
| **L1-a-锚点** | `GenerateCornerMask(R, 8)`，`R = 1, 2, 3, 4` | **掩码字节逐元素 `==` §3.4b 锚点表**（**精确相等，无容差**） | 抓采样坐标 / 舍入 / 判定边界三类细节错误；因运算精确（§3.4c）故跨平台稳定 |
| **L1-b** | `GenerateCornerMask(R, 8)`，`R = 2, 4, 8, 16` | **存在 `c == 255`**；另对 `R = 4, 8, 16` 断言**存在 `c == 0`** | ⚠️ **v1.2 修正**：全外像素（`c == 0`）自 **`R >= 4`** 起才存在——`R = 2 / 3` 时最外角点像素仍有部分覆盖（实测 `R=2` 最小值 84、`R=3` 最小值 4），原「`R >= 2` 必同时含全内与全外」不成立 |
| **L1-c** | `GenerateCornerMask(8, 8)` | `∃ 0 < c < 255` | 硬边掩码只有 0/255 → 此条必失败（**AA 生效的判据**） |
| **L1-d** | `GenerateCornerMask(8, 8)` | 固定 `j`：`c(i,j)` 随 `i` 增大**非递减**；固定 `i`：`c(i,j)` 随 `j` 增大**非递减** | 两轴分别循环验证（初设 §8.1 机械规则） |
| **L1-e** | `MaskIndexX/Y` 四角 × `local ∈ [0,R)` | 返回值恒在 `[0, R)`；`TopRight` 的 `maskI` == `R-1-localI`；`BottomLeft` 的 `maskJ` == `R-1-localJ` | 索引变换正确性 |
| **L1-f** | `GenerateCornerMask(8,8)` 调两次 | 两次 `coverage` **逐元素相同** | 规则网格采样 → 无 RNG（确定性） |
| **L1-g** | `cache.Get(8)` 两次 | **返回同一地址**（`&a == &b`）且 `Size()` 不增 | 缓存命中 |
| **L1-h** | `Get(8)` → `SetSamples(16)` | `Size()==1` → `SetSamples(16)` 后 `Size()==0`；`GetSamples()==16` | `SetSamples` 清空缓存（§4） |
| **L1-i** | `GenerateCornerMask(0,8)` / `(-1,8)` / `(8,0)` / `(0,0)` | 全部 `Empty() == true` | 非法入参 → 空掩码，不抛异常 |

**多半径覆盖**：`R = 1, 2, 3, 4, 8, 16, 32`（含小半径——离散化误差高危区，初设 §3.4）。

### 6.2 L2 用例（真窗口像素读回）

**窗口规格**（沿用 `RendererTests.cpp::TestGDIBackendAlphaBlend` 的既有模式）：

```cpp
// 屏幕右下角短暂显示（屏幕外窗口 DC 裁剪区为空 → GetPixel 恒返 CLR_INVALID，原测试已注释该坑）
// SW_SHOWNOACTIVATE 不抢焦点；测试后立即 DestroyWindow
WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
wc.lpszClassName = L"ECDI_TestAA";     // 每个用例用不同类名（与既有测试同款，避免类已注册冲突）
RegisterClassW(&wc);                    // 失败时断言 ERROR_CLASS_ALREADY_EXISTS（重复运行）
HWND hwnd = CreateWindowExW(0, L"ECDI_TestAA", L"ECDI_AATest", WS_POPUP,
                            screenW - 210, screenH - 210, 200, 200, nullptr, nullptr, wc.hInstance, nullptr);
ShowWindow(hwnd, SW_SHOWNOACTIVATE);
```

统一绘制基线：`DrawRect(Rect{0,0,200,200}, Color::Blue())` 铺底，再画被测圆角矩形。圆角矩形**位置固定为 `Rect{50,50,100,100}`**（R=8 时：中带 `y∈[58,142]`、上带 `y∈[50,58] x∈[58,142]`；四角补丁 `8×8`）。

| # | 绘制 | 读取点 | 断言 |
|---|---|---|---|
| **L2-a** | ① `AA 关`：R=8 红色圆角矩形 ② 清底重画 `AA 开`：同参数 | 左上角补丁区 `x∈[50,58), y∈[50,58)` 全部 64 点 | 两帧在该区**存在像素差异**（证明 AA 生效） |
| **L2-b** | `AA 开`，同上 | 同角区 64 点 | **存在**像素同时 ≠ 纯红(`RGB(255,0,0)`) 且 ≠ 纯蓝(`RGB(0,0,255)`) ——过渡像素 |
| **L2-c** | `R = 0`：① `AA 关` 画一次 ② `AA 开` 画一次（`DrawRoundedRect(Rect{50,50,100,100}, 0.0f, Red)`） | 全窗 `200×200` | **逐像素完全一致**（`R=0` 时 AA 路径根本不启用）——**不引入 Golden Image** |
| **L2-d** | `AA 开` + `DrawRoundedRect(Rect{50,50,100,100}, 8.0f, Color::FromRGBA8(255,0,0,128))` | **带内点** `(100, 60)`（`y=60 ∈ [58,142]` → 中带，**覆盖度恒 1**） | ≈ `RGB(128, 0, 127)`，各通道 `±3`（沿用既有 `TestGDIBackendAlphaBlend` 的期望算法） |
| **L2-e-1** | `AA 开` + 圆角矩形 `Rect{50,50,100,100}, R=8` | 包围盒角点 `(51,51)` | == 背景蓝（角被切除） |
| **L2-e-2** | `AA 开` + 胶囊 `Rect{50,50,100,20}, R=10`（`h == 2R` → 中带零高跳过，§初设 4.3） | 角点 `(51,51)`；左端中点 `(51,60)` | `(51,51)` == 背景蓝（在左半圆外）；`(51,60)` == 红（在左半圆内） |
| **L2-e-3** | `AA 开` + 真圆 `Rect{50,50,20,20}, R=10`（`w == h == 2R` → 三条带全跳过，四象限拼成整圆） | 包围盒角点 `(51,51)`；圆心 `(60,60)` | `(51,51)` == 背景蓝；`(60,60)` == 红 |

> **L2-d 的 ±3 说明（初设 §8.2 已定）**：它验证的是 **`coverage × color.a → 预乘 BGRA → AlphaBlend` 的合成链路**，**不是 coverage 掩码精度**——故刻意取**带内点**（覆盖度恒 1）以隔离变量。掩码精度由 L1 覆盖。

> **L2-a 的「清底重画」注意点（v1.1 补充——评审 §7）**：两次绘制之间必须**先用不透明背景色铺满整个窗口**（`DrawRect(Rect{0,0,200,200}, Blue)` 即使底 `BeginFrame` 清屏已发生，也要显式重铺一次），确保第一帧在角区留下的**部分覆盖度像素**被完全覆盖，否则第二帧读到的是两帧叠加的结果，"差异"会失去意义。**这是测试实现细节，不是设计约束**——故不加断言，只在此注明。

> **不增加更多像素断言（评审 §7 明确同意）**：`L2-a`（开关改变结果）+ `L2-b`（出现过渡像素）两条已足以证明「AA 生效」；**不引入 Golden Image**（初设 D9）。

### 6.3 测试注册

`RunAllTests.h` 追加声明：

```cpp
void RegisterAntiAliasingTests();
```

`RunAllTests.cpp` 的 `RunAllTests()` 中追加调用（置于 `RegisterRendererTests();` 之后）：

```cpp
	RegisterAntiAliasingTests();
```

注册函数体（`AntiAliasingTests.cpp` 末尾）：

```cpp
void ECDI::Test::RegisterAntiAliasingTests()
{
    GetTestRegistry().Add("AntiAliasing.MaskAreaConservation",   &TestMaskAreaConservation);
    GetTestRegistry().Add("AntiAliasing.MaskExactAnchors",       &TestMaskExactAnchors);
    GetTestRegistry().Add("AntiAliasing.MaskValueRange",         &TestMaskValueRange);
    GetTestRegistry().Add("AntiAliasing.MaskPartialCoverage",    &TestMaskPartialCoverage);
    GetTestRegistry().Add("AntiAliasing.MaskMonotonic",          &TestMaskMonotonic);
    GetTestRegistry().Add("AntiAliasing.MaskIndexTransform",     &TestMaskIndexTransform);
    GetTestRegistry().Add("AntiAliasing.MaskDeterminism",        &TestMaskDeterminism);
    GetTestRegistry().Add("AntiAliasing.CacheHit",               &TestCacheHit);
    GetTestRegistry().Add("AntiAliasing.CacheSetSamplesClears",  &TestCacheSetSamplesClears);
    GetTestRegistry().Add("AntiAliasing.MaskInvalidArgs",        &TestMaskInvalidArgs);
    GetTestRegistry().Add("AntiAliasing.GDIAAOnOffDiffers",      &TestGDIAAOnOffDiffers);
    GetTestRegistry().Add("AntiAliasing.GDITransitionPixels",    &TestGDITransitionPixels);
    GetTestRegistry().Add("AntiAliasing.GDIRadiusZeroBitwise",   &TestGDIRadiusZeroBitwise);
    GetTestRegistry().Add("AntiAliasing.GDIPremultipliedBlend",  &TestGDIPremultipliedBlend);
    GetTestRegistry().Add("AntiAliasing.GDIShapeComposition",    &TestGDIShapeComposition);
}
```

（**15 条用例：L1 十条 + L2 五条**——L1-a 的多半径/S16 与 L2-e 的三形状在各自函数内循环覆盖。v1.1 新增 `MaskExactAnchors`——把 §3.4b 的精确字节锚点独立成用例，以获得独立的失败粒度：**容差式（面积）与精确式（锚点）失败时指向的缺陷类型不同**。）

### 6.4 `tol(S, R)` 定稿（v1.1 重写——改用参考实现实测数据）

**v1.0 的两处问题（评审 §3）**：

1. v1.0 写 `tol = R/S`（自称「保守上界」），推导是「边界像素数 × 单像素误差上界」。该推导**不严格**——它假设所有边界像素的量化误差**同号累加**，而实际沿弧扫描时误差符号交替、相互抵消。
2. **实测证明它既不保守、又过度宽松**（见下表）——v1.0 的 `R=1` 那行所依据的实际误差 0.0734 本身就是错的（正确值 0.0271，§3.4a）。

#### 6.4a 参考实现实测扫描（`S ∈ {4, 8, 16, 32}`，`R ∈ [1, 256]`）

| `S` | 实测最大误差（`R ≤ 64`） | 出现在 `R` | 实测最大误差（`R ≤ 256`） | v1.0 的 `R/S`（同点对比） |
|---|---|---|---|---|
| 4 | 0.551 | 55 | — | 55/4 = 13.75（**25× 过宽**） |
| 8 | 0.278 | 62 | 0.470（`R = 192`） | 192/8 = 24.0（**51× 过宽**） |
| 16 | 0.068 | 31 | 0.100（`R = 96`） | 96/16 = 6.0（**60× 过宽**） |
| 32 | 0.051 | 62 | — | — |

**两个结论**：

| # | 结论 |
|---|---|
| 1 | **误差不随 `R` 线性增长**——`R/S` 在大半径下松弛约 **2.5 个数量级**（`R=32, S=8`：tol 4.0 vs 实测 0.015 → 267× 余量）→ 作为测试保护**过于宽松，会掩盖真实缺陷** |
| 2 | **误差近似 ∝ `√R / S`**（符号交替抵消 → 随机游走量级）。拟合常数 `k = err·S/√R` 在全部扫描点上落在 **0.16 ~ 0.30** |

#### 6.4b 定稿公式

```text
tol(S, R) = 0.5 · sqrt(R) / S          （单位 = 像素面积；与 L1-a 左侧同域）
```

取 `0.5` 相对于拟合上界 `k = 0.30` 提供 **≥ 1.6× 余量**。

**余量校验表**：

| 点 | 实测误差 | `tol = 0.5·√R/S` | 余量 |
|---|---|---|---|
| `R=1, S=8` | 0.0264 | 0.0625 | 2.4× |
| `R=8, S=8` | 0.0875 | 0.1768 | 2.0× |
| `R=62, S=8` | 0.278 | 0.492 | 1.8× |
| `R=192, S=8` | 0.470 | 0.866 | 1.8× |
| `R=8, S=16` | 0.0106 | 0.0884 | 8.3× |
| `R=96, S=16` | 0.0999 | 0.306 | 3.1× |

#### 6.4c 表述纪律（评审 §3 的建议已采纳）

`tol` 是**「当前采样方案下的工程容差上限（测试保护）」**，**不是**对 midpoint sampling 全局误差的形式化数学证明。其依据是**参考实现在上述范围内的实测扫描**；本阶段实际使用的半径远小于扫描范围。

#### 6.4d 失败判据与扩展规则（三步，实现阶段执行）

```text
① L1-a 用例【必须打印】：Σ(c/255)、πR²/4、实际误差
② 实测误差 > tol(S,R) → 判定为【生成器缺陷】，核查采样坐标 / 舍入规则 / 判定边界
                          —— **不得放宽容差**
③ 若测试半径扩展到 R > 256（超出本次扫描范围）→ **重新运行扫描并更新 §6.4a/b**，
   不得沿用公式外推而不验证（v1.0 的失效正是「用一个未验证的公式外推」）
```

> ⚠️ 第 ② 条是本节的关键纪律：**用放宽容差来「修好」测试是本阶段最需要避免的失败模式**（§3.4a 的手算错误也印证了这一点——错误只有在有独立复算时才暴露）。
>
> 💡 **`tol` 只服务于 L1-a（面积守恒）**；实现细节类的错误由 **§6.1 的 L1-a-锚点（精确相等）** 负责捕获。二者分工：**容差式断言抓几何整体，精确锚点抓实现细节**。

---

## 7. CMake / vcxproj 规格

### 7.1 CMake：**零改动**（已核实）

| glob | 覆盖 | 结论 |
|---|---|---|
| `FRAMEWORK_SOURCES` = `GLOB_RECURSE ECDI/src/*.cpp` − `/Demo/\|/Tests/` | `src/Render/CornerCoverageMask.cpp` 命中 → **自动进库** | ✅ |
| `TEST_SOURCES` = `GLOB_RECURSE ECDI/src/Tests/*.cpp` | `src/Tests/AntiAliasingTests.cpp` 命中 → **自动进测试 exe** | ✅ |
| `PUBLIC_HEADERS` = `ECDI/include/ECDI/*.h` | 本阶段不新增公共头 → 自包含测试不变 | ✅ |

两者均带 `CONFIGURE_DEPENDS` → 新增文件后 CMake 自动重新 configure（CLion / VS 会提示 reload）。

### 7.2 `ECDI/ECDI.vcxproj`（辅助工程，4 处）

按现有条目格式追加（对齐 `src\Render\...` / `src\Tests\...` 的既有写法）：

```xml
<!-- ClCompile 段（与 src\Render\GDIBackend.cpp 同组） -->
<ClCompile Include="src\Render\CornerCoverageMask.cpp" />
<!-- ClCompile 段（与 src\Tests\RendererTests.cpp 同组） -->
<ClCompile Include="src\Tests\AntiAliasingTests.cpp" />

<!-- ClInclude 段（与 src\Render\GDIBackend.h 同组） -->
<ClInclude Include="src\Render\CornerCoverageMask.h" />
```

`RunAllTests.h` / `.cpp` 已在工程内（仅内容改动，无需新增条目）。

---

## 8. 验收清单

| # | 项 | 判据 |
|---|---|---|
| A1 | **公共头零改动** | `git diff --stat ECDI/include` **为空**；Public 头数量仍为 **81** |
| A2 | **既有测试零回归** | 既有 158 条全绿（**AA 默认开启**，所有既有渲染测试在 AA 开启下通过） |
| A3 | **`R == 0` 逐位一致** | L2-c 通过 |
| A4 | **AA 关闭逐位一致** | L2 的关/开对照可证；另对 `R > 0` 人工抽查一次（AA 关 vs 改造前截图对比，非自动化） |
| A5 | L1 全部通过 | **十条用例**；且打印实测误差 |
| A6 | L2 全部通过 | 五条用例（含三形状） |
| A7 | **性能基线有记录** | 初设 §8.4 Baseline / Case A / Case B / Case C 有实测数字；结论写入本文档修订记录 |
| A8 | 四工具链编译通过 | MSVC / Clang / ClangCL / MinGW |
| A9 | 目视 | `Radio`（真圆）/ `ProgressBar`（胶囊）/ 圆角 `Panel` 边缘无阶梯；`S=8` vs `S=16` 对比确认默认值 |
| A10 | 文档回写 | ① `phase9.5-alpha-primitive-detailed-design.md` 的**约束 2 加修订标注**（需求 D1，实现阶段执行）② 本文档修订记录补性能实测结论 ③ `roadmap-deferred.md` #29 状态 ④ `docs/README.md` 索引 ⑤ `.workbuddy/memory` 日志 |
| A11 | 无新依赖 | `CMakeLists.txt` 链接库零改动 |

---

## 9. 实施顺序（10 步，含每步的可验证中间态）

| 步 | 动作 | 中间态验证 |
|---|---|---|
| 1 | 新建 `CornerCoverageMask.h` / `.cpp`（§2–§4） | 编译通过 |
| 2 | L1 用例（§6.1 **前十条**，含精确锚点）+ `RunAllTests` 登记 | **L1 全绿**（此时尚未碰渲染——数学正确性先立） |
| 3 | `GDIBackend.h` 追加成员/辅助声明 + 析构调用 `Release()` | 编译通过（空实现） |
| 4 | **`DrawRoundedRectLegacy` 搬移**（纯重构，零行为变更） | **既有 158 条全绿** |
| 5 | `BlendAlphaSolid` → `DrawRoundedRectBlendedAA`（§5.4，约 5 行改动） | 既有测试全绿（`a < 1` 用例无回归） |
| 6 | `PatchSurface::Ensure/Release`（§5.6）+ `a == 1` 新路径（§5.3） | 既有测试全绿 + L2-a/b 可跑 |
| 7 | L2 五条用例（§6.2） | **L1 + L2 全绿** |
| 8 | 性能基线实测（初设 §8.4） | 得出 Baseline / A / B / C 数字；**若不可接受 → 回到本文档修订**（不静默改方案） |
| 9 | 目视 V1–V3（含 `S=8` vs `S=16`） | 确认 `S` 默认值；无阶梯 |
| 10 | 文档回写（§8 A10）+ 用户四工具链编译 | 验收清单 A1–A11 全项 |

**顺序理由**：

- **第 2 步把数学验证前置**——第 4–6 步才动渲染，此时若渲染结果异常，可直接排除掩码本身的问题（初设 §12 的「先数学后渲染」）
- **第 4 步是纯重构**，是唯一「可以证明零行为变更」的一步——把 legacy 搬移独立出来，后续任何回归都能二分定位
- **第 5 步风险最低**（改动最小、结构不变）；**第 6 步是唯一的全新实现**，风险集中于此，而前 5 步已经铺好验证网
- 第 8 步的「回到本文档修订」与初设 §7 B10 同一条纪律

---

## 10. 修订记录

- v1.4（2026-09-11）**修复 `DrawFocusRect` 四角弧心 1px 偏移**（即 v1.3 记录的「附带发现」，用户确认后处理）：
  - **根因**：`DrawFocusRect` 为避开 `PushClip` 把右 / 下边界内缩 1px（`fRight = lround(x+w) - 1`、`fBottom = lround(y+h) - 1`），但**四个圆角弧心也一并用了这个内缩边界** ⇒ **右上 / 左下 / 右下三角的弧心各内移 1px**（左上角因 `fLeft` / `fTop` 本就不内缩而恰好对齐）→ 与背景 `DrawRoundedRect` 的弧错位，在**圆角与直线交接处**表现为焦点框「内收」。该偏移**不对称**（仅三角）正是它的特征。
  - **修复**（`GDIBackend::DrawFocusRect` 三处）：
    1. 引入**几何边界** `gRight = lround(x + w)` / `gBottom = lround(y + h)`——**半径与四角弧心一律基于几何边界**（与 `DrawRoundedRect` / `DrawRect` 的形状语义一致）；
    2. **直线端点仍用可见像素边界** `fRight` / `fBottom`（避开 clip：`right`/`bottom` 列落在开区间外）；
    3. 沿周界的采样坐标（`PointAt`）**clamp 到 `[fLeft, fRight] × [fTop, fBottom]`**——因为弧端点在几何边界上（x 可达 `gRight`），**无 `PushClip` 时可能越界 1px 绘制**。
  - **实证**：四角「焦点框最外像素 vs 背景弧理论值」**最大偏差 1.34px**（1px 线斜向栅格化的固有容差内），四角全部对齐；右上角逐行对比——修复前 `y=54` 焦点框为 `148`（理论 149.19，差 **1.19**）→ **修复后 `149`（差 0.19）**。**回归测试 173/173 维持**。
  - **影响面**：仅 `GDIBackend::DrawFocusRect`（Button 与 TextBox 的焦点框同构受益）；`RenderCommand` / `RecordingBackend` / 命令层语义**零改动**。
  - **后续追加（2026-09-12）**：同批次为 ModelProbe 二次查询缺陷补了回归用例 `ModelProbePage.FetchReplacesPrevious`（+1）⇒ 全量用例数由 **173 → 174**，`ecdi_tests` **174/174 通过**。该用例与 AA 实现无耦合，仅共享同一测试二进制。
- v1.3（2026-09-11）**缺陷修复：`PatchSurface` 行宽（stride）误用**：
  - **症状**（用户报告）：运行 ModelProbe 时，**鼠标经过后圆角出现渲染错误**。
  - **根因**：`GDIBackend::FillPatchFromMask` 用 `stride = R * 4`（`R` = 本次调用的 effective 半径）定位 DIB 行，而 `PatchSurface` 的 **DIB 实际行宽是 `size * 4`**（`size` = 只增不减的最大半径）。因 `Ensure` 仅在 `requiredSize > size` 时重建，故 **`size >= R` 恒成立**——一旦同一后端画过更大的半径，后续小半径的补丁即整体**行错位**，内容被垂直压缩（每行的写入实际落进 DIB 上一行的后半）。
  - **触发条件**：同一 `GDIBackend` 实例内「**先画大半径圆角矩形、再画小半径圆角矩形**」。ModelProbe 同时使用 `kRadius = 6` 与 `radius = 8.0f` 的控件，hover 重绘走到该失效路径 → 表现为「鼠标经过后圆角渲染错误」。
  - **为什么 173 条测试全绿却漏掉**：L2 五条用例**每条都用独立 backend + 单一半径**，此时 `size == R`，恰好走正确分支。**这是测试设计的盲区**——未覆盖「同一 backend 跨半径复用 `PatchSurface`」。**建议补一条此类用例**（本次未加，待定）。
  - **实证**（对照实验）：场景 A = 干净 backend 画 `R=8`；场景 B = 同帧先画 `R=16` 再画同参数 `R=8`。**修复前 40/289 像素不一致**（差异集中在圆角弧段）**→ 修复后 0/289**；回归测试维持 **173/173 通过**。
  - **修复**：`stride` 改为 `m_patchSurface.size * 4`（§5.6 代码与「行宽（stride）」定稿要点行已同步）。**注意语义区分**：「有效区域 = `R×R`」说的是**写多少列**，而**行间跨距**由 DIB 实际宽度决定——两者不可混为一谈，这正是本缺陷的认识根源（§5.6 的 v1.1 行原本只声明了前者）。
  - **本缺陷属「详设 + 实现」共同引入**：§5.6 的示例代码原文即为 `stride = R * 4`，实现照此落地。**这是详设的第 4 处错误**（前三处：`Logger` 宽字面量 / L1-b 边界 / §5.3 冗余 `SelectObject`）。
  - **附带发现（本次未修，独立记账）**：`DrawFocusRect` 为避开 `PushClip` 把右/下边界内缩 1px 时，**连圆角弧心也一起内缩** ⇒ 左上角与背景对齐，而**右上 / 左下 / 右下三角的弧心比背景各偏移 1px**。该行为自 Phase 8.5 R4 起即存在（Phase 8.6 未改动 `DrawFocusRect`），背景 AA 后边界更精确而更易察觉。**与本次缺陷无关。**
- v1.2（2026-09-11）**实现落地**：按 §9 的 10 步顺序完成编码（第 1–7 步），代码改动 **8 个文件 = 3 新建**（`src/Render/CornerCoverageMask.h` / `.cpp`、`src/Tests/AntiAliasingTests.cpp`）**+ 5 修改**（`src/Render/GDIBackend.h` / `.cpp`、`src/Tests/RunAllTests.h` / `.cpp`、`ECDI/ECDI.vcxproj` 三处条目）；CMake **零改动**（已核实 §7.1 的两个 glob 自动吸纳）。**验证结果：`ecdi_tests` 173/173 通过**（158 既有**零回归** + 15 新增），验证构建 = MinGW g++ 16.1（`-G "MinGW Makefiles"`，项目支持的四工具链之一）。**A1 公共头零改动已静态确认**（`git status ECDI/include` 为空，Public 头仍为 81）。
  - **L1-a 实测误差已打印并与 §6.4b 余量校验表逐点吻合**（`S=8`：`R=1` → 0.026367 / `tol` 0.0625；`R=8` → 0.087459 / 0.176777；`R=32` → 0.015026 / 0.353553；`S=16`：`R=8` → 0.010580 / 0.088388）——**`tol` 公式经实现侧独立验证**，未发生「实测超容差」情形（即 §6.4d 第 ② 条纪律未被触发）。
  - **实施中发现并修正的详设三处错误**：
    - **① §4 / §5.2 的 `Logger::Log` 调用示例是窄字符串字面量**（编译期实证为错误）：`Logger::Log` 第二参是 `std::wstring_view`（见 `Core/Logger.h` 与既有调用惯例 `WicImageDecoder.cpp:209` 的 `L"..."`），详设示例传 `"..."` 会**编译失败**。**已改为宽字面量 `L"..."`**（详见 §4 / §5.2 正文同步修正）。这是本轮唯一一处「照抄详设即编译不过」的错误。
    - **② §6.1 的 L1-b 断言边界有误**：原文「`R = 2, 4, 8, 16` 均存在 `c == 255` **且** `c == 0`」——实测 `c == 0`（全外像素）**自 `R >= 4` 起才出现**（`R = 2` 最小值 84、`R = 3` 最小值 4，最外角点像素仍有部分覆盖）。已按实测修正为「`R >= 2` 断言 `c == 255`；`R >= 4` 断言 `c == 0`」（§6.1 正文已同步）。
    - **③ §5.3 的 `SelectObject` / `oldBrush` 是冗余操作**：`FillRect` 直接接受 `HBRUSH` 参数，无需选入 DC（既有 `DrawRect` 正是此惯例）。实现省略该对操作——不改变结果，且避免引入「选入/恢复」这对易错的 GDI 状态操作。
  - **一处实现层的设计收敛（偏离 §5.4 的字面描述，语义等价）**：§5.4 的写法暗示在 `DrawRoundedRectBlendedAA` 中**复制** `BlendAlphaSolid` 的 DIB 生命周期骨架（约 50 行）。实现改为**保留 `BlendAlphaSolid` 作为共用骨架**——签名追加 `const CornerCoverageMask* mask`（`nullptr` = 矩形/二值降级），`DrawRoundedRectBlendedAA` 成为「`maskUsable` 判定 + 转发」的薄包装。**语义与 §5.4 完全一致**（含 `maskUsable` 防御分支），且顺带**落实需求 R3「两条圆角路径的几何统一」**——原二值圆角分支不再独立存在，而降级为「掩码不可用」时的防御路径。**免除了 ~50 行 DIB 代码重复**。
  - **遗留（待用户执行）**：A7 性能基线实测（§9 第 8 步）/ A9 目视与 `S=8` vs `S=16` 对比（第 9 步）/ A8 其余三工具链（MSVC / Clang / ClangCL）/ A4 AA 关闭人工抽查。**A10 的其余回写项（9.5 约束 2 修订标注、`roadmap-deferred.md` #29、`docs/README.md` 索引）随本条目一并完成。**
- v1.1（2026-09-11）**外部评审 7 项全采纳（2 必改 + 3 建议 + 2 小修）**：
  - **P0-1 · 修正 §3.4 的 `R=1, S=8` 手工推导错误**（评审 §1）：v1.0 原文「圆外 9 / 圆内 55 / byte 219 / 误差 0.0734」**全部错误**。由参考实现复算得正确值——**圆外 12 / 圆内 52 / coverage 0.8125 / byte 207 / 误差 0.027102**。错因定位为计数表**三处漏项**（`u=0.9375` 行漏 `v=0.9375`；`u=0.8125` 行漏 `v=0.9375` 与 `v=0.8125`；`u=0.6875` 行漏 `v=0.8125`），并把「此类两两求和计数表**必须用参考实现复算**」写入文档作为方法论教训。**同时按评审建议把 `207` 从「注释参考」升级为硬断言**（`L1-a-1`），理由见新增的 §3.4c。
  - **新增 §3.4b 精确字节锚点表**（`R = 1..4` 的完整掩码字节与 Σ）——**精确相等断言**，用于捕获采样坐标 / 舍入规则 / 判定边界三类细节错误（面积守恒的容差式断言对这类错误不敏感）；并新增 §3.4c **浮点精确性论证**：`S` 取 2 的幂时全部中间量为分母 2 的幂的二进制有理数，`R ≤ 128`（S=8）/ `R ≤ 64`（S=16）范围内 `float` 下精确无舍入 → **字节值跨编译器/平台确定**，故精确断言成立。**由此得到「`S` 取 2 的幂」的第二个理由**（第一个是舍入恒为 round-half-up）——v1.0 只说「建议偶数」，不够强。
  - **P0-2 · §5.2 类型链显式化**（评审 §2）：核验结论——v1.0 的单行式 `static_cast<int>(std::clamp(std::lround(cornerRadius), 0L, (std::min)(w, h) / 2))` **实际可以编译**（`std::lround` → `long`、`0L` → `long`、`w`/`h` 为 `LONG` 即 `long`，三参类型一致 → `std::clamp<long>` 推导成立；且该写法已在框架既有代码中编译通过）。仍改为**显式三段式**（量化 → 上界 → 钳制 → int），理由三条：与 §4「effective 整数半径」链路逐行对应 / 消除对 `w`/`h` 恰为 `LONG` 的隐性依赖（类型变化时不会演变成 `clamp` 三参不一致）/ `maxRadius` 具名后可挂接 §2.1「钳制消除退化」的论证。
  - **P1-3 · §6.4 重写（`tol` 换成实测推导）**（评审 §3）：v1.0 的 `tol = R/S` 自称「保守上界」，其「边界像素数 × 单像素误差上界」的推导**不严格**（假设误差同号累加，忽略了沿弧扫描时的符号抵消），且**实测既不保守也过度宽松**（`R=32, S=8`：tol 4.0 vs 实测 0.015 → 267× 余量，会掩盖真实缺陷）。新增 §6.4a **参考实现实测扫描表**（`S ∈ {4,8,16,32}`、`R ∈ [1,256]`），得出误差 **∝ `√R/S`**（`k = err·S/√R ∈ [0.16, 0.30]`）；**定稿为 `tol(S,R) = 0.5·sqrt(R)/S`**（≥1.6× 余量，附 6 点余量校验表）；新增 §6.4c **表述纪律**（`tol` 是「当前采样方案下的工程容差上限（测试保护）」，**不是**形式化数学证明）与 §6.4d **扩展规则**（测试半径超过 256 必须重跑扫描，**不得公式外推**——v1.0 的失效正是「用未验证的公式外推」）。
  - **P1-4 · §5.3 把「三条带覆盖度恒为 1」的语义钉死**（评审 §4）：新增引用框 + **适用前提/失效边界表**——该结论依赖「整数像素边界」，**Phase 8 R4 浮点化落地后失效**（届时带边界落在像素方格内部，三条带也必须走覆盖度路径）；并说明「圆角用连续面积覆盖度、主体用 GDI 整数填充」不是不一致，而是「整数边界下两者覆盖度恰为 1」的两种等价表达。
  - **P1-5 · `samples` 契约明确**（评审 §5）：§2 的 API 注释、`SetSamples` 注释、§3.1 注释、§3.2 规则 6 统一为——`samples <= 0` → **no-op**；`samples > 0` → **一律接受（含奇数）**，仅**推荐 2 的幂**（附 §3.4c 的理由）。
  - **P2-6 · §5.6 新增「有效区域」行**（评审 §6）：`PatchSurface` 的有效区域仅 `[0,R)×[0,R)`，`size − R` 范围内的历史像素**无语义要求**、调用方不得读取或混合——避免后来者把「没有清空 DIB」误判为 bug。
  - **P2-7 · §6.2 补 L2-a 的「清底重画」注意点**（评审 §7）：两帧之间必须显式用不透明背景铺满整窗，确保第一帧的**部分覆盖度像素**被完全覆盖；同时明确采纳评审意见——**不再增加更多像素断言**、**不引入 Golden Image**。
  - **§1.1 措辞修订**（评审 §10）：「全部收口」→「**全部形成实施决策**」——因为 `S=8` / `tol` / AlphaBlend 合并三项保留了验证后调整机制，属「默认值/实现参数已定稿」而非永久数学契约。
  - **连带更新**：L1 新增 `MaskExactAnchors` 用例（14 → **15 条**注册）；§6.1 的 L1-a 四行 tol 按新公式重算（0.0625 / 0.1768 / 0.3536 / 0.0884）；A5 / §9 第 2 步的用例计数同步。
  - **评审明确表扬且不动的项**：`maskUsable` 防御分支（内部 bug → 降级 AA，而非越界读/崩溃）、三条路径的结构（「统一几何语义、保留各自最佳实现」）、L2 的关/开对照设计、不做 Golden Image。
- v1.0（2026-09-11）详细设计初稿：① **范围映射 + 11 项待定项全部收口**（`PatchSurface` 私有嵌套结构 / 索引变换辅助函数不预生成 4 份 / `S=8` / `tol=R/S` + 实测收紧程序 / AlphaBlend **不合并** / AA 开关 = **Internal 头内公有方法** / 诊断阈值 **32 条** / `a<1` DIB **不迁移** / 1×1 拉伸 **不采用** / brush 缓存 **不引入** / L1 断言逐用例定稿）；② **`CornerCoverageMask.h` 全文定稿**（结构 + `CornerId` + `MaskIndexX/Y` + `GenerateCornerMask` 声明 + `CornerMaskCache` 类；含「为何 `stride = radius`」「为何 `std::map`」「为何 `At` 不检查边界」的依据表）；③ **`GenerateCornerMask` 算法全文定稿** + **8 条关键规则表** + **显式不做快速路径**（附最差成本量级表，证明收益不可感知）+ **`R=1, S=8` 手工推导自检参考值 219**（含 9 个圆外样本的逐项计数表，实现自检用、不作硬断言）；④ **`CornerMaskCache` 实现定稿**（`emplace` 引用稳定性、**`SetSamples` 必须清空缓存**、阈值只记一次）；⑤ **`GDIBackend` 实施规格**（成员与 7 个新辅助函数 / 路径选择全文含 **`PatchSurface` 失败 → legacy** 的 fail-safe 降级 + 「legacy 传原始 `cornerRadius` 以保逐位一致」/ `a==1` 流程含 3 带 RECT 与 4 角坐标表 / `a<1` 逐像素公式 + **与既有实现的 5 条差异逐条对照** / 索引变换规则 + 几何依据 / `PatchSurface::Ensure/Release` 全文 + 6 项定稿要点表 / `FillPatchFromMask` + `BlendPatch` 全文含**预乘不变量检查** / AA 开关定稿含「为何不用 friend / #ifdef」）；⑥ **测试规格定稿**（include 策略 / L1 十二条断言逐条 + 多半径 / L2 七条含**精确读取坐标与期望值** + 窗口与绘制基线固定为 `Rect{50,50,100,100}` + **L2-d 刻意取带内点以隔离 coverage 变量** + 14 条注册名 / **`tol = R/S` 双点校验表** + 三步实测收紧程序含「超出上界即判定生成器缺陷，不得放宽容差」的纪律条款）；⑦ **CMake 零改动已核实**（两个 glob 的覆盖论证）+ vcxproj 4 处条目；⑧ **验收清单 A1–A11**（含 A10 的 5 项文档回写，其中 9.5 约束 2 修订标注归实现阶段）；⑨ **10 步实施顺序**（含每步可验证中间态与顺序理由：数学前置 / 第 4 步纯重构可二分定位 / 第 6 步风险集中）。待评审。
