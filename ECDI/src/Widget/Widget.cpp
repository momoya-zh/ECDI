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

#include "ECDI/Layout/Layout.h"

#include <algorithm>
#include <utility>

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
		const int localX = x - child->m_geometry.x;
		const int localY = y - child->m_geometry.y;

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

void Widget::Paint(HDC hdc,int offsetX,int offsetY){
	
	if (!IsVisible())
		return;

	int x = offsetX + m_geometry.x;
	int y = offsetY + m_geometry.y;

	OnPaint(hdc,x,y);

	for(auto& child : m_children){

		child->Paint(hdc,x,y);

	}

}





// ── 事件处理默认实现（空函数，子类按需 override）────────────

void Widget::OnPaint(HDC,int,int){}

void Widget::OnMouseMove(const MouseMoveEvent&) {}

void Widget::OnMouseButtonDown(const MouseButtonDownEvent&) {}

void Widget::OnMouseButtonUp(const MouseButtonUpEvent&) {}

void Widget::OnMouseWheel(const MouseWheelEvent&) {}

void Widget::OnKeyDown(const KeyDownEvent&) {}

void Widget::OnKeyUp(const KeyUpEvent&) {}

void Widget::OnCharInput(const CharInputEvent&) {}
