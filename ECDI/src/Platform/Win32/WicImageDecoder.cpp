#include "ECDI/Decode/ImageDecoder.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"

#include <windows.h>

#include <initguid.h>     // ⚠️ 必须在首次包含声明相关 GUID 的 SDK 头之前——CLSID/IID 在本 TU 生成定义
#include <wincodec.h>     // WIC 全套 + CLSID_WICImagingFactory + IID_PPV_ARGS
#include <shlwapi.h>      // SHCreateMemStream

// windows.h 的 min/max 宏会打爆 numeric_limits<>::max()（skill 条 10 防御性 undef 先例）
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <cstdint>
#include <limits>
#include <string>

namespace ECDI::Decode
{

namespace {

// ── 日志辅助（Logger::Log(LogLevel, wstring_view) 无格式化——方案 B：内部组装，不改 Logger 契约）──
void LogDecoderError(const wchar_t* step, HRESULT hr)
{
	wchar_t buf[128];
	std::swprintf(buf, 128, L"ImageDecoder: %s failed (hr=0x%08lX)", step, static_cast<unsigned long>(hr));
	Logger::Log(LogLevel::Error, std::wstring_view{buf});
}

void LogImageTooLarge(unsigned int w, unsigned int h)
{
	wchar_t buf[128];
	std::swprintf(buf, 128, L"ImageDecoder: image too large (w=%u h=%u)", w, h);
	Logger::Log(LogLevel::Error, std::wstring_view{buf});
}

// ── ComPtr（轻量 RAII——匿名 namespace，不引 wrl；出参专用 ReleaseAndGetAddressOf 防覆盖泄漏）──
template <typename T>
class ComPtr {
	T* m_ptr = nullptr;
public:
	ComPtr() = default;
	explicit ComPtr(T* p) noexcept : m_ptr(p) {}   // 接管裸指针所有权（如 SHCreateMemStream 返回值）
	~ComPtr() { Reset(); }
	ComPtr(const ComPtr&) = delete;
	ComPtr& operator=(const ComPtr&) = delete;
	ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
	ComPtr& operator=(ComPtr&& other) noexcept {
		if (this != &other) { Reset(); m_ptr = other.m_ptr; other.m_ptr = nullptr; }
		return *this;
	}
	T* Get() const noexcept { return m_ptr; }
	T* operator->() const noexcept { return m_ptr; }
	explicit operator bool() const noexcept { return m_ptr != nullptr; }
	void Reset() noexcept { if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; } }
	T** ReleaseAndGetAddressOf() noexcept { Reset(); return &m_ptr; }
};

// ── ComScope（per-call COM 生命周期——详设 v1.1 双状态：usable / ownsInit）──
/// RPC_E_CHANGED_MODE：当前线程已有他模式 COM 初始化——本次【未取得】引用计数，
/// 可用但【不得】CoUninitialize（否则替别人撤销）。
struct ComScope {
	bool usable = false;
	bool ownsInit = false;
	explicit ComScope() {
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (hr == S_OK || hr == S_FALSE) { usable = true; ownsInit = true; }    // 取得引用计数
		else if (hr == RPC_E_CHANGED_MODE) { usable = true; ownsInit = false; } // 可用但不 Uninit
	}
	~ComScope() { if (ownsInit) CoUninitialize(); }
};

/// @brief 尺寸四域检查（int 域 → stride int 域 → buffer UINT 域 → size_t 域 → 256MB 资源上限）
/// @return 检查通过并填入 out 参数则 true
bool CalculateImageBufferSize(unsigned int w, unsigned int h,
                              int& outWidth, int& outHeight,
                              unsigned int& outStride, unsigned int& outBufferSize,
                              std::size_t& outVectorSize)
{
	constexpr std::uint64_t kIntMax  = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
	constexpr std::uint64_t kUintMax = static_cast<std::uint64_t>(std::numeric_limits<unsigned int>::max());
	constexpr std::uint64_t kSizeMax = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
	constexpr std::uint64_t kMaxDecodedBytes = 256ull * 1024ull * 1024ull;   // 资源上限（防恶意图片/解压炸弹——独立于溢出检查）

	if (w == 0 || h == 0) { LogImageTooLarge(w, h); return false; }
	if (static_cast<std::uint64_t>(w) > kIntMax || static_cast<std::uint64_t>(h) > kIntMax) { LogImageTooLarge(w, h); return false; }

	const std::uint64_t stride64 = static_cast<std::uint64_t>(w) * 4u;   // 32bpp
	if (stride64 > kIntMax) { LogImageTooLarge(w, h); return false; }

	const std::uint64_t buffer64 = stride64 * h;
	if (buffer64 > kUintMax) { LogImageTooLarge(w, h); return false; }   // CopyPixels cbStride/cbBufferSize 是 UINT
	if (buffer64 > kSizeMax) { LogImageTooLarge(w, h); return false; }
	if (buffer64 > kMaxDecodedBytes) { LogImageTooLarge(w, h); return false; }

	outWidth = static_cast<int>(w);
	outHeight = static_cast<int>(h);
	outStride = static_cast<unsigned int>(stride64);
	outBufferSize = static_cast<unsigned int>(buffer64);
	outVectorSize = static_cast<std::size_t>(buffer64);
	return true;
}

/// @brief 共同后段：Frame → FormatConverter(32bppPBGRA) → CopyPixels → Image
Image DecodeFromDecoder(IWICImagingFactory* factory, IWICBitmapDecoder* decoder)
{
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, frame.ReleaseAndGetAddressOf()))) {
		LogDecoderError(L"GetFrame", E_FAIL);
		return {};
	}

	unsigned int w = 0, h = 0;
	if (FAILED(frame->GetSize(&w, &h))) {
		LogDecoderError(L"GetSize", E_FAIL);
		return {};
	}

	int width = 0, height = 0;
	unsigned int stride = 0, bufferSize = 0;
	std::size_t vectorSize = 0;
	if (!CalculateImageBufferSize(w, h, width, height, stride, bufferSize, vectorSize)) {
		return {};   // 日志已在检查内
	}

	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf()))) {
		LogDecoderError(L"create format converter", E_FAIL);
		return {};
	}

	// 32bppPBGRA：与 Image::pixels（premultiplied BGRA / top-down / stride=w*4）逐字节同构——直灌零转换
	HRESULT hr = converter->Initialize(
	    frame.Get(), GUID_WICPixelFormat32bppPBGRA,
	    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) {
		LogDecoderError(L"convert to 32bppPBGRA", hr);
		return {};
	}

	Image image;
	image.width = width;
	image.height = height;
	image.stride = stride;
	image.pixels.resize(vectorSize);

	hr = converter->CopyPixels(nullptr, stride, bufferSize, image.pixels.data());
	if (FAILED(hr)) {
		LogDecoderError(L"CopyPixels", hr);
		return {};
	}
	return image;
}

Image DecodeFileImpl(const std::wstring& widePath)
{
	ComScope comScope;
	if (!comScope.usable) { LogDecoderError(L"CoInitializeEx", E_FAIL); return {}; }

	ComPtr<IWICImagingFactory> factory;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
	                              IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) { LogDecoderError(L"create WIC factory", hr); return {}; }

	ComPtr<IWICBitmapDecoder> decoder;
	hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
	                                        WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
	if (FAILED(hr)) { LogDecoderError(L"create decoder", hr); return {}; }

	return DecodeFromDecoder(factory.Get(), decoder.Get());
}

Image DecodeMemoryImpl(const std::uint8_t* data, std::size_t size)
{
	ComScope comScope;
	if (!comScope.usable) { LogDecoderError(L"CoInitializeEx", E_FAIL); return {}; }

	ComPtr<IWICImagingFactory> factory;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
	                              IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) { LogDecoderError(L"create WIC factory", hr); return {}; }

	ComPtr<IStream> stream(SHCreateMemStream(data, static_cast<unsigned int>(size)));
	if (!stream) { LogDecoderError(L"create memory stream", E_FAIL); return {}; }

	ComPtr<IWICBitmapDecoder> decoder;
	hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
	                                      WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
	if (FAILED(hr)) { LogDecoderError(L"create decoder", hr); return {}; }

	return DecodeFromDecoder(factory.Get(), decoder.Get());
}

} // anonymous namespace

namespace Decode
{

Image DecodeMemory(const std::uint8_t* data, std::size_t size)
{
	// 输入校验（3.0）：非法输入不交给 WIC/流层
	if (size == 0 || data == nullptr) {
		Logger::Log(LogLevel::Error, L"ImageDecoder: invalid input (null data / zero size)");
		return {};
	}
	return DecodeMemoryImpl(data, size);
}

Image DecodeFile(const std::string& utf8Path)
{
	return DecodeFileImpl(UTF8ToWide(utf8Path));
}

} // namespace Decode

} // namespace ECDI::Decode
