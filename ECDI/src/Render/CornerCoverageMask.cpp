#include "Render/CornerCoverageMask.h"

#include "ECDI/Core/Logger.h"

#include <cstddef>

namespace ECDI {

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

					// 圆心 (R, R)；边界含圆周（<=）——与详细设计 §3.2 规则 4 一致
					if (dx * dx + dySq <= radiusSq) {

						++inside;

					}

				}

			}

			// 舍入：round(255 * inside / total)——整数实现（确定性、无浮点误差累积）
			// ⚠️ `total = S²`；S 取 2 的幂时 total 为偶数 → 该式即严格 round-half-up。
			//    S 为奇数时 total 为奇数，`total/2` 整数除截断 → 退化为 round-half-down
			//    （无害，但规则必须写死——详细设计 §3.2 规则 5 的确定性要求）
			const int byteValue = (255 * inside + total / 2) / total;

			mask.coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(R)
			              + static_cast<std::size_t>(i)]
				= static_cast<std::uint8_t>(byteValue);

		}

	}

	return mask;

}

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

		return;   // 契约：samples <= 0 → no-op；与当前值相同 → 幂等 no-op

	}

	m_samples = samples;

	m_masks.clear();          // ⚠️ 必须清空：不同 S 生成的掩码量化级别不同，不可混用
	m_thresholdLogged = false;

}

}
