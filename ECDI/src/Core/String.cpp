#include"ECDI/Core/String.h"

#include<Windows.h>

namespace ECDI{

std::wstring UTF8ToWide(const std::string& utf8){

	if (utf8.empty()){

		return {};

	}

	const int length = MultiByteToWideChar(
		CP_UTF8,
		0,
		utf8.data(),
		static_cast<int>(utf8.size()),
		nullptr,
		0);

	if (length <= 0){

		return {};

	}

	std::wstring result(length, L'\0');

	MultiByteToWideChar(
		CP_UTF8,
		0,
		utf8.data(),
		static_cast<int>(utf8.size()),
		result.data(),
		length);

	return result;
}

std::string WideToUTF8(const std::wstring& wide){

	if (wide.empty()){

		return {};

	}

	const int length = WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.data(),
		static_cast<int>(wide.size()),
		nullptr,
		0,
		nullptr,
		nullptr);

	if (length <= 0){

		return {};

	}

	std::string result(length, '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.data(),
		static_cast<int>(wide.size()),
		result.data(),
		length,
		nullptr,
		nullptr);

	return result;
}

}
