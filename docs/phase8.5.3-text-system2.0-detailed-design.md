
# Phase 8.5.3 文本系统 2.0 详细设计（Undo/Redo）

> 状态：v0.1（2026-08-24）｜草案待实施前定稿
> 前序：Phase 8.5.2 定稿 / 8.5.1 完结 ✅（commit 8ab8300）
> 相关：phase8.5-text-system2.0-preliminary-design.md（B6 Undo Snapshot / C3 Composition 与 Undo / C4 Push 时机）
> 拆分说明：本文件 = 8.5.3 专属详细设计（原 phase8.5-text-system2.0-detailed-design.md §10 拆出）

---

## 10. 核心设计方向（草案）

- **Snapshot（B6）**：`struct UndoSnapshot { std::string text; size_t caret; SelectionRange selection; float scrollOffsetY; }`
- **Push 时机（C4）**：编辑操作**前** push 当前状态 → 执行修改 → clear redo；仅文本内容改变产生（输入/删除/粘贴/剪切/IME Commit）；纯光标/Selection/Scroll 不产生
- **Composition（C3）**：首次 UpdateComposition（组合开始）push 一次；过程不 push；Commit 自然并入该快照（Ctrl+Z 一次撤销整个组合输入）
- **栈**：m_undoStack/m_redoStack + m_maxUndoDepth=100
- **快捷键**：Ctrl+Z Undo / Ctrl+Y Redo（OnKeyDown Ctrl 分支扩展）
- **边界**：空栈 no-op；Undo 后 m_text/caret/selection/scroll 全部恢复 + Invalidate + SyncTextInputCaret + RaiseTextChanged

## 11. 修订记录

- v0.1（2026-08-24）草案：Undo/Redo 方向（Snapshot / Push 时机 C4 / Composition 与 Undo C3 / 快捷键 Ctrl+Z+Y）。
