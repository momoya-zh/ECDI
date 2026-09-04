#include "ECDI/Widget/Widget.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/Layout.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <utility>

namespace ECDI{

Widget::Widget() = default;

Widget::~Widget() = default;

bool Widget::Contains(const Widget* widget) const noexcept{

	if (this == widget){

		return true;

	}

	// 递归搜索所有后代
	for (const auto& child : m_children){

		if (child->Contains(widget)){

			return true;

		}

	}

	return false;
}

void Widget::AddChild(std::unique_ptr<Widget> child){

	// 前置条件：child 非空
	FRAMEWORK_ASSERT(child);

	// 前置条件：child 尚无父节点（禁止多父）
	FRAMEWORK_ASSERT(child->m_parent == nullptr);

	// 前置条件：child 的子树中不能包含 this（防环）
	FRAMEWORK_ASSERT(!child->Contains(this));

	child->m_parent = this;

	m_children.emplace_back(std::move(child));

}

std::unique_ptr<Widget>Widget::RemoveChild(Widget* child){

	FRAMEWORK_ASSERT(child);

	// 在子节点列表中查找目标
	auto it = std::find_if(
		m_children.begin(),
		m_children.end(),
		[child](const auto& item){
		
			return item.get() == child;

		}
	);


	FRAMEWORK_ASSERT(

		it != m_children.end()

	);


	// 取出 unique_ptr 所有权并从列表中删除
	std::unique_ptr<Widget> result=std::move(*it);

	m_children.erase(it);

	// 清除父指针（已脱离树）
	result->m_parent = nullptr;

	return result;
}

bool Widget::ContainsPoint(int x, int y) const noexcept{

	// 默认命中区域：矩形 [0, width) × [0, height)
	return x >= 0 &&y >= 0 &&x < m_geometry.width &&y < m_geometry.height;

}

Widget* Widget::HitTest(int x, int y) noexcept {

	// 不可见的 Widget 不参与命中测试
	if (!IsVisible()){

		return nullptr;

	}

	// 禁用的 Widget 不参与命中测试
	if (!IsEnabled()) {

		return nullptr;

	}

	// 逆序遍历子节点（后添加的在上层，优先命中）
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
	{
		Widget* child = it->get();

		// 坐标转换：从当前 Widget 的局部坐标转到子 Widget 的局部坐标
		const int localX = x - static_cast<int>(child->m_geometry.x);
		const int localY = y - static_cast<int>(child->m_geometry.y);

		// 递归检测子节点
		if (Widget* target = child->HitTest(localX, localY))
		{
			return target;
		}
	}

	// 子节点均未命中，检测自身
	if (ContainsPoint(x, y))
	{
		return this;
	}

	return nullptr;

}

void Widget::SetLayout(std::unique_ptr<Layout> layout){

	m_layout = std::move(layout);

}

Widget* Widget::GetChildAt(size_t index) noexcept{

	FRAMEWORK_ASSERT(index < m_children.size());

	return m_children[index].get();

}


const Widget* Widget::GetChildAt(size_t index) const noexcept{

	FRAMEWORK_ASSERT(index < m_children.size());

	return m_children[index].get();

}

void Widget::ArrangeInternal(){

	if (m_layout){

		m_layout->Arrange(*this);

	}


	for (auto& child : m_children){

		child->ArrangeInternal();

	}
}

void Widget::Arrange(){

	ArrangeInternal();

}

void Widget::SetSize(int w, int h){

	m_geometry.width = static_cast<float>(w);
	m_geometry.height = static_cast<float>(h);

}

void Widget::SetStretch(int stretch) noexcept{

	FRAMEWORK_ASSERT(stretch >= 0);   // 负值无合理权重语义（详设 §2.1）

	m_stretch = stretch;

}

Size Widget::GetPreferredSize() const{

	return Size{ static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

}

bool Widget::AutoSize(){

	if (GetStretch() > 0)
		return false;                       // §3.5 条 1：stretch 互斥——调用时判断（SetStretch(0) 后重新生效）

	const Size preferred = GetPreferredSize();
	const int w = static_cast<int>(preferred.width);   // float → int 向零截断（详设 v1.1——v1 无 rounding policy）
	const int h = static_cast<int>(preferred.height);

	if (w == GetWidth() && h == GetHeight())
		return false;                       // 同尺寸 no-op

	SetSize(w, h);                          // 虚分派——TextBox::SetSize 的 EnsureCaretVisible+Invalidate 为 SetSize 既有语义
	return true;

}

void Widget::Paint(PaintContext& ctx,int offsetX,int offsetY){
	
	if (!IsVisible())
		return;   // 9.5 R1：唯一例外路径——不 Push 不 Pop（天然平衡）

	int x = offsetX + static_cast<int>(m_geometry.x);
	int y = offsetY + static_cast<int>(m_geometry.y);

	// 9.5 R1：自身边界入栈（绝对坐标 = Window 客户区；与 OnPaint 的 x/y 同源——不变量 I3）
	// 嵌套交集：子控件被"父边界 ∩ 自身边界"自动裁剪（后端 IntersectClipRect 天然语义）
	ctx.PushClip(Rect{ static_cast<float>(x), static_cast<float>(y),
	                   m_geometry.width, m_geometry.height });

	OnPaint(ctx,x,y);

	for(auto& child : m_children){

		child->Paint(ctx,x,y);

	}

	ctx.PopClip();   // 严格配对（不变量 I1：Paint 内无其他 return 路径）

}

// ── 事件处理默认实现（空函数，子类按需 override）────────────

void Widget::OnPaint(PaintContext&,int,int){}

void Widget::OnMouseMove(const MouseMoveEvent&) {}

void Widget::OnMouseButtonDown(const MouseButtonDownEvent&) {}

void Widget::OnMouseButtonUp(const MouseButtonUpEvent&) {}

void Widget::OnMouseWheel(const MouseWheelEvent&) {}

void Widget::OnKeyDown(const KeyDownEvent&) {}

void Widget::OnKeyUp(const KeyUpEvent&) {}

void Widget::OnCharInput(const CharInputEvent&) {}

void Widget::OnTimer(const TimerEvent&) {}

void Widget::OnMouseEnter() {}

void Widget::OnMouseLeave() {}

// ── 交互/重绘（5.4.1）───────────────────────────────

Window* Widget::GetWindow() noexcept{

	Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window;

}

const Window* Widget::GetWindow() const noexcept{

	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window;

}

void Widget::SetWindow(Window* window){

	m_window = window;

}

void Widget::Invalidate(){

	if (Window* window = GetWindow()){

		window->Invalidate();

	}

}

bool Widget::HasFocus() const noexcept{

	const Widget* root = this;

	while (root->m_parent){

		root = root->m_parent;

	}

	return root->m_window && root->m_window->GetFocusedWidget() == this;

}

Point Widget::GetAbsolutePosition() const noexcept{

	Point pos{

		static_cast<float>(GetX()),

		static_cast<float>(GetY())

	};

	const Widget* parent = m_parent;

	while (parent){

		pos.x += static_cast<float>(parent->GetX());

		pos.y += static_cast<float>(parent->GetY());

		parent = parent->GetParent();

	}

	return pos;

}

}
