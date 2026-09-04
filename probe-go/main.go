// ModelProbe 后端（P0，2026-09-01）
//
// 职责：ECDI GUI 的纯逻辑后端——所有 HTTP/JSON 都在这里完成，GUI 只做呈现。
// 零第三方依赖（net/http + encoding/json 标准库）。
//
// 进程模型：常驻子进程。stdin 收命令（行协议）、stdout 吐结果（TSV 行协议）、
// stdin EOF = 退出信号（GUI 关闭时只需关管道，后端自行退出）。
//
// 命令（每行一条，空白分割）：
//
//	FETCH <base> <key>              查询该 key 可调用的模型列表
//	TEST  <base> <key> <id> [id...] 逐个测试模型（POST /chat/completions）
//
// 输出（每行一条，制表符分割；动态文本已做制表符/换行清洗）：
//
//	OK\tFETCH\t<base>\t<count>
//	MODEL\t<id>\t<meta>             meta = "owned by X" | 日期(YYYY-MM-DD) | 空
//	DONE\tFETCH
//	OK\tTEST\t<count>
//	TEST\t<id>\tOK|FAIL\t<message>
//	DONE\tTEST
//	ERR\t<命令>\t<message>          失败或未知命令
//
// 网络语义（对齐原 PySide6 版 model-probe-gui/main.py）：
//
//	base 以 /v\d+ 结尾 → 只用它；否则同时尝试 base 与 base/v1（404 自动换候选）
//	FETCH: GET {base}/models；200 → data 数组；401/403 → 认证失败；其他 → HTTP 状态
//	TEST:  POST {base}/chat/completions，body = {model, ping, max_tokens:1, stream:false}
//	超时 20 秒
//
// 编译：go build -ldflags "-s -w" -o probe.exe main.go
// 部署：释放到 <exe_dir>/networkbackend/probe.exe（固定名；存在则复用，见 2026-09-01 决策）
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"regexp"
	"strings"
	"time"
)

const requestTimeout = 20 * time.Second

var versionPathRE = regexp.MustCompile(`/v\d+$`)

func main() {
	sc := bufio.NewScanner(os.Stdin)
	out := bufio.NewWriter(os.Stdout)
	for sc.Scan() {
		fields := strings.Fields(sc.Text())
		if len(fields) == 0 {
			continue
		}
		switch fields[0] {
		case "FETCH":
			if len(fields) < 3 {
				writeLine(out, "ERR", "FETCH", "usage: FETCH <base> <key>")
				continue
			}
			handleFetch(fields[1], fields[2], out)
		case "TEST":
			if len(fields) < 4 {
				writeLine(out, "ERR", "TEST", "usage: TEST <base> <key> <id> [id...]")
				continue
			}
			handleTest(fields[1], fields[2], fields[3:], out)
		default:
			writeLine(out, "ERR", "UNKNOWN", "unknown command: "+fields[0])
		}
	}
}

// writeLine 输出一行 TSV（自动清洗 + 立即 flush——管道另一端靠它做非阻塞轮询）。
func writeLine(out *bufio.Writer, parts ...string) {
	for i, p := range parts {
		if i > 0 {
			out.WriteByte('\t')
		}
		out.WriteString(tsv(p))
	}
	out.WriteByte('\n')
	out.Flush()
}

// tsv 清洗动态文本中的制表符/换行，保证一行一条记录。
func tsv(s string) string {
	s = strings.ReplaceAll(s, "\t", " ")
	s = strings.ReplaceAll(s, "\r", " ")
	s = strings.ReplaceAll(s, "\n", " ")
	return s
}

func buildCandidates(base string) []string {
	b := strings.TrimRight(strings.TrimSpace(base), "/")
	if versionPathRE.MatchString(b) {
		return []string{b}
	}
	return []string{b, b + "/v1"}
}

func handleFetch(base, key string, out *bufio.Writer) {
	for _, b := range buildCandidates(base) {
		status, payload, err := httpGetJSON(b, key, "/models")
		if err != nil {
			writeLine(out, "ERR", "FETCH", "无法连接 "+b+"/models: "+err.Error())
			return
		}
		switch {
		case status == 200:
			data, ok := payload["data"].([]any)
			if !ok {
				writeLine(out, "ERR", "FETCH", "返回格式异常（缺少 data 数组）")
				return
			}
			writeLine(out, "OK", "FETCH", b, fmt.Sprintf("%d", len(data)))
			for _, m := range data {
				obj, _ := m.(map[string]any)
				writeLine(out, "MODEL", str(obj["id"]), modelMeta(obj))
			}
			writeLine(out, "DONE", "FETCH")
			return
		case status == 401 || status == 403:
			writeLine(out, "ERR", "FETCH", fmt.Sprintf("认证失败（HTTP %d）：API Key 无效或没有权限", status))
			return
		case status == 404:
			continue
		default:
			writeLine(out, "ERR", "FETCH", fmt.Sprintf("服务端返回 HTTP %d", status))
			return
		}
	}
	writeLine(out, "ERR", "FETCH", "未找到 /models 端点，请检查 BaseURL 是否正确")
}

func handleTest(base, key string, ids []string, out *bufio.Writer) {
	writeLine(out, "OK", "TEST", fmt.Sprintf("%d", len(ids)))
	for _, id := range ids {
		ok, msg := testOne(base, key, id)
		mark := "FAIL"
		if ok {
			mark = "OK"
		}
		writeLine(out, "TEST", id, mark, msg)
	}
	writeLine(out, "DONE", "TEST")
}

func testOne(base, key, id string) (bool, string) {
	body := fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","content":"ping"}],"max_tokens":1,"stream":false}`, id)
	for _, b := range buildCandidates(base) {
		req, err := http.NewRequest(http.MethodPost, b+"/chat/completions", strings.NewReader(body))
		if err != nil {
			return false, err.Error()
		}
		req.Header.Set("Authorization", "Bearer "+key)
		req.Header.Set("Content-Type", "application/json")
		client := &http.Client{Timeout: requestTimeout}
		resp, err := client.Do(req)
		if err != nil {
			return false, "无法连接：" + err.Error()
		}
		raw, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		switch {
		case resp.StatusCode == 200:
			return true, "调用成功"
		case resp.StatusCode == 401 || resp.StatusCode == 403:
			return false, fmt.Sprintf("认证失败（HTTP %d）", resp.StatusCode)
		case resp.StatusCode == 404:
			continue
		}
		var payload map[string]any
		if len(raw) > 0 {
			_ = json.Unmarshal(raw, &payload)
		}
		if em, ok := payload["error"].(map[string]any); ok {
			if msg := str(em["message"]); msg != "" {
				return false, msg
			}
		}
		return false, fmt.Sprintf("HTTP %d", resp.StatusCode)
	}
	return false, "未找到 /chat/completions 端点"
}

func httpGetJSON(base, key, path string) (int, map[string]any, error) {
	req, err := http.NewRequest(http.MethodGet, base+path, nil)
	if err != nil {
		return 0, nil, err
	}
	req.Header.Set("Authorization", "Bearer "+key)
	req.Header.Set("Accept", "application/json")
	client := &http.Client{Timeout: requestTimeout}
	resp, err := client.Do(req)
	if err != nil {
		return 0, nil, err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	var payload map[string]any
	if len(body) > 0 {
		_ = json.Unmarshal(body, &payload)
	}
	return resp.StatusCode, payload, nil
}

func modelMeta(obj map[string]any) string {
	if s := str(obj["owned_by"]); s != "" {
		return "owned by " + s
	}
	if c, ok := obj["created"].(float64); ok && c > 0 {
		return time.Unix(int64(c), 0).Format("2006-01-02")
	}
	return ""
}

func str(v any) string {
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}
