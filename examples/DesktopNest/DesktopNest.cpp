#include "DesktopNest.h"   // .cpp 对应头（第一）

#include <utility>   // std::move

namespace ECDI::DesktopNest {

namespace {

/// @brief 冻结初始态摆放（贴顶边一排——需求 §5 R-3 的「框倾向贴边」）
constexpr int kInitialX = 16;
constexpr int kInitialY = 16;
constexpr int kBoxGap = 16;   ///< 框间距（220 + 16 = 236 递推）

/// @brief 三框初始 X（16 / 252 / 488）
constexpr int BoxX(int index) noexcept { return kInitialX + index * (Metrics::kWindowWidth + kBoxGap); }

/// @brief 成员标题（K6 冻结初始态）
constexpr const char* kMemberTitles[] = { "C1", "C2" };

}   // namespace

BoxId DesktopNestApp::MakeBox(const std::string& title, const Rect& placement, bool show){

	const BoxId id = m_model.AddBox(title);

	BoxEntry& entry = m_boxes[id];

	entry.window = std::make_unique<BoxWindow>();
	entry.view = std::make_unique<BoxView>(m_model, id);

	// 回调接线：动作上抛协调器 / 被收编关窗 / 单浮层纪律
	entry.view->SetOnAction([this, id](BoxView::Action action, BoxId source){
		OnBoxAction(id, action, source);
	});
	entry.view->SetOnAbsorbed([this, id]{ CloseBoxEntry(id); });
	entry.view->SetOnPopupOpening([this, id]{ CloseOtherPopups(id); });

	entry.window->Create(*this, placement, *entry.view, title);

	if (show){

		entry.window->Show();
	}

	return id;
}

BoxId DesktopNestApp::MakeMergedBox(const std::string& title,
                                    const std::vector<std::string>& members,
                                    const Rect& placement, bool show){

	const BoxId id = m_model.AddMergedBox(title, members);

	BoxEntry& entry = m_boxes[id];

	entry.window = std::make_unique<BoxWindow>();
	entry.view = std::make_unique<BoxView>(m_model, id);

	entry.view->SetOnAction([this, id](BoxView::Action action, BoxId source){
		OnBoxAction(id, action, source);
	});
	entry.view->SetOnAbsorbed([this, id]{ CloseBoxEntry(id); });
	entry.view->SetOnPopupOpening([this, id]{ CloseOtherPopups(id); });

	entry.window->Create(*this, placement, *entry.view, title);

	if (show){

		entry.window->Show();
	}

	return id;
}

DesktopNestApp::Fixture DesktopNestApp::BuildFixture(bool show){

	const Rect aPlacement{ static_cast<float>(BoxX(0)), static_cast<float>(kInitialY),
	                       static_cast<float>(Metrics::kWindowWidth), static_cast<float>(Metrics::kWindowHeight) };
	const Rect bPlacement{ static_cast<float>(BoxX(1)), static_cast<float>(kInitialY),
	                       static_cast<float>(Metrics::kWindowWidth), static_cast<float>(Metrics::kWindowHeight) };
	const Rect cPlacement{ static_cast<float>(BoxX(2)), static_cast<float>(kInitialY),
	                       static_cast<float>(Metrics::kWindowWidth), static_cast<float>(Metrics::kWindowHeight) };

	const BoxId a = MakeBox("Box A", aPlacement, show);
	const BoxId b = MakeBox("Box B", bPlacement, show);
	const BoxId c = MakeMergedBox("Box C", { kMemberTitles[0], kMemberTitles[1] }, cPlacement, show);

	return Fixture{ a, b, c };
}

int DesktopNestApp::Run(){

	BuildFixture(true);

	return Application::Run();
}

void DesktopNestApp::Collect(BoxId target, BoxId source){

	// 流程前置守卫（C-M1-22(f)）：已关框不参与业务流程——不改模型、不把空 Rect 当落点
	if (!m_boxes.at(target).window->IsOpen() || !m_boxes.at(source).window->IsOpen()){

		return;
	}

	// ① 读几何（「记 placement 要读」——Phase 31 GetBounds）
	const Rect placement = m_boxes.at(source).window->GetBounds();

	if (placement.width <= 0.0f){

		return;   // 边界无效（防御——正常路径 IsOpen 已保证）
	}

	// ② Model 变更（通知驱动余下一切）
	m_model.Collect(target, source, placement);
}

void DesktopNestApp::Detach(BoxId box){

	// 流程前置守卫（C-M1-22(f)）
	if (!m_boxes.at(box).window->IsOpen()){

		return;
	}

	if (!m_model.CanDetach(box)){

		return;
	}

	// ① 落点：有记忆 ⇒ 恢复；无记忆 ⇒ 级联（§9-6）
	const BoxId member = m_model.GetBox(box).active;

	// ★ 引用即取即用（C-M1-24 / 复评 S4）：GetBox 返回引用的寿命受 push_back 影响 ⇒ 按值取
	const Rect recorded = m_model.GetBox(member).placement;

	Rect placement = recorded;

	if (recorded.width <= 0.0f){

		// 级联基准 = 源框自己当前窗口边界（源框必然打开——动作由其浮层发起）
		placement = CascadedPlacement(m_boxes.at(box).window->GetBounds());
	}

	// ② Model 变更
	m_model.Detach(box);

	// ③ 被拆成员重建独立窗（同一 BoxWindow 包装对象——C-M1-10③）
	const std::string title = m_model.GetBox(member).title;

	BoxEntry& entry = m_boxes.at(member);

	entry.window->Create(*this, placement, *entry.view, title);
	entry.window->Show();
}

void DesktopNestApp::CloseBoxEntry(BoxId box){

	BoxEntry& entry = m_boxes.at(box);

	// ★ 顺序不可交换（C-M1-20(c)）：Close 后旧树仍存活到泵清理点 ⇒ Disassemble 必须紧随
	entry.window->Close();
	entry.view->Disassemble();
}

void DesktopNestApp::CloseOtherPopups(BoxId keepOpen){

	for (auto& item : m_boxes){

		if (item.first != keepOpen){

			item.second.view->ClosePopup();
		}
	}
}

void DesktopNestApp::OnBoxAction(BoxId owner, BoxView::Action action, BoxId source){

	switch (action){

	case BoxView::Action::Detach:
		Detach(owner);
		break;

	case BoxView::Action::Collect:
		Collect(owner, source);
		break;
	}
}

BoxId DesktopNestApp::FindBoxByWindow(const Window* window) const{

	if (window == nullptr){

		return -1;
	}

	for (const auto& item : m_boxes){

		if (item.second.window->GetWindow() == window){

			return item.first;
		}
	}

	return -1;
}

bool DesktopNestApp::IsBoxClosed(BoxId box) const{

	const auto it = m_boxes.find(box);

	if (it == m_boxes.end()){

		return false;
	}

	return !it->second.window->IsOpen();
}

void DesktopNestApp::OnWindowCloseRequested(const WindowCloseRequestedEvent& event){

	// C-M1-22：先同步包装器与视图（与 CloseBoxEntry 同一收尾），**再委托基类**实际销毁。
	// 顺序不可颠倒——否则基类返回后包装器仍报存活。
	const BoxId box = FindBoxByWindow(event.GetWindow());

	if (box >= 0){

		CloseBoxEntry(box);
	}

	Application::OnWindowCloseRequested(event);   // 基类默认 = Release()（实际销毁）
}

}   // namespace ECDI::DesktopNest
