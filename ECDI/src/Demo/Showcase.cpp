#include "Showcase.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Theme/ProgressBarStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CheckBox.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/Radio.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace ECDI{

namespace{

// ── Showcase 内部配色（暗色风——黑底 + 灰阶层级 + 白/浅灰字）──
// 四层底色：内容区底（近黑）→ 卡片（深灰）→ 内层（中灰）→ TextBox 背景（与内层区分的灰）
constexpr Color kCard()          noexcept{ return Color::FromRGBA8(18, 20, 24, 255); }    // 内容区/导航底（近黑——衬托卡片）
constexpr Color kInnerCard()     noexcept{ return Color::FromRGBA8(32, 35, 41, 255); }    // 内层卡片（深灰——层次）
constexpr Color kCardLayer3()    noexcept{ return Color::FromRGBA8(48, 52, 60, 255); }    // 最内层（中灰——内容承载）
constexpr Color kTextBoxBg()     noexcept{ return Color::FromRGBA8(42, 45, 52, 255); }    // TextBox 背景（与内层区分的深浅灰）
constexpr Color kTitle()         noexcept{ return Color::FromRGBA8(240, 242, 248, 255); }  // 标题白
constexpr Color kHint()          noexcept{ return Color::FromRGBA8(150, 156, 168, 255); }  // 说明浅灰
constexpr Color kAccentGreen()   noexcept{ return Color::FromRGBA8(80, 200, 120, 255); }   // 语义绿（暗色提亮）
constexpr Color kAccentOrange()  noexcept{ return Color::FromRGBA8(240, 165, 80, 255); }   // 语义橙（暗色提亮）
constexpr Color kBtnDarkBg()     noexcept{ return Color::FromRGBA8(45, 48, 55, 255); }     // 大按钮底（深灰）
constexpr Color kBtnLightFg()    noexcept{ return Color::FromRGBA8(225, 228, 235, 255); }  // 大按钮字（浅灰白——比底色浅）
constexpr Color kSmallBtnBg()    noexcept{ return Color::FromRGBA8(240, 242, 248, 255); } // 小按钮底（近白）
constexpr Color kSmallBtnFg()    noexcept{ return Color::FromRGBA8(30, 32, 38, 255); }     // 小按钮字（近黑）
constexpr float kCornerNormal = 8.0f;   ///< 常规圆角（8px——现代柔和）

constexpr int kPageMargin = 36;   ///< 页面左右留白（36px——舒展）

/// @brief 页面标题 Label（顶部大字号）
std::unique_ptr<Label> MakePageTitle(const std::string& text, int width){
	auto label = std::make_unique<Label>(text);
	label->SetSize(width - 2 * kPageMargin, 36);
	label->SetFont(Font{ .size = 20.0f });
	label->SetTextColor(kTitle());
	return label;
}

/// @brief 页面说明 Label（灰色小字）
std::unique_ptr<Label> MakeHint(const std::string& text, int width){
	auto label = std::make_unique<Label>(text);
	label->SetSize(width - 2 * kPageMargin, 22);
	label->SetTextColor(kHint());
	return label;
}

/// @brief 垂直间距占位（VerticalLayout 无间距支持——透明 Widget 撑高；
/// SetVisible(false) 不画不命中，但 Arrange 仍排位 GetHeight → 间距生效）
std::unique_ptr<Widget> MakeSpacer(int h = 10){
	auto s = std::make_unique<Widget>();
	s->SetSize(1, h);
	s->SetVisible(false);
	return s;
}

}   // namespace

// ── 左侧导航栏：标题 + 6 页签（VerticalLayout）──────────────────────────

namespace Demo{

ShowcaseNav BuildNavPanel(int width, int height){
	auto panel = std::make_unique<Panel>();
	panel->SetSize(width, height);   // 导航卡片背景尺寸（子按钮经 VerticalLayout 排位）
	panel->SetStyle(PanelStyleOverride{ .background = kCard() });
	panel->SetLayout(std::make_unique<VerticalLayout>());

	auto title = std::make_unique<Label>("ECDI Showcase");
	title->SetSize(width, 44);
	title->SetFont(Font{ .size = 20.0f });
	title->SetTextColor(kTitle());
	panel->AddChild(std::move(title));
	panel->AddChild(MakeSpacer(12));

	ShowcaseNav nav;
	nav.buttons.reserve(6);
	for (const char* name : { "Buttons", "Input", "Selection", "Containers", "Animation", "Rendering" }){
		auto btn = std::make_unique<Button>(name);
		btn->SetSize(width - 24, 40);
		btn->SetStyle(ButtonStyleOverride{ .background = kBtnDarkBg(), .cornerRadius = kCornerNormal });
		btn->SetTextColor(kBtnLightFg());
		nav.buttons.push_back(btn.get());
		panel->AddChild(std::move(btn));
		panel->AddChild(MakeSpacer(6));   // 页签间垂直间距
	}
	nav.panel = std::move(panel);   // ⚠️ 所有权转入 nav（此前漏赋值 → nav.panel 为 nullptr → 调用方空指针崩溃）
	return nav;
}

// ── 页面：Buttons ───────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildButtonsPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Buttons", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("标准按钮 + 样式覆盖（SetStyle 运行时注入——Phase 9 决策层）", width));
	page->AddChild(MakeSpacer(10));

	// 统一按钮工厂：大尺寸按钮——暗灰底/语义色底 + 白字（比底色浅，暗色风可读性）
	auto makeBtn = [width](const std::string& text, ButtonStyleOverride style, const std::string& logName){
		auto btn = std::make_unique<Button>(text);
		btn->SetSize(width - 2 * kPageMargin, 44);
		btn->SetStyle(style);
		btn->SetTextColor(Color::White());
		btn->SetOnClick([logName]{ Logger::Log(LogLevel::Info, UTF8ToWide("Button: " + logName)); });
		return btn;
	};

	page->AddChild(makeBtn("Default",      ButtonStyleOverride{ .background = kBtnDarkBg(), .cornerRadius = kCornerNormal }, "Default"));
	page->AddChild(MakeSpacer(8));
	page->AddChild(makeBtn("Green",        ButtonStyleOverride{ .background = kAccentGreen(), .cornerRadius = kCornerNormal }, "Green"));
	page->AddChild(MakeSpacer(8));
	page->AddChild(makeBtn("Orange",       ButtonStyleOverride{ .background = kAccentOrange(), .cornerRadius = kCornerNormal }, "Orange"));
	page->AddChild(MakeSpacer(8));
	page->AddChild(makeBtn("Translucent",  ButtonStyleOverride{ .background = Color::FromRGBA8(0, 255, 220, 60), .cornerRadius = 80.0f }, "Translucent"));
	page->AddChild(MakeSpacer(8));
	page->AddChild(makeBtn("Focus Test",   ButtonStyleOverride{ .background = kBtnDarkBg(), .cornerRadius = kCornerNormal }, "Focus Test"));

	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("点击按钮 → Debug 日志（事件分发验证）；Tab 聚焦 Focus Test 显示点线框", width));
	return page;
}

// ── 页面：Input ─────────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildInputPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Input", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("文本框：IME 组合 / 光标定位 / 拖选 / 退格（Phase 5.5-5.6）", width));
	page->AddChild(MakeSpacer(10));

	auto textBox = std::make_unique<TextBox>("Type something...");
	textBox->SetSize(360, 30);
	textBox->SetStyle(ECDI::TextBoxStyleOverride{ .background = kTextBoxBg() });
	textBox->SetTextColor(Color::White());

	auto echoBtn = std::make_unique<Button>("Echo");
	echoBtn->SetSize(120, 36);
	echoBtn->SetStyle(ButtonStyleOverride{ .background = kSmallBtnBg(), .cornerRadius = kCornerNormal });
	echoBtn->SetTextColor(kSmallBtnFg());

	auto echoLabel = std::make_unique<Label>("Echo: ");
	echoLabel->SetSize(width - 2 * kPageMargin, 24);
	echoLabel->SetTextColor(kAccentGreen());

	// 状态指针（move 前抓取；树生命周期 = 窗口 > 回调生命周期）
	TextBox* textBoxPtr = textBox.get();
	Label* echoLabelPtr = echoLabel.get();
	echoBtn->SetOnClick([textBoxPtr, echoLabelPtr]{
		echoLabelPtr->SetText("Echo: " + textBoxPtr->GetText());
	});

	page->AddChild(std::move(textBox));
	page->AddChild(MakeSpacer(8));
	page->AddChild(std::move(echoBtn));
	page->AddChild(MakeSpacer(8));
	page->AddChild(std::move(echoLabel));

	page->AddChild(MakeSpacer(10));
	page->AddChild(MakeHint("Tab 切换焦点；点击定位光标；IME 中文输入组合验证", width));
	page->AddChild(MakeSpacer(8));
	page->AddChild(MakeHint("长文本：拖选 + 超宽裁切（9.5 R1 Clip）", width));
	page->AddChild(MakeSpacer(8));

	auto longBox = std::make_unique<TextBox>("Hello World, this is a very long text for selection testing.");
	longBox->SetSize(360, 30);
	longBox->SetStyle(ECDI::TextBoxStyleOverride{ .background = kTextBoxBg() });
	longBox->SetTextColor(Color::White());
	page->AddChild(std::move(longBox));

	page->AddChild(MakeSpacer(10));
	page->AddChild(MakeHint("输入内容经 Echo 按钮回显——Label::SetText 状态流；页面切换间输入保留（预建）", width));
	return page;
}

// ── 页面：Selection ─────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildSelectionPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Selection", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("CheckBox：独立状态，Space 键切换（Phase 6.2）", width));
	page->AddChild(MakeSpacer(10));

	auto cbA = std::make_unique<CheckBox>("Option A");
	cbA->SetSize(240, 28);
	cbA->SetTextColor(Color::White());
	auto cbB = std::make_unique<CheckBox>("Option B");
	cbB->SetSize(240, 28);
	cbB->SetTextColor(Color::White());

	auto stateLabel = std::make_unique<Label>("A: off  B: off");
	stateLabel->SetSize(width - 2 * kPageMargin, 24);
	stateLabel->SetTextColor(kAccentGreen());

	// CheckBox 状态跨回调共享——堆分配状态容器（页面函数返回后回调仍安全）
	struct SelState{ bool a = false; bool b = false; };
	auto sel = std::make_shared<SelState>();
	Label* statePtr = stateLabel.get();
	cbA->SetOnCheckedChanged([sel, statePtr](bool v){
		sel->a = v;
		statePtr->SetText("A: " + std::string(v ? "on" : "off") + "  B: " + std::string(sel->b ? "on" : "off"));
	});
	cbB->SetOnCheckedChanged([sel, statePtr](bool v){
		sel->b = v;
		statePtr->SetText("A: " + std::string(sel->a ? "on" : "off") + "  B: " + std::string(v ? "on" : "off"));
	});

	page->AddChild(std::move(cbA));
	page->AddChild(std::move(cbB));
	page->AddChild(std::move(stateLabel));

	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("Radio：同父互斥——选中自动取消兄弟（用户交互不可取消，Phase 6.2）", width));
	page->AddChild(MakeSpacer(10));

	auto r1 = std::make_unique<Radio>("Choice 1");
	r1->SetSize(240, 28);
	r1->SetChecked(true);   // 初始选中（须在 AddChild 前——此后 unique_ptr 被 move）
	r1->SetTextColor(Color::White());
	auto r2 = std::make_unique<Radio>("Choice 2");
	r2->SetSize(240, 28);
	r2->SetTextColor(Color::White());
	auto r3 = std::make_unique<Radio>("Choice 3");
	r3->SetSize(240, 28);
	r3->SetTextColor(Color::White());

	auto radioLabel = std::make_unique<Label>("Selected: Choice 1");
	radioLabel->SetSize(width - 2 * kPageMargin, 24);
	radioLabel->SetTextColor(kAccentOrange());

	// Radio 选中回显（checked==true 的才是新选中——互斥时兄弟先收 false，忽略之）
	Label* radioPtr = radioLabel.get();
	r1->SetOnCheckedChanged([radioPtr, self = r1.get()](bool v){
		if (v){ radioPtr->SetText("Selected: " + self->GetText()); }
	});
	r2->SetOnCheckedChanged([radioPtr, self = r2.get()](bool v){
		if (v){ radioPtr->SetText("Selected: " + self->GetText()); }
	});
	r3->SetOnCheckedChanged([radioPtr, self = r3.get()](bool v){
		if (v){ radioPtr->SetText("Selected: " + self->GetText()); }
	});

	page->AddChild(std::move(r1));
	page->AddChild(std::move(r2));
	page->AddChild(std::move(r3));
	page->AddChild(std::move(radioLabel));

	page->AddChild(MakeHint("Tab 聚焦 + Space 切换；点击 Choice 2/3 自动取消 Choice 1", width));
	return page;
}

// ── 页面：Containers ────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildContainersPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Containers", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("CollapsiblePanel：默认收起——点击外部按钮展开（200ms 高度动画，9.6）", width));
	page->AddChild(MakeSpacer(10));

	auto toggle = std::make_unique<Button>("Toggle Collapsible");
	toggle->SetSize(240, 36);
	toggle->SetStyle(ButtonStyleOverride{ .background = kSmallBtnBg(), .cornerRadius = kCornerNormal });
	toggle->SetTextColor(kSmallBtnFg());

	auto coll = std::make_unique<CollapsiblePanel>();
	coll->SetSize(480, 130);   // 默认 Down + 默认收起：SetSize = 定义展开基准（m_expandedRect）
	auto collText = std::make_unique<Label>("Collapsible content area (0↔130 height animation)");
	collText->SetPosition(10, 10);
	collText->SetSize(460, 30);
	collText->SetTextColor(kTitle());
	coll->GetContent()->AddChild(std::move(collText));

	CollapsiblePanel* collPtr = coll.get();
	toggle->SetOnClick([collPtr]{ collPtr->Toggle(); });

	page->AddChild(std::move(toggle));

	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("Panel 透明语义：透明 Panel = 纯布局容器——背景由视觉卡片层决定（2026-08-30 v1.1）", width));
	page->AddChild(MakeSpacer(10));

	// 透明容器（row）嵌视觉卡片（cardA/cardB）——HorizontalLayout 横排
	auto row = std::make_unique<Panel>();
	row->SetSize(width - 2 * kPageMargin, 132);
	row->SetLayout(std::make_unique<HorizontalLayout>());

	auto cardA = std::make_unique<Panel>();
	cardA->SetSize(280, 120);
	cardA->SetStyle(PanelStyleOverride{ .background = kInnerCard() });
	auto la = std::make_unique<Label>("Inner card A");
	la->SetPosition(12, 12);
	la->SetSize(200, 24);
	la->SetTextColor(kTitle());
	cardA->AddChild(std::move(la));

	auto cardB = std::make_unique<Panel>();
	cardB->SetSize(280, 120);
	cardB->SetStyle(PanelStyleOverride{ .background = kInnerCard() });
	auto lb = std::make_unique<Label>("Inner card B");
	lb->SetPosition(12, 12);
	lb->SetSize(200, 24);
	lb->SetTextColor(kTitle());
	auto cb = std::make_unique<CheckBox>("Nested CheckBox");
	cb->SetPosition(12, 44);
	cb->SetSize(200, 28);
	cb->SetTextColor(kTitle());
	cardB->AddChild(std::move(lb));
	cardB->AddChild(std::move(cb));

	row->AddChild(std::move(cardA));
	row->AddChild(std::move(cardB));
	page->AddChild(std::move(row));

	page->AddChild(MakeSpacer(10));
	page->AddChild(MakeHint("嵌套卡片：透明容器（row）嵌视觉卡片（cardA/B），控件自然落位", width));

	// CollapsiblePanel 放页面最后——VerticalLayout 按收起态高度 0 排后续元素，
	// 展开动画会长高覆盖下方元素（CollapsiblePanel × Layout 组合首个场景）；最后子节点无后续元素
	page->AddChild(std::move(coll));
	return page;
}

// ── 页面：Animation ─────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildAnimationPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Animation", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("ProgressBar：200ms EaseOut 平滑过渡 / 替换式重启 / 同目标 no-op（9.6）", width));
	page->AddChild(MakeSpacer(10));

	auto bar1 = std::make_unique<ProgressBar>();
	bar1->SetSize(width - 2 * kPageMargin, 20);
	bar1->SetStyle(ProgressBarStyleOverride{ .trackColor = kCardLayer3() });
	auto bar2 = std::make_unique<ProgressBar>();
	bar2->SetSize(width - 2 * kPageMargin, 20);
	bar2->SetStyle(ProgressBarStyleOverride{ .trackColor = kCardLayer3(), .fillColor = kAccentGreen(), .cornerRadius = 8.0f });
	auto bar3 = std::make_unique<ProgressBar>();
	bar3->SetSize(width - 2 * kPageMargin, 20);
	bar3->SetStyle(ProgressBarStyleOverride{ .trackColor = kCardLayer3(), .fillColor = kAccentOrange() });

	auto targetLabel = std::make_unique<Label>("Target: 0%");
	targetLabel->SetSize(width - 2 * kPageMargin, 24);
	targetLabel->SetTextColor(kTitle());

	// 三 bar 共享目标值——堆分配（页面函数返回后回调仍安全）；裸指针树内稳定
	auto progress = std::make_shared<int>(0);
	ProgressBar* bar1Ptr = bar1.get();
	ProgressBar* bar2Ptr = bar2.get();
	ProgressBar* bar3Ptr = bar3.get();
	Label* targetPtr = targetLabel.get();
	auto refresh = [progress, bar1Ptr, bar2Ptr, bar3Ptr, targetPtr]{
		bar1Ptr->SetPercent(*progress);
		bar2Ptr->SetPercent(*progress);
		bar3Ptr->SetPercent(*progress);
		targetPtr->SetText("Target: " + std::to_string(*progress) + "%");
	};

	// 按钮行（HorizontalLayout 排 4 键——140×4 = 560 ≤ 624）
	auto btnRow = std::make_unique<Panel>();
	btnRow->SetSize(width - 2 * kPageMargin, 44);
	btnRow->SetLayout(std::make_unique<HorizontalLayout>());

	auto btnMinus = std::make_unique<Button>("-20%");
	btnMinus->SetSize(140, 40);
	btnMinus->SetStyle(ButtonStyleOverride{ .background = kSmallBtnBg(), .cornerRadius = kCornerNormal });
	btnMinus->SetTextColor(kSmallBtnFg());
	btnMinus->SetOnClick([progress, refresh]{
		*progress = (std::max)(0, *progress - 20);   // 括号防 Windows.h min/max 宏污染
		refresh();
	});

	auto btnPlus = std::make_unique<Button>("+20%");
	btnPlus->SetSize(140, 40);
	btnPlus->SetStyle(ButtonStyleOverride{ .background = kSmallBtnBg(), .cornerRadius = kCornerNormal });
	btnPlus->SetTextColor(kSmallBtnFg());
	btnPlus->SetOnClick([progress, refresh]{
		*progress = (std::min)(100, *progress + 20);
		refresh();
	});

	auto btnRandom = std::make_unique<Button>("Random");
	btnRandom->SetSize(140, 40);
	btnRandom->SetStyle(ButtonStyleOverride{
		.background = kAccentGreen(),
		.cornerRadius = kCornerNormal,
	});
	btnRandom->SetTextColor(Color::White());
	btnRandom->SetOnClick([progress, refresh]{
		*progress = std::rand() % 101;
		refresh();
	});

	auto btnReset = std::make_unique<Button>("Reset");
	btnReset->SetSize(140, 40);
	btnReset->SetStyle(ButtonStyleOverride{
		.background = kAccentOrange(),
		.cornerRadius = kCornerNormal,
	});
	btnReset->SetTextColor(Color::White());
	btnReset->SetOnClick([progress, refresh]{
		*progress = 0;
		refresh();
	});

	btnRow->AddChild(std::move(btnMinus));
	btnRow->AddChild(std::move(btnPlus));
	btnRow->AddChild(std::move(btnRandom));
	btnRow->AddChild(std::move(btnReset));

	page->AddChild(std::move(bar1));
	page->AddChild(MakeSpacer(6));
	page->AddChild(std::move(bar2));
	page->AddChild(MakeSpacer(6));
	page->AddChild(std::move(bar3));
	page->AddChild(MakeSpacer(10));
	page->AddChild(std::move(targetLabel));
	page->AddChild(MakeSpacer(10));
	page->AddChild(std::move(btnRow));

	page->AddChild(MakeSpacer(14));
	page->AddChild(MakeHint("CollapsiblePanel：折叠高度 0↔130 动画（EaseOut/EaseIn 200ms）", width));
	page->AddChild(MakeSpacer(10));

	auto toggle = std::make_unique<Button>("Toggle Panel");
	toggle->SetSize(240, 36);
	toggle->SetStyle(ButtonStyleOverride{ .background = kSmallBtnBg(), .cornerRadius = kCornerNormal });
	toggle->SetTextColor(kSmallBtnFg());

	auto coll = std::make_unique<CollapsiblePanel>();
	coll->SetSize(480, 130);
	auto collText = std::make_unique<Label>("Collapsible content: height animation 0↔130");
	collText->SetPosition(10, 10);
	collText->SetSize(460, 30);
	collText->SetTextColor(kTitle());
	coll->GetContent()->AddChild(std::move(collText));

	CollapsiblePanel* collPtr = coll.get();
	toggle->SetOnClick([collPtr]{ collPtr->Toggle(); });

	page->AddChild(std::move(toggle));

	page->AddChild(MakeSpacer(8));
	page->AddChild(MakeHint("窗口 2 ProgressBar demo 三色联动交互原样迁入（窗口 2 已退役并入本页）", width));

	// CollapsiblePanel 放页面最后——VerticalLayout 按收起态高度 0 排后续元素，
	// 展开动画会长高覆盖下方元素；最后子节点无后续元素
	page->AddChild(std::move(coll));
	return page;
}

// ── 页面：Rendering ─────────────────────────────────────────────────────

std::unique_ptr<Panel> BuildRenderingPage(int width, int height){
	auto page = std::make_unique<Panel>();
	page->SetSize(width, height);   // 透明容器占位尺寸（不画背景——Panel 透明语义；子控件相对本页定位）
	page->SetLayout(std::make_unique<VerticalLayout>());

	page->AddChild(MakePageTitle("Rendering", width));
	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("DrawRoundedRect 被 Button / ProgressBar 消费——示例见 Buttons / Animation 页（Phase 8）", width));
	page->AddChild(MakeSpacer(10));

	page->AddChild(MakeHint("AlphaBlend：半透明合成（9.5 后端层，预乘 BGRA + AC_SRC_ALPHA）", width));
	page->AddChild(MakeSpacer(10));

	auto translucent = std::make_unique<Button>("Translucent Button");
	translucent->SetSize(300, 60);
	translucent->SetStyle(ButtonStyleOverride{
		.background = Color::FromRGBA8(0, 255, 220, 60),
		.cornerRadius = 40.0f,
	});
	translucent->SetTextColor(Color::White());
	page->AddChild(std::move(translucent));

	page->AddChild(MakeSpacer(14));
	page->AddChild(MakeHint("PushClip：子控件超出父边界被矩形裁切（9.5 R1）", width));
	page->AddChild(MakeSpacer(10));

	// Clip 可观测例：Label 起点贴近卡片右缘、文本向右溢出 → 尾部被父边界裁掉
	auto clipPanel = std::make_unique<Panel>();
	clipPanel->SetSize(width - 2 * kPageMargin, 80);
	clipPanel->SetStyle(PanelStyleOverride{ .background = kInnerCard() });
	auto overflow = std::make_unique<Label>(
		"This long label starts near the right edge and overflows far beyond the card — the tail is clipped by the parent's clip bounds.");
	overflow->SetPosition(width - 2 * kPageMargin - 194, 28);   // 起点在卡片右缘附近——文本向右溢出
	overflow->SetSize(400, 24);
	overflow->SetTextColor(kTitle());
	clipPanel->AddChild(std::move(overflow));
	page->AddChild(std::move(clipPanel));

	page->AddChild(MakeSpacer(12));
	page->AddChild(MakeHint("能力清单：DrawRect / DrawRoundedRect / DrawLine / DrawImage / PushClip / PopClip / DrawFocusRect（Phase 4 + Phase 8）", width));
	return page;
}

}   // namespace Demo

}   // namespace ECDI
