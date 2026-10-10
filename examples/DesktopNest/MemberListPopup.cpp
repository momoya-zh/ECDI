#include "MemberListPopup.h"   // .cpp 对应头（第一）

#include "ECDI/Core/Point.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Widget/Label.h"   // 「加入成员」分区头（非交互行）

#include <memory>
#include <utility>   // std::move

namespace ECDI::DesktopNest {

namespace {

/// @brief 成员行文本前缀——当前成员 = "[X] "，其余 = "[ ] "（ASCII 安全，条 48）
constexpr const char* kActivePrefix = "[X] ";
constexpr const char* kInactivePrefix = "[ ] ";

/// @brief 「加入成员」分区头文本（非交互 Label 行）
constexpr const char* kJoinHeaderText = "Add member:";

/// @brief 拆出行文本前缀
constexpr const char* kDetachPrefix = "Detach ";

/// @brief 加入行文本前缀
constexpr const char* kJoinPrefix = "Join ";

}   // namespace

MemberListPopup::MemberListPopup(){

	// 裸 Widget 容器（非 Panel——G-5 红线）；尺寸由 BoxView 装配时设定
	SetVisible(false);   // 常驻隐藏态（装配后由 OpenPopup 显示）
}

void MemberListPopup::ClearRows(){

	// 拆旧行：RemoveChild 返 unique_ptr——离开作用域即销毁（子树拥有）
	while (GetChildCount() > 0){

		RemoveChild(GetChildAt(0));
	}

	m_memberRows.clear();
	m_detachRow = nullptr;
	m_joinRows.clear();
}

Button* MemberListPopup::MakeRow(const std::string& text, bool enabled, int y){

	auto button = std::make_unique<Button>(text);

	Button* raw = button.get();

	raw->SetPosition(Metrics::kPopupRowPad, y);
	raw->SetSize(Metrics::kPopupWidth - 2 * Metrics::kPopupRowPad, Metrics::kPopupRowHeight);
	raw->SetEnabled(enabled);
	raw->SetTextColor(Palette::TextForeground());

	AddChild(std::move(button));

	return raw;
}

std::string MemberListPopup::MemberRowText(const BoxModel& model, const Box& box, BoxId member){

	const bool isActive = (box.active == member);

	return std::string(isActive ? kActivePrefix : kInactivePrefix)
	       + model.GetBox(member).title;
}

void MemberListPopup::Rebuild(const BoxModel& model, BoxId box){

	ClearRows();

	const Box& record = model.GetBox(box);
	const std::size_t memberCount = record.members.size();

	// ── 分区①：成员单选行（勾选即切换）──
	// 布局表（详设 △3）：成员行 i = (4, 4 + i×24, 172, 24)
	int y = 4;

	for (std::size_t i = 0; i < memberCount; ++i){

		const BoxId member = record.members[i];

		Button* row = MakeRow(MemberRowText(model, record, member), true, y);

		row->SetOnClick([this, member]{

			if (m_onSelected){

				m_onSelected(member);
			}
		});

		m_memberRows.push_back(row);

		y += Metrics::kPopupRowHeight;
	}

	// ── 分区②：成员管理 ──
	// 拆出行（可用性 = CanDetach——C-M1-24 前置同源）
	const BoxId active = record.active;
	const std::string activeTitle = (active >= 0) ? model.GetBox(active).title : std::string{};

	Button* detachRow = MakeRow(std::string(kDetachPrefix) + "\"" + activeTitle + "\"",
	                            model.CanDetach(box), y + 4);

	detachRow->SetOnClick([this]{

		if (m_onDetach){

			m_onDetach();
		}
	});

	m_detachRow = detachRow;

	y += 4 + Metrics::kPopupRowHeight;

	// 「加入成员」分区头（非交互 Label）
	{
		auto header = std::make_unique<Label>(kJoinHeaderText);

		header->SetPosition(Metrics::kPopupRowPad, y);
		header->SetSize(Metrics::kPopupWidth - 2 * Metrics::kPopupRowPad, 20);
		header->SetTextColor(Palette::SecondaryText());

		AddChild(std::move(header));
	}

	y += 20;

	// 加入行：列出桌面上其他独立框（排除 owner 自身）
	const std::vector<BoxId> topLevel = model.GetTopLevelBoxes();

	for (const BoxId candidate : topLevel){

		if (candidate == box){

			continue;
		}

		Button* row = MakeRow(std::string(kJoinPrefix) + model.GetBox(candidate).title, true, y);

		row->SetOnClick([this, candidate]{

			if (m_onCollect){

				m_onCollect(candidate);
			}
		});

		m_joinRows.push_back(row);

		y += Metrics::kPopupRowHeight;
	}
}

void MemberListPopup::Refresh(const BoxModel& model, BoxId box){

	// ★ C-M1-19 R2：原位更新文本/可用性——**不创建、不销毁任何控件**
	// （可见期模型变更只可能是 Activated ⇒ 成员数不变 ⇒ 行数与顺序均一致）
	const Box& record = model.GetBox(box);

	for (std::size_t i = 0; i < m_memberRows.size() && i < record.members.size(); ++i){

		m_memberRows[i]->SetText(MemberRowText(model, record, record.members[i]));
		m_memberRows[i]->Invalidate();   // SetText 不请求重绘（架构边界约定）
	}

	if (m_detachRow != nullptr && record.active >= 0){

		m_detachRow->SetText(std::string(kDetachPrefix) + "\""
		                     + model.GetBox(record.active).title + "\"");
		m_detachRow->SetEnabled(model.CanDetach(box));
		m_detachRow->Invalidate();
	}
}

std::string MemberListPopup::GetMemberRowText(std::size_t index) const{

	if (index >= m_memberRows.size()){

		return {};
	}

	return m_memberRows[index]->GetText();
}

bool MemberListPopup::IsDetachRowEnabled() const{

	return (m_detachRow != nullptr) && m_detachRow->IsEnabled();
}

void MemberListPopup::OnPaint(PaintContext& ctx, int x, int y){

	// 圆角背景（坐标约定同 Button::OnPaint：x/y = 本控件最终偏移）
	ctx.DrawRoundedRect(
		Rect{ static_cast<float>(x), static_cast<float>(y),
		      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
		static_cast<float>(Metrics::kPopupRadius), Palette::PopupBackground());
}

}   // namespace ECDI::DesktopNest
