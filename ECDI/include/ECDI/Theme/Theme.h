#pragma once

#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/CheckBoxStyle.h"
#include "ECDI/Theme/RadioStyle.h"

namespace ECDI{

/// @brief 主题抽象基类（默认视觉规范提供者——Style 是唯一视觉属性来源）
/// @details Phase 9 决策层：只回答"默认应该长什么样"，不参与绘制（能力/决策正交——Phase 8 提供能力）。
/// 返回值语义（非引用）：Theme 是规范提供者而非 Style 生命周期管理器——实现可自由组织内部数据。
/// 6.2：CheckBoxStyle/RadioStyle 加入（Phase 9 v1.4 预留注释兑现）。
class Theme{
public:
	virtual ~Theme() = default;

	virtual TextStyle    GetTextStyle() const = 0;    ///< 文本控件共享样式
	virtual ButtonStyle  GetButtonStyle() const = 0;
	virtual TextBoxStyle GetTextBoxStyle() const = 0;
	virtual PanelStyle   GetPanelStyle() const = 0;
	virtual CheckBoxStyle GetCheckBoxStyle() const = 0;   ///< 6.2 CheckBox 专属样式
	virtual RadioStyle    GetRadioStyle() const = 0;      ///< 6.2 Radio 专属样式
};

}
