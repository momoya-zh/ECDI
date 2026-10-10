#include "BoxWindow.h"   // .cpp 对应头（第一）

#include "BoxView.h"

#include <utility>   // std::move

namespace ECDI::DesktopNest {

void BoxWindow::Create(Application& application, const Rect& placement,
                       BoxView& view, std::string title){

	// ① 幂等前置：释放旧窗并**立即**断开包装指针（C-M1-16 / C-M1-20(b)）
	if (m_window != nullptr){

		m_window->Release();
		m_window = nullptr;
		m_open = false;
	}

	// ② 唯一构造入口（Window.h:42；无失败返回——恒返回引用）
	m_window = &application.Create(std::move(title), Metrics::kWindowWidth, Metrics::kWindowHeight);

	// ③–⑥ 配置期四件套（均须在 Show 前——F2–F4）
	m_window->SetChromeMode(ChromeMode::Borderless);        // F2
	m_window->SetCaptionHeight(Metrics::kCaptionHeight);    // F3——标题条行为区（K5 前提）
	m_window->SetResizeInset(0);                            // K5：不可缩放（Phase 12 既有契约）
	m_window->SetWindowLayer(WindowLayer::Desktop);         // F4——K1 常驻（Win+D 后仍可见）

	// ⑦ 几何通道注入（C-M1-14）
	view.SetBoxWindow(this);

	// ⑧ 视图全量重建（S3——C-M1-16⑧）
	view.Assemble(m_window->GetRootWidget());

	// ⑨ 初始边界（Phase 31——请求语义）
	m_window->SetBounds(placement);

	// ⑩ 存活标记置末位（C-M1-16⑩——②–⑨ 之间异常时不会报告存活）
	m_open = true;
}

void BoxWindow::Close(){

	if (m_window != nullptr){

		m_window->Release();   // 幂等（Win32PlatformWindow::Release——重复调用安全）
		m_window = nullptr;    // ★ 必须置空（C-M1-20(b)：Window 对象延迟销毁 ⇒ 否则悬空）
	}

	m_open = false;
}

void BoxWindow::Show(){

	if (m_window != nullptr){

		m_window->Show();
	}
}

void BoxWindow::SetBounds(const Rect& bounds){

	if (m_window != nullptr){

		m_window->SetBounds(bounds);
	}
}

Rect BoxWindow::GetBounds() const{

	if (m_window == nullptr){

		return Rect{};   // Close 后 / 未创建：空 Rect（C-M1-22(c)）
	}

	return m_window->GetBounds();
}

}   // namespace ECDI::DesktopNest
