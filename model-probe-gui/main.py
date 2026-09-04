#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""模型探测工具 GUI 版
通过 BaseURL + API Key 查询该 key 可调用的模型，勾选后导出 JSON 配置。
特性：直连 API（无 CORS 限制）、Key 存 Windows 凭据管理器、历史配置、一键测试模型。
用法：python main.py
"""
import json
import re
import sys
import time
import urllib.error
import urllib.request

import keyring
from PySide6.QtCore import QObject, QSettings, QThread, Signal
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QHBoxLayout, QLabel,
    QLineEdit, QListWidget, QListWidgetItem, QMainWindow, QMessageBox,
    QPlainTextEdit, QPushButton, QRadioButton, QVBoxLayout, QWidget,
)

SERVICE = "ModelProbe"
TIMEOUT = 20


# ---------- 凭据清理（防御性：清除历史残留） ----------
def delete_cred(base):
    try:
        keyring.delete_password(SERVICE, base)
    except Exception:
        pass


# ---------- 网络层 ----------
def build_candidates(base):
    """已带 /v1 风格路径则直接用；否则同时尝试根路径与 /v1"""
    b = (base or "").strip().rstrip("/")
    if re.search(r"/v\d+$", b):
        return [b]
    return [b, b + "/v1"]


def http_json(base, key, path, method="GET", body=None):
    url = base + path
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": f"Bearer {key}",
        "Content-Type": "application/json",
        "Accept": "application/json",
    })
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            text = resp.read().decode("utf-8", "replace")
            return resp.status, (json.loads(text) if text.strip() else {})
    except urllib.error.HTTPError as e:
        try:
            text = e.read().decode("utf-8", "replace")
            detail = json.loads(text) if text.strip() else {}
        except Exception:
            detail = {}
        return e.code, detail
    except Exception as e:
        raise ConnectionError(str(e))


def fetch_models(base, key):
    for b in build_candidates(base):
        try:
            status, payload = http_json(b, key, "/models")
        except ConnectionError as e:
            return {"ok": False, "message": f"无法连接 {b}/models：{e}"}
        if status == 200:
            data = payload.get("data") if isinstance(payload, dict) else None
            if isinstance(data, list):
                return {"ok": True, "base": b, "count": len(data), "data": data}
            return {"ok": False, "message": "返回格式异常（缺少 data 数组）"}
        if status in (401, 403):
            return {"ok": False, "message": f"认证失败（HTTP {status}）：API Key 无效或没有权限"}
        if status == 404:
            continue
        return {"ok": False, "message": f"服务端返回 HTTP {status}"}
    return {"ok": False, "message": "未找到 /models 端点，请检查 BaseURL 是否正确"}


def test_model(base, key, model_id):
    for b in build_candidates(base):
        try:
            status, payload = http_json(b, key, "/chat/completions", method="POST", body={
                "model": model_id,
                "messages": [{"role": "user", "content": "ping"}],
                "max_tokens": 1,
                "stream": False,
            })
        except ConnectionError as e:
            return {"ok": False, "id": model_id, "message": f"无法连接：{e}"}
        if status == 200:
            return {"ok": True, "id": model_id, "message": "调用成功"}
        if status in (401, 403):
            return {"ok": False, "id": model_id, "message": f"认证失败（HTTP {status}）"}
        if status == 404:
            continue
        msg = payload.get("error", {}).get("message") if isinstance(payload, dict) else None
        return {"ok": False, "id": model_id, "message": msg or f"HTTP {status}"}
    return {"ok": False, "id": model_id, "message": "未找到 /chat/completions 端点"}


# ---------- 后台任务 ----------
class NetWorker(QObject):
    finished = Signal(dict)

    def fetch_models(self, base, key):
        self.finished.emit(fetch_models(base, key))

    def test_models(self, base, key, ids):
        results = []
        for mid in ids:
            results.append(test_model(base, key, mid))
        self.finished.emit({"kind": "test", "results": results})


# ---------- 模型行控件 ----------
class ModelItem(QWidget):
    def __init__(self, model, checked, on_toggle):
        super().__init__()
        self.id_text = str(model.get("id", ""))
        self.cb = QCheckBox()
        self.cb.setChecked(checked)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(10, 4, 10, 4)
        lay.setSpacing(10)
        id_label = QLabel(self.id_text)
        id_label.setStyleSheet("font-family: Consolas, 'Courier New', monospace; font-size: 13px;")
        meta = ""
        if model.get("owned_by"):
            meta = "owned by " + str(model["owned_by"])
        elif model.get("created"):
            meta = time.strftime("%Y-%m-%d", time.localtime(model["created"]))
        meta_label = QLabel(meta)
        meta_label.setStyleSheet("color: #9aa3b2; font-size: 12px;")
        lay.addWidget(self.cb)
        lay.addWidget(id_label, 1)
        lay.addWidget(meta_label)
        self.cb.toggled.connect(on_toggle)


# ---------- 主窗口 ----------
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("模型探测工具")
        self.resize(920, 700)
        self.models = []
        self.selected = set()
        self.worker = None
        self.thread = None
        self.settings = QSettings("ModelProbe", "ModelProbe")
        self._build_ui()
        self._load_prefs()

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(14, 14, 14, 14)
        root.setSpacing(10)

        title = QLabel("模型探测工具")
        title.setStyleSheet("font-size: 18px; font-weight: 600;")
        sub = QLabel("输入 BaseURL 与 API Key，查看该 key 可调用的模型，勾选后导出 JSON 配置")
        sub.setStyleSheet("color: #9aa3b2;")
        root.addWidget(title)
        root.addWidget(sub)

        root.addWidget(QLabel("BaseURL"))
        base_row = QHBoxLayout()
        self.base_combo = QComboBox()
        self.base_combo.setEditable(True)
        self.base_combo.setPlaceholderText("https://api.openai.com/v1 或 https://api.deepseek.com")
        self.base_combo.setMinimumHeight(32)
        self.clear_btn = QPushButton("清理历史与 Key")
        self.clear_btn.setProperty("class", "ghost")
        base_row.addWidget(self.base_combo, 1)
        base_row.addWidget(self.clear_btn)
        root.addLayout(base_row)

        root.addWidget(QLabel("API Key"))
        key_row = QHBoxLayout()
        self.key_edit = QLineEdit()
        self.key_edit.setEchoMode(QLineEdit.Password)
        self.key_edit.setPlaceholderText("sk-...")
        self.key_edit.setMinimumHeight(32)
        self.eye_btn = QPushButton("显示")
        self.eye_btn.setProperty("class", "ghost")
        self.eye_btn.setFixedWidth(56)
        key_row.addWidget(self.key_edit, 1)
        key_row.addWidget(self.eye_btn)
        root.addLayout(key_row)

        btn_row = QHBoxLayout()
        self.query_btn = QPushButton("查询模型")
        self.test_btn = QPushButton("测试所选")
        self.test_btn.setProperty("class", "ghost")
        self.test_btn.setEnabled(False)
        btn_row.addWidget(self.query_btn)
        btn_row.addWidget(self.test_btn)
        btn_row.addStretch(1)
        root.addLayout(btn_row)

        list_bar = QHBoxLayout()
        self.stat_label = QLabel("共 0 个模型 · 已选 0")
        self.stat_label.setStyleSheet("color: #9aa3b2;")
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("搜索模型…")
        self.search_edit.setFixedWidth(220)
        self.search_edit.setMinimumHeight(30)
        self.all_btn = QPushButton("全选")
        self.all_btn.setProperty("class", "ghost")
        self.none_btn = QPushButton("清空")
        self.none_btn.setProperty("class", "ghost")
        list_bar.addWidget(self.stat_label)
        list_bar.addStretch(1)
        list_bar.addWidget(self.search_edit)
        list_bar.addWidget(self.all_btn)
        list_bar.addWidget(self.none_btn)
        root.addLayout(list_bar)

        self.list_widget = QListWidget()
        root.addWidget(self.list_widget, 1)

        fmt_row = QHBoxLayout()
        fmt_row.addWidget(QLabel("导出格式"))
        self.fmt_ids = QRadioButton("仅 ID 数组")
        self.fmt_full = QRadioButton("对象数组")
        self.fmt_cfg = QRadioButton("配置格式（含 base_url）")
        self.fmt_ids.setChecked(True)
        fmt_row.addWidget(self.fmt_ids)
        fmt_row.addWidget(self.fmt_full)
        fmt_row.addWidget(self.fmt_cfg)
        fmt_row.addStretch(1)
        root.addLayout(fmt_row)

        act_row = QHBoxLayout()
        self.gen_btn = QPushButton("生成 JSON")
        self.export_btn = QPushButton("导出到文件…")
        self.export_btn.setProperty("class", "ghost")
        act_row.addWidget(self.gen_btn)
        act_row.addWidget(self.export_btn)
        act_row.addStretch(1)
        root.addLayout(act_row)

        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.preview.setPlaceholderText("勾选模型后点击「生成 JSON」，这里显示可用的 JSON 内容…")
        self.preview.setMinimumHeight(130)
        root.addWidget(self.preview)

        self.statusBar().showMessage("就绪")

        self.query_btn.clicked.connect(self.on_query)
        self.test_btn.clicked.connect(self.on_test)
        self.eye_btn.clicked.connect(self.toggle_key_visible)
        self.search_edit.textChanged.connect(self.apply_filter)
        self.all_btn.clicked.connect(self.select_all)
        self.none_btn.clicked.connect(self.clear_all)
        self.gen_btn.clicked.connect(self.on_generate)
        self.export_btn.clicked.connect(self.on_export)
        self.key_edit.returnPressed.connect(self.on_query)
        self.base_combo.lineEdit().returnPressed.connect(self.on_query)
        self.clear_btn.clicked.connect(self.on_clear_history)

    # ---------- 偏好与凭据 ----------
    def _load_prefs(self):
        try:
            recent = list(self.settings.value("recent_bases", []))
        except Exception:
            recent = []
        for b in recent:
            self.base_combo.addItem(b)
        if recent:
            self.base_combo.setCurrentIndex(0)

    def _save_history(self, base):
        try:
            recent = list(self.settings.value("recent_bases", []))
        except Exception:
            recent = []
        recent = [b for b in recent if b != base]
        recent.insert(0, base)
        recent = recent[:10]
        self.settings.setValue("recent_bases", recent)
        self.base_combo.blockSignals(True)
        self.base_combo.clear()
        self.base_combo.addItems(recent)
        self.base_combo.setCurrentIndex(0)
        self.base_combo.blockSignals(False)

    def on_clear_history(self):
        ret = QMessageBox.question(
            self, "确认清理",
            "将删除所有历史 BaseURL 记录及可能残留的凭据（不可恢复），确定继续吗？",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if ret != QMessageBox.Yes:
            return
        recent = []
        try:
            recent = list(self.settings.value("recent_bases", []))
        except Exception:
            recent = []
        for b in recent:
            delete_cred(b)
        self.settings.setValue("recent_bases", [])
        self.base_combo.clear()
        self.key_edit.clear()
        self.statusBar().showMessage(f"已清理 {len(recent)} 条历史记录")

    # ---------- 后台任务 ----------
    def _start_task(self, fn):
        self.thread = QThread(self)
        self.worker = NetWorker()
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(fn)
        self.worker.finished.connect(self.on_worker_done)
        self.worker.finished.connect(self.thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(self.thread.deleteLater)
        self.thread.start()

    def _set_busy(self, busy, msg):
        self.query_btn.setEnabled(not busy)
        self.test_btn.setEnabled(not busy and bool(self.selected))
        self.base_combo.setEnabled(not busy)
        self.key_edit.setEnabled(not busy)
        self.query_btn.setText("查询中…" if busy else "查询模型")
        if busy:
            self.statusBar().showMessage(msg)

    # ---------- 查询 ----------
    def on_query(self):
        base = self.base_combo.currentText().strip()
        key = self.key_edit.text().strip()
        if not base or not key:
            self.statusBar().showMessage("请同时填写 BaseURL 和 API Key")
            return
        self._set_busy(True, "正在查询模型…")
        self._start_task(lambda: self.worker.fetch_models(base, key))

    def on_worker_done(self, result):
        self._set_busy(False, "")
        if result.get("kind") == "test":
            self._show_test_results(result["results"])
            return
        if not result.get("ok"):
            self.statusBar().showMessage(result.get("message", "查询失败"))
            QMessageBox.warning(self, "查询失败", result.get("message", "未知错误"))
            return
        self.models = result["data"]
        self.selected = set()
        self._render_items()
        base = self.base_combo.currentText().strip()
        self._save_history(base)
        self.statusBar().showMessage(f"成功获取 {result['count']} 个模型（{result['base']}）")

    # ---------- 列表交互 ----------
    def _render_items(self):
        self.list_widget.clear()
        for i, m in enumerate(self.models):
            item = QListWidgetItem()
            self.list_widget.addItem(item)
            widget = ModelItem(m, False, lambda checked, idx=i: self._on_toggle(idx, checked))
            item.setSizeHint(widget.sizeHint())
            self.list_widget.setItemWidget(item, widget)
        self._update_stat()
        self.apply_filter()

    def _on_toggle(self, idx, checked):
        if checked:
            self.selected.add(idx)
        else:
            self.selected.discard(idx)
        self._update_stat()

    def _update_stat(self):
        self.stat_label.setText(f"共 {len(self.models)} 个模型 · 已选 {len(self.selected)}")
        self.test_btn.setEnabled(bool(self.selected))
        self.test_btn.setText(f"测试所选（{len(self.selected)}）")

    def apply_filter(self):
        kw = self.search_edit.text().strip().lower()
        for i in range(self.list_widget.count()):
            item = self.list_widget.item(i)
            wid = self.list_widget.itemWidget(item)
            hidden = bool(kw) and (wid is None or kw not in wid.id_text.lower())
            item.setHidden(hidden)

    def select_all(self):
        for i in range(self.list_widget.count()):
            item = self.list_widget.item(i)
            if item.isHidden():
                continue
            wid = self.list_widget.itemWidget(item)
            if wid is not None:
                wid.cb.setChecked(True)

    def clear_all(self):
        for i in range(self.list_widget.count()):
            wid = self.list_widget.itemWidget(self.list_widget.item(i))
            if wid is not None:
                wid.cb.setChecked(False)

    # ---------- 导出 ----------
    def _build_json(self):
        picked = sorted(self.selected)
        if self.fmt_full.isChecked():
            fmt = "full"
        elif self.fmt_cfg.isChecked():
            fmt = "config"
        else:
            fmt = "ids"
        if fmt == "ids":
            obj = [self.models[i]["id"] for i in picked]
        elif fmt == "full":
            obj = [{"id": self.models[i]["id"],
                    "created": self.models[i].get("created"),
                    "owned_by": self.models[i].get("owned_by")} for i in picked]
        else:
            obj = {"base_url": self.base_combo.currentText().strip(),
                   "models": [self.models[i]["id"] for i in picked]}
        return json.dumps(obj, ensure_ascii=False, indent=2)

    def on_generate(self):
        if not self.selected:
            self.statusBar().showMessage("请先勾选要导出的模型")
            return
        self.preview.setPlainText(self._build_json())
        self.statusBar().showMessage("JSON 已生成")

    def on_export(self):
        if not self.selected:
            self.statusBar().showMessage("请先勾选要导出的模型")
            return
        text = self._build_json()
        path, _ = QFileDialog.getSaveFileName(self, "导出 JSON", "models.json", "JSON 文件 (*.json)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        self.statusBar().showMessage(f"已导出：{path}")

    # ---------- 一键测试 ----------
    def on_test(self):
        if not self.selected:
            return
        base = self.base_combo.currentText().strip()
        key = self.key_edit.text().strip()
        if not base or not key:
            self.statusBar().showMessage("请先填写 BaseURL 和 API Key")
            return
        ids = [self.models[i]["id"] for i in sorted(self.selected)]
        self._set_busy(True, f"正在测试 {len(ids)} 个模型…")
        self._start_task(lambda: self.worker.test_models(base, key, ids))

    def _show_test_results(self, results):
        lines = []
        ok_n = sum(1 for r in results if r["ok"])
        for r in results:
            mark = "OK" if r["ok"] else "FAIL"
            lines.append(f"[{mark}] {r['id']} — {r['message']}")
        self.preview.setPlainText("\n".join(lines))
        self.statusBar().showMessage(f"测试完成：{ok_n}/{len(results)} 成功")

    def toggle_key_visible(self):
        showing = self.key_edit.echoMode() == QLineEdit.Normal
        self.key_edit.setEchoMode(QLineEdit.Normal if not showing else QLineEdit.Password)
        self.eye_btn.setText("隐藏" if not showing else "显示")


QSS = """
QWidget { background-color: #0f1115; color: #e6e9ef; font-size: 13px; }
QLineEdit, QComboBox, QPlainTextEdit {
    background-color: #1c212b; border: 1px solid #2a3140; border-radius: 6px;
    padding: 6px 8px; selection-background-color: #2f7fd9;
}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #4f9cf7; }
QComboBox QAbstractItemView { background-color: #1c212b; border: 1px solid #2a3140; }
QPushButton {
    background-color: #2f7fd9; color: #ffffff; border: none; border-radius: 6px;
    padding: 7px 16px; font-weight: 500;
}
QPushButton:hover { background-color: #4f9cf7; }
QPushButton:disabled { background-color: #232936; color: #6b7280; }
QPushButton[class="ghost"] { background-color: transparent; border: 1px solid #2a3140; color: #e6e9ef; }
QPushButton[class="ghost"]:hover { border-color: #4f9cf7; color: #4f9cf7; }
QListWidget { background-color: #161a21; border: 1px solid #2a3140; border-radius: 8px; }
QListWidget::item { border-bottom: 1px solid #1e2530; }
QRadioButton, QCheckBox { color: #e6e9ef; spacing: 6px; }
QStatusBar { color: #9aa3b2; }
QScrollBar:vertical { background: #161a21; width: 10px; }
QScrollBar::handle:vertical { background: #2a3140; border-radius: 5px; min-height: 24px; }
"""


def main():
    app = QApplication(sys.argv)
    app.setStyleSheet(QSS)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
