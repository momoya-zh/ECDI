#include "RunAllTests.h"
#include "TestFramework.h"

#include "../../examples/ModelProbe/ModelProbe.h"   // 2026-09-03：demo 移 examples/（原 ../Demo/ 废弃）；测试文件留框架侧

#include "ECDI/Platform/ChildProcess.h"

#include <memory>
#include <string>
#include <vector>

using namespace ECDI;

namespace{

// ── P1：ModelProbePage 流程测试（注入 fake ChildProcess——RecordingBackend 式命令断言）──

/// @brief fake 子进程：测试脚本注入响应流，记录写入命令（详设 §8 用例 17-21）
/// @note 初始 running=false（模拟进程未启动——StartFetch 的 !IsRunning() 分支才会真正 Start）；
/// SetResponses 重置 readIndex（每次「新阶段脚本」从头读——TestFlow 覆盖响应流的正确方式）
class FakeChildProcess : public ChildProcess{
public:
	bool Start(const std::string&, const std::vector<std::string>&) override{ started = true; running = true; return true; }
	bool IsRunning() const override{ return running; }
	bool WriteLine(const std::string& line) override{ written.push_back(line); return true; }
	std::string ReadAvailable() override{
		if (readIndex >= responses.size())
			return {};
		return responses[readIndex++];
	}
	void CloseInput() override{ closed = true; }
	bool WaitForExit(unsigned int) override{ return true; }
	void Terminate() override{ running = false; }

	void SetResponses(std::vector<std::string> rs){
		responses = std::move(rs);
		readIndex = 0;
	}

	std::vector<std::string> responses;   ///< 每次 ReadAvailable 返回一段（可分包）
	std::vector<std::string> written;     ///< 收到的命令行
	size_t readIndex = 0;
	bool started = false;
	bool running = false;   ///< 初始未启动——Start 后 true（模拟进程生命周期）
	bool closed = false;
};

/// @brief 建页面 + 注入 fake（返回 fake 裸指针供断言）
/// @note 返回 unique_ptr——Widget 家族禁复制禁移动（资源类铁律），页面只能指针传递
std::unique_ptr<Demo::ModelProbePage> MakePage(FakeChildProcess*& fakeOut){
	auto fake = std::make_unique<FakeChildProcess>();
	fakeOut = fake.get();
	auto page = std::make_unique<Demo::ModelProbePage>(std::move(fake));
	page->SetBaseUrl("https://api.longcat.chat/openai/v1");
	page->SetApiKey("sk-test");
	return page;
}

void TestModelProbeFetchFlow()
{
	// FetchFlow：OK/MODEL×2/DONE → 列表行数 + 统计正确 + busy 还原
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	fake->SetResponses({
		"OK\tFETCH\thttps://api.longcat.chat/openai/v1\t2\n"
		"MODEL\tLongCat-2.0\towned by LongCat\n"
		"MODEL\tdeepseek-chat\t\n"
		"DONE\tFETCH\n",
	});
	page->OnQueryClick();
	EXPECT_TRUE(fake->started);
	EXPECT_EQ(fake->written.size(), 1);
	EXPECT_TRUE(fake->written[0].rfind("FETCH https://api.longcat.chat/openai/v1 sk-test", 0) == 0);
	EXPECT_TRUE(page->IsBusy());   // 在途
	page->PollProbe();
	EXPECT_FALSE(page->IsBusy());  // DONE → 收尾
	EXPECT_EQ(page->GetModelCount(), 2);
	EXPECT_EQ(page->GetSelectedCount(), 0);
}

void TestModelProbeFetchFragmented()
{
	// FetchFragmentedOutput（GPT 评审强推）：分包喂 OK\nMOD / EL\t...\nDONE\n →
	// 解析出 OK/MODEL/DONE（管道读取边界 ≠ 协议消息边界）
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	fake->SetResponses({
		"OK\tFETCH\thttps://x\t1\nMOD",
		"EL\tgpt-4\towned by openai\nDONE\tFETCH\n",
	});
	page->OnQueryClick();
	page->PollProbe();   // 第一次：半行缓冲
	EXPECT_TRUE(page->IsBusy());   // 未 DONE——MODEL 行未完整
	page->PollProbe();   // 第二次：补全 + DONE
	EXPECT_FALSE(page->IsBusy());
	EXPECT_EQ(page->GetModelCount(), 1);
}

void TestModelProbeFetchError()
{
	// FetchError：ERR → busy 还原 + 列表空
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	fake->SetResponses({ "ERR\tFETCH\t认证失败（HTTP 401）：API Key 无效或没有权限\n" });
	page->OnQueryClick();
	page->PollProbe();
	EXPECT_FALSE(page->IsBusy());
	EXPECT_EQ(page->GetModelCount(), 0);
}

void TestModelProbeTestFlow()
{
	// TestFlow：FETCH 成功后勾选 → TEST → TEST 行收集 → DONE 汇总写预览
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	fake->SetResponses({
		"OK\tFETCH\thttps://x\t2\nMODEL\tm1\t\nMODEL\tm2\t\nDONE\tFETCH\n",
	});
	page->OnQueryClick();
	page->PollProbe();
	EXPECT_EQ(page->GetModelCount(), 2);

	page->SelectModel(0, true);
	EXPECT_EQ(page->GetSelectedCount(), 1);

	fake->SetResponses({
		"OK\tTEST\t1\nTEST\tm1\tOK\t调用成功\nDONE\tTEST\n",
	});
	page->OnTestClick();
	EXPECT_TRUE(page->IsBusy());
	page->PollProbe();
	EXPECT_FALSE(page->IsBusy());
	EXPECT_TRUE(page->GetPreviewText().find("[OK] m1 — 调用成功") != std::string::npos);
}

void TestModelProbeBusyGuard()
{
	// BusyGuard：Fetching 中再 OnQueryClick → 无第二次 FETCH 写入（防重入）
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	fake->SetResponses({});   // 空响应——永远在途
	page->OnQueryClick();
	EXPECT_TRUE(page->IsBusy());
	page->OnQueryClick();   // 防重入
	page->PollProbe();      // 空数据——仍在途（fake IsRunning true）
	EXPECT_TRUE(page->IsBusy());
	EXPECT_EQ(fake->written.size(), 1);   // 只有一条 FETCH
}

void TestModelProbeShutdown()
{
	// Shutdown：CloseInput（stdin EOF = 退出信号）+ WaitForExit（无窗口——StopTimer 防御空窗口）
	FakeChildProcess* fake = nullptr;
	auto page = MakePage(fake);
	page->ShutdownBackend();
	EXPECT_TRUE(fake->closed);      // CloseInput 已调（EOF 契约）
	// 幂等：再调无害（fake WaitForExit 恒 true）
	page->ShutdownBackend();
	EXPECT_TRUE(fake->closed);
}

void TestTsvParse()
{
	// TsvParse.Basic：正常行 / 空 meta / 半行缓冲语义（SplitTsv 纯函数）
	const auto fields = Demo::SplitTsv("MODEL\tgpt-4\towned by openai");
	EXPECT_EQ(fields.size(), 3);
	EXPECT_EQ(fields[0], "MODEL");
	EXPECT_EQ(fields[1], "gpt-4");
	EXPECT_EQ(fields[2], "owned by openai");
	const auto emptyMeta = Demo::SplitTsv("MODEL\tdeepseek\t");
	EXPECT_EQ(emptyMeta.size(), 3);
	EXPECT_TRUE(emptyMeta[2].empty());
	const auto noTab = Demo::SplitTsv("DONE");
	EXPECT_EQ(noTab.size(), 1);
}

void TestJsonEscape()
{
	// JsonEscape.Quotes：`"`/`\` 转义
	EXPECT_EQ(Demo::EscapeJson("a\"b"), "a\\\"b");
	EXPECT_EQ(Demo::EscapeJson("a\\b"), "a\\\\b");
	EXPECT_EQ(Demo::EscapeJson("plain"), "plain");
}

} // anonymous namespace

void ECDI::Test::RegisterModelProbeTests()
{
	GetTestRegistry().Add("ModelProbePage.FetchFlow",             &TestModelProbeFetchFlow);
	GetTestRegistry().Add("ModelProbePage.FetchFragmentedOutput", &TestModelProbeFetchFragmented);
	GetTestRegistry().Add("ModelProbePage.FetchError",            &TestModelProbeFetchError);
	GetTestRegistry().Add("ModelProbePage.TestFlow",              &TestModelProbeTestFlow);
	GetTestRegistry().Add("ModelProbePage.BusyGuard",             &TestModelProbeBusyGuard);
	GetTestRegistry().Add("ModelProbePage.Shutdown",              &TestModelProbeShutdown);
	GetTestRegistry().Add("TsvParse.Basic",                       &TestTsvParse);
	GetTestRegistry().Add("JsonEscape.Quotes",                    &TestJsonEscape);
}
