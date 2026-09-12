#pragma once

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
/// @param radius  半径 R（见详细设计 §3 算法）
/// @param samples 超采样倍率 S。**推荐取 2 的幂（4 / 8 / 16）**：
///                (a) 舍入恒为 round-half-up；(b) 运算全程为二进制有理数 → `float` 精确
///                （见详细设计 §3.4c）。**奇数合法**，但舍入退化为 round-half-down 且失去跨平台精确性。
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
	/// **契约**：`samples <= 0` → **no-op**；`samples > 0` → **一律接受（含奇数）**，
	/// 仅**推荐**取 2 的幂（理由见详细设计 §3.4c）。
	void SetSamples(int samples);

	int GetSamples() const noexcept { return m_samples; }

	std::size_t Size() const noexcept { return m_masks.size(); }

	/// @brief 条目数诊断阈值（超出记一次 Warning——防异常半径导致缓存无界增长）
	static constexpr std::size_t kDiagnosticThreshold = 32;

private:

	std::map<int, CornerCoverageMask> m_masks;   ///< 键 = effective 整数半径
	int m_samples = 8;                           ///< 超采样倍率（默认 8，见初步设计 §2.3）
	bool m_thresholdLogged = false;              ///< 阈值告警只记一次

};

}
