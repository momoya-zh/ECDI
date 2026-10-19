#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "Render/BackendContract.h"
#include "Render/GDIBackend.h"
#include "ECDI/Render/Renderer.h"
#include "Platform/Win32/Win32RenderContext.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

using namespace ECDI;

namespace {

// Independent pixel-box oracle. The production helper is file-local to GDIBackend.cpp;
// T34-4's source audit separately verifies that consumers actually route through it.
struct TestPixelBox
{
	int left;
	int top;
	int right;
	int bottom;
};

TestPixelBox MakePixelBox(float x, float y, float width, float height)
{
	return TestPixelBox{
		BackendContract::QuantizeEdge(x),
		BackendContract::QuantizeEdge(y),
		BackendContract::QuantizeEdge(x + width),
		BackendContract::QuantizeEdge(y + height)
	};
}

struct QuantizationProbeWindow
{
	HWND hwnd = nullptr;
	~QuantizationProbeWindow() { if (hwnd) DestroyWindow(hwnd); }
};

bool CreateQuantizationProbeWindow(QuantizationProbeWindow& out)
{
	WNDCLASSW wc{};
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"ECDI_Phase34_QuantizationProbe";
	if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		EXPECT_TRUE(false);
		return false;
	}

	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	out.hwnd = CreateWindowExW(0, wc.lpszClassName, wc.lpszClassName, WS_POPUP,
	                           screenW - 210, 10, 200, 200,
	                           nullptr, nullptr, wc.hInstance, nullptr);
	EXPECT_TRUE(out.hwnd != nullptr);
	if (!out.hwnd) return false;
	ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
	return true;
}

std::vector<COLORREF> CaptureQuantizationProbe(HWND hwnd)
{
	std::vector<COLORREF> pixels;
	pixels.reserve(200 * 200);
	HDC dc = GetDC(hwnd);
	if (!dc) return pixels;
	for (int y = 0; y < 200; ++y)
		for (int x = 0; x < 200; ++x)
			pixels.push_back(GetPixel(dc, x, y));
	ReleaseDC(hwnd, dc);
	return pixels;
}

void DrawLegacyIntegerRectFrame(HWND hwnd, int scale)
{
	HDC dc = GetDC(hwnd);
	RECT full{ 0, 0, 200, 200 };
	HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
	FillRect(dc, &full, background);
	DeleteObject(background);

	const Rect rects[] = {
		{ -4, 5, 14, 12 }, { 18, 9, 23, 17 }, { 47, 25, 32, 24 }, { 82, 12, 13, 28 }
	};
	const COLORREF colors[] = { RGB(255, 0, 0), RGB(0, 0, 255), RGB(0, 255, 0), RGB(255, 255, 255) };
	for (std::size_t i = 0; i < std::size(rects); ++i)
	{
		const LONG left = static_cast<LONG>(rects[i].x * scale);
		const LONG top = static_cast<LONG>(rects[i].y * scale);
		const LONG width = static_cast<LONG>(rects[i].width * scale);
		const LONG height = static_cast<LONG>(rects[i].height * scale);
		RECT legacy{ left, top, left + width, top + height };
		HBRUSH brush = CreateSolidBrush(colors[i]);
		FillRect(dc, &legacy, brush);
		DeleteObject(brush);
	}
	ReleaseDC(hwnd, dc);
}

void RunIntegerScalePixelProbe()
{
	QuantizationProbeWindow window;
	if (!CreateQuantizationProbeWindow(window)) return;

	const Rect rects[] = {
		{ -4, 5, 14, 12 }, { 18, 9, 23, 17 }, { 47, 25, 32, 24 }, { 82, 12, 13, 28 }
	};
	const Color colors[] = { Color::Red(), Color::Blue(), Color::Green(), Color::White() };
	const float scales[] = { 1.0f, 2.0f };
	for (const float scale : scales)
	{
		{
			GDIBackend backend;
			backend.Initialize(Win32RenderContext(window.hwnd));
			Renderer renderer(backend);
			CommandBuffer commands;
			for (std::size_t i = 0; i < std::size(rects); ++i)
				commands.emplace_back(DrawRectCommand{ rects[i], colors[i] });

			InvalidateRect(window.hwnd, nullptr, FALSE);
			renderer.BeginFrame(Color::Black());
			renderer.Execute(commands, scale);
			renderer.EndFrame();
		}
		const std::vector<COLORREF> rendered = CaptureQuantizationProbe(window.hwnd);
		DrawLegacyIntegerRectFrame(window.hwnd, static_cast<int>(scale));
		const std::vector<COLORREF> legacy = CaptureQuantizationProbe(window.hwnd);

		EXPECT_EQ(rendered.size(), std::size_t{ 200 * 200 });
		EXPECT_EQ(legacy.size(), std::size_t{ 200 * 200 });
		std::size_t differingPixels = 0;
		std::size_t invalidPixels = 0;
		for (std::size_t i = 0; i < rendered.size() && i < legacy.size(); ++i)
		{
			if (rendered[i] == CLR_INVALID || legacy[i] == CLR_INVALID) ++invalidPixels;
			if (rendered[i] != legacy[i]) ++differingPixels;
		}
		EXPECT_EQ(invalidPixels, std::size_t{ 0 });
		EXPECT_EQ(differingPixels, std::size_t{ 0 });
	}
}

void TestQuantizeEdgePositiveDiscriminatesRound()
{
	EXPECT_EQ(BackendContract::QuantizeEdge(1.5f), 1);
	EXPECT_EQ(BackendContract::QuantizeEdge(1.75f), 1);
	EXPECT_EQ(BackendContract::QuantizeEdge(2.5f), 2);
	EXPECT_EQ(BackendContract::QuantizeEdge(0.5f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(0.999f), 0);
}

void TestQuantizeEdgeNegativeDiscriminatesTruncation()
{
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.001f), -1);
	EXPECT_EQ(BackendContract::QuantizeEdge(-1.25f), -2);
	EXPECT_EQ(BackendContract::QuantizeEdge(-1.75f), -2);
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.5f), -1);
}

void TestQuantizeEdgeIntegersAndNegativeZero()
{
	EXPECT_EQ(BackendContract::QuantizeEdge(0.0f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(-0.0f), 0);
	EXPECT_EQ(BackendContract::QuantizeEdge(2.0f), 2);
	EXPECT_EQ(BackendContract::QuantizeEdge(-3.0f), -3);
	EXPECT_EQ(BackendContract::QuantizeEdge(3.0f), 3);
	// T34-7: quantizing INT_MIN is defined; do not evaluate a subsequent `- 1`.
	EXPECT_EQ(BackendContract::QuantizeEdge(static_cast<float>((std::numeric_limits<int>::min)())),
	          (std::numeric_limits<int>::min)());
}

void TestPixelBoxUsesEdgeDifference()
{
	// Pixel-space input: these cases distinguish edge subtraction from lround(width).
	const TestPixelBox a = MakePixelBox(0.0f, 0.0f, 1.5f, 1.5f);
	EXPECT_EQ(a.left, 0);
	EXPECT_EQ(a.right - a.left, 1);
	EXPECT_EQ(a.right, 1);

	const TestPixelBox b = MakePixelBox(0.0f, 0.0f, 0.5f, 0.5f);
	EXPECT_EQ(b.right - b.left, 0);

	const TestPixelBox c = MakePixelBox(3.0f, 0.0f, 2.5f, 1.0f);
	EXPECT_EQ(c.left, 3);
	EXPECT_EQ(c.right - c.left, 2);
	EXPECT_EQ(c.right, 5);

	// Controls: equal-value fractional and integral cases.
	const TestPixelBox d = MakePixelBox(1.25f, 0.0f, 2.75f, 1.0f);
	EXPECT_EQ(d.right - d.left, 3);
	const TestPixelBox e = MakePixelBox(2.0f, 0.0f, 4.0f, 1.0f);
	EXPECT_EQ(e.right - e.left, 4);
}

void TestFocusRectBboxIntegerEquivalence()
{
	const float x = -2.0f, y = 4.0f, width = 8.0f, height = 6.0f;
	const int oldLeft = static_cast<int>(x) - 2;
	const int oldTop = static_cast<int>(y) - 2;
	const int oldRight = oldLeft + static_cast<int>(width) + 4;
	const int oldBottom = oldTop + static_cast<int>(height) + 4;
	const int newLeft = BackendContract::QuantizeEdge(x) - 2;
	const int newTop = BackendContract::QuantizeEdge(y) - 2;
	const int newRight = BackendContract::QuantizeEdge(x + width) + 2;
	const int newBottom = BackendContract::QuantizeEdge(y + height) + 2;
	EXPECT_EQ(oldLeft, newLeft);
	EXPECT_EQ(oldTop, newTop);
	EXPECT_EQ(oldRight, newRight);
	EXPECT_EQ(oldBottom, newBottom);
}

void TestFocusRectBboxFractionalDifferenceAttribution()
{
	const float x = -1.5f, y = 0.0f, width = 3.0f, height = 2.0f;
	const int oldLeft = static_cast<int>(x) - 2;
	const int oldRight = oldLeft + static_cast<int>(width) + 4;
	const int newLeft = BackendContract::QuantizeEdge(x) - 2;
	const int newRight = BackendContract::QuantizeEdge(x + width) + 2;
	EXPECT_EQ(oldLeft, -3);
	EXPECT_EQ(oldRight, 4);
	EXPECT_EQ(newLeft, -4);
	EXPECT_EQ(newRight, 3);
	// Both changed edges are attributable to truncation of the same negative x.
	EXPECT_EQ(static_cast<int>(x), -1);
	EXPECT_EQ(BackendContract::QuantizeEdge(x), -2);
}

void TestFocusRectFloatEndpointSemantics()
{
	// Keep q and q-1 exactly representable as float as required by T34-3b.
	const float x = -3.25f, y = 2.75f, width = 11.5f, height = 7.25f;
	const int qRight = BackendContract::QuantizeEdge(x + width);
	const int qBottom = BackendContract::QuantizeEdge(y + height);
	EXPECT_TRUE(qRight > (std::numeric_limits<int>::min)());
	EXPECT_TRUE(qBottom > (std::numeric_limits<int>::min)());
	EXPECT_TRUE(std::abs(qRight) <= (1 << 24));
	EXPECT_TRUE(std::abs(qBottom) <= (1 << 24));
	const float fRight = static_cast<float>(qRight - 1);
	const float fBottom = static_cast<float>(qBottom - 1);
	const float gRight = static_cast<float>(qRight);
	const float gBottom = static_cast<float>(qBottom);
	EXPECT_EQ(gRight - fRight, 1.0f);
	EXPECT_EQ(gBottom - fBottom, 1.0f);
	EXPECT_EQ(static_cast<float>(BackendContract::QuantizeEdge(x)), -4.0f);
	EXPECT_EQ(static_cast<float>(BackendContract::QuantizeEdge(y)), 2.0f);
}

void TestRectConsumersShareQuantizedEdges()
{
	// DrawRect / PushClip use the same pixel-space Rect-to-edge contract.
	const float x = -1.5f, y = 2.25f, width = 4.0f, height = 3.5f;
	const TestPixelBox box = MakePixelBox(x, y, width, height);
	const int clipLeft = BackendContract::QuantizeEdge(x);
	const int drawLeft = BackendContract::QuantizeEdge(x);
	const int clipRight = BackendContract::QuantizeEdge(x + width);
	const int drawRight = BackendContract::QuantizeEdge(x + width);
	EXPECT_EQ(box.left, clipLeft);
	EXPECT_EQ(box.left, drawLeft);
	EXPECT_EQ(box.right, clipRight);
	EXPECT_EQ(box.right, drawRight);
	EXPECT_EQ(box.left + (box.right - box.left), box.right);
	EXPECT_EQ(box.top + (box.bottom - box.top), box.bottom);
}

void TestPixelBoxDimensionsAreDerivedFromEdges()
{
	const TestPixelBox box = MakePixelBox(-12.7f, 4.25f, 55.3f, 8.5f);
	const int width = box.right - box.left;
	const int height = box.bottom - box.top;
	EXPECT_EQ(box.left + width, box.right);
	EXPECT_EQ(box.top + height, box.bottom);
	EXPECT_EQ(box.left, -13);
	EXPECT_EQ(box.right, 42);
	EXPECT_EQ(width, 55);

	// T34-5 integer DIP probe: at 100% / 200%, scaled edges remain integers;
	// compare the new edge-derived box with the old truncation geometry.
	const int xs[] = { -7, 0, 13 };
	const int ys[] = { -3, 2, 11 };
	const int widths[] = { 1, 4, 17 };
	const int heights[] = { 2, 5, 12 };
	const int scales[] = { 1, 2 };
	for (const int scale : scales)
	{
		for (const int x : xs)
			for (const int y : ys)
				for (const int dipWidth : widths)
					for (const int dipHeight : heights)
					{
						const float pxX = static_cast<float>(x * scale);
						const float pxY = static_cast<float>(y * scale);
						const float pxWidth = static_cast<float>(dipWidth * scale);
						const float pxHeight = static_cast<float>(dipHeight * scale);
						const TestPixelBox newBox = MakePixelBox(pxX, pxY, pxWidth, pxHeight);
						const int oldLeft = static_cast<int>(pxX);
						const int oldTop = static_cast<int>(pxY);
						const int oldRight = oldLeft + static_cast<int>(pxWidth);
						const int oldBottom = oldTop + static_cast<int>(pxHeight);
						EXPECT_EQ(newBox.left, oldLeft);
						EXPECT_EQ(newBox.top, oldTop);
						EXPECT_EQ(newBox.right, oldRight);
						EXPECT_EQ(newBox.bottom, oldBottom);
					}
	}

	// T34-5 byte probe: exercise the actual Renderer → GDIBackend path at both scales.
	RunIntegerScalePixelProbe();
}

} // namespace

void ECDI::Test::RegisterBackendQuantizationTests()
{
	GetTestRegistry().Add("BackendQuantization.EdgePositive", &TestQuantizeEdgePositiveDiscriminatesRound);
	GetTestRegistry().Add("BackendQuantization.EdgeNegative", &TestQuantizeEdgeNegativeDiscriminatesTruncation);
	GetTestRegistry().Add("BackendQuantization.EdgeInteger", &TestQuantizeEdgeIntegersAndNegativeZero);
	GetTestRegistry().Add("BackendQuantization.PixelBoxEdgeDifference", &TestPixelBoxUsesEdgeDifference);
	GetTestRegistry().Add("BackendQuantization.FocusRectInteger", &TestFocusRectBboxIntegerEquivalence);
	GetTestRegistry().Add("BackendQuantization.FocusRectFractionalAttribution", &TestFocusRectBboxFractionalDifferenceAttribution);
	GetTestRegistry().Add("BackendQuantization.FocusRectFloatEndpoints", &TestFocusRectFloatEndpointSemantics);
	GetTestRegistry().Add("BackendQuantization.SharedRectEdges", &TestRectConsumersShareQuantizedEdges);
	GetTestRegistry().Add("BackendQuantization.PixelBoxDimensions", &TestPixelBoxDimensionsAreDerivedFromEdges);
}
