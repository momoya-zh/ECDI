#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE）

// Windows.h 宏防护（规范条 10：入口 cpp 显式 include Windows.h 同样要防护——
// DrawText 等宏不污染 ECDI 头声明）
#ifdef DrawText
#undef DrawText
#endif

#include "DesktopNest.h"   // examples/DesktopNest 同目录

/// @brief 入口（薄壳——装配与逻辑全部在 DesktopNestApp，与逻辑分离以便链入测试）
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int){

	ECDI::DesktopNest::DesktopNestApp app;

	return app.Run();
}
