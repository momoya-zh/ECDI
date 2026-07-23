#pragma once

#include<string_view>

namespace ECDI
{
	namespace Detail
	{
		void HandleAssertFailure(
			std::string_view expression,
			std::string_view file,
			int line,
			std::string_view function);
	}
}

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
}while (false)  


#else

#define FRAMEWORK_ASSERT(condition) ((void)0)

#endif
