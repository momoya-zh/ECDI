#pragma once

#include<string_view>

namespace ECDI{

	namespace Detail{

		/// @brief 断言失败处理函数（内部使用）
		/// @param expression 失败的条件表达式文本
		/// @param file       源文件路径
		/// @param line       行号
		/// @param function   函数名
		/// @details 输出日志 → 弹 MessageBox → assert(false) 终止程序
		void HandleAssertFailure(
			std::string_view expression,
			std::string_view file,
			int line,
			std::string_view function);
	}
}

// Debug 模式：断言失败时输出详细信息并终止
#ifdef _DEBUG

#define FRAMEWORK_ASSERT(condition)                         \
do                                                          \
{                                                           \
	if (!(condition))                                       \
	{                                                       \
		ECDI::Detail::HandleAssertFailure(                  \
			#condition,                                     \
			__FILE__,                                       \
			__LINE__,                                       \
			__func__);                                      \
	}                                                       \
} while (false)


#else

// Release 模式：断言被编译为空操作
#define FRAMEWORK_ASSERT(condition) ((void)0)

#endif
