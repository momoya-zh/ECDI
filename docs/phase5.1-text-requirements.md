# Phase5 文本系统职责确认（phase5.1-text-requirements.md）

> 阶段：Phase 5（文本与控件可用性，v0.1 → v1.0 的核心构成）
> 文档类型：职责确认（只记边界与决策点，不展开接口设计——接口在初步设计固化）
> 修订记录：v1.0（2026-08-12）

## 1. 阶段定位

Phase 5 目标：让框架"可供人使用"——用框架 API 能做出一个像样的小应用（有文本、能输入、能点击、能布局、能重绘）。**v1.0 判据即此，与"控件数量"无关**。

子模块顺序（已锁定）：
`5.1 文本系统（本文档）→ 5.2 Label → 5.3 Button 完整化 → 5.4 交互基础设施（Tab 焦点 + 焦点视觉态 + Mouse Capture）→ 5.5 TextBox 基础 → 5.6 IME（v0.1 可裁剪）`

## 2. 5.1 文本系统边界

### 做什么
- 文本数据进入命令体系：DrawTextCommand（variant 扩展）
- Font 最小模型（字号 + 字体族默认）
- RenderingBackend 增加文本绘制操作（操作粒度，认识公共类型）
- GDIBackend 的 GDI 文本实现
- 文本测量能力（载体未锁，见 D5）
- Button/Label SetText UTF-8 迁移收尾

### 不做什么（边界）
- ❌ 多行排版 / 换行 / 文本块（D6：单行起步）
- ❌ 字体池 / 字体资源管理（D3：Font 值拷贝）
- ❌ IME / 中文输入法组合（归 5.6，v0.1 可裁剪）
- ❌ 控件层对齐策略（D9：对齐是控件职责，Backend 不做排版）
- ❌ Auto Size 布局系统（控件自动尺寸由控件自己调测量，非系统级）

## 3. 决策记录

### 核心决策

**D1 文本存储编码 —— 结论：A（UTF-8 全链路）**

公共层（Widget 存储 / 命令数据 / 控件 API）统一 UTF-8 `std::string`；GDIBackend 画字时才转 UTF-16（边界转换，字符串既定方向收尾）。避免控件存 wstring、命令存 string 的双重转换。

**D2 Font 模型形态 —— 结论：A（最小 Font 描述符）**

字号（float）+ 字体族（默认系统字体，第一版可传默认）。Font 作为概念先建立（未来 TextBox/控件都引用它），字段后续可加；不做完整 Font 对象（family/weight/style 全量）——过度设计。

**D3 DrawTextCommand 数据形态 —— 结论：A（死数据，Font 值拷贝）**

`{ Point pos; std::string text; Color color; Font font; }`——Font 是轻量描述符，值拷贝符合"命令是死数据"原则（Renderer/Backend 不认识 Widget）；不做字体池/句柄引用。

**D4 RenderingBackend 接口扩展 —— 结论：A（展开参数，操作粒度）**

`DrawText(pos, text, color, font)` 展开参数，与 `DrawRect(Rect, Color)` 风格一致；Backend 不认识命令结构（决策 11/14 操作粒度不变）。

**D5 文本测量 —— 能力确定，载体未锁**

第一版**必须**提供文本测量能力（5.2 Label 自动尺寸 / 5.3 Button 文字居中直接依赖；GDI `GetTextExtentPoint32W` 可实现）。
⚠️ **"Font 提供 Measure" 这一职责表述暂不锁死**——测量能力挂在谁身上（Font 方法 / 独立测量入口）由初步设计定。

### 范围决策

**D6 第一版文本范围 —— 结论：A（单行起步）**

所有控件文本单行；多行是排版系统的事（换行/行高/溢出），未来独立设计。与 5.5 TextBox 基础功能匹配。

**D7 GDIBackend 文本实现 —— 结论：A（TextOutW）**

单行、起点绘制、对齐由控件自算；DrawTextW 自带对齐/换行偏排版语义，第一版不需要。

**D8 SetText UTF-8 迁移 —— 结论：A（本次一并迁移）**

Button/Label 的 `SetText`/`m_text` 改 `std::string`，main.cpp 调用改窄字面量——"公共 API UTF-8"目标收尾（字符串暂缓项结清）。

**D9 对齐方式 —— 结论：A（控件算偏移，Backend 不做排版）**

命令只带起点 Point；Label 居中 / Button 文字居中由控件调测量自算偏移。对齐是布局语义（控件职责），Backend 只画"在给定位置"。

## 4. 待初步设计固化

- D5 测量能力的载体（Font 方法 vs 独立测量入口）
- Font 描述符具体形态（默认字体族的表达：空串 = 系统默认？）
- DrawTextCommand 与 Backend::DrawText 的映射（Renderer 执行转发）
- 测量与绘制是否共享 Font 资源（GDI HFONT 生命周期：创建/缓存/销毁）
- 单行文本的垂直对齐（第一版垂直居中？）

## 5. 修订记录

- v1.0（2026-08-12）：职责确认，9 项决策落盘（D5 测量能力锁定、载体未锁，待初步设计）
