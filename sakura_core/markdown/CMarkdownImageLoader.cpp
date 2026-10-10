/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/CMarkdownImageLoader.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#endif

namespace markdown {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaximumQueuedRequests = 256;
constexpr std::uint32_t kMaximumDimension = 32768;	//!< 画像の幅と高さの上限(縮小前)

class ScopedHandle {
public:
	explicit ScopedHandle(HANDLE handle) noexcept : m_handle(handle) {}
	~ScopedHandle()
	{
		if (IsValid()) ::CloseHandle(m_handle);
	}
	ScopedHandle(const ScopedHandle&) = delete;
	ScopedHandle& operator=(const ScopedHandle&) = delete;
	HANDLE Get() const noexcept { return m_handle; }
	bool IsValid() const noexcept { return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr; }

private:
	HANDLE m_handle;
};

//! COMを、この関数の間だけ使えるようにする。すでに別の方式で初期化されていても、そのまま使える
class ScopedCom {
public:
	ScopedCom() noexcept : m_result(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~ScopedCom()
	{
		if (SUCCEEDED(m_result)) ::CoUninitialize();	// RPC_E_CHANGED_MODEのときは、初期化していないので戻さない
	}
	ScopedCom(const ScopedCom&) = delete;
	ScopedCom& operator=(const ScopedCom&) = delete;
	bool IsUsable() const noexcept { return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE; }

private:
	HRESULT m_result;
};

//! ハンドルの、実際のパス(シンボリックリンクやジャンクションを解決したもの)。\\?\は取り除く
std::wstring FinalPath(HANDLE handle)
{
	constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
	const DWORD required = ::GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
	if (required == 0) return {};
	std::wstring path(required, L'\0');
	const DWORD written = ::GetFinalPathNameByHandleW(handle, path.data(), required, flags);
	if (written == 0 || written >= required) return {};
	path.resize(written);
	if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
		path = L"\\\\" + path.substr(8);
	} else if (path.compare(0, 4, L"\\\\?\\") == 0) {
		path.erase(0, 4);
	}
	while (path.size() > 3 && path.back() == L'\\') path.pop_back();
	return path;
}

//! fileが、rootの下にあるか(大文字小文字は区別しない。フォルダの境界で比べる)
bool IsInside(std::wstring_view root, std::wstring_view file)
{
	while (root.size() > 3 && root.back() == L'\\') root.remove_suffix(1);
	if (root.empty() || file.size() <= root.size()) return false;
	if (::CompareStringOrdinal(file.data(), static_cast<int>(root.size()), root.data(), static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) {
		return false;
	}
	return root.back() == L'\\' || file[root.size()] == L'\\';
}

bool Decode(HANDLE file, const CMarkdownImageLoader::Limits& limits, CMarkdownImageLoader::Image* image)
{
	ComPtr<IWICImagingFactory> factory;
	if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
		return false;
	}
	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(factory->CreateDecoderFromFileHandle(reinterpret_cast<ULONG_PTR>(file), nullptr,
		WICDecodeMetadataCacheOnDemand, &decoder))) {
		return false;
	}
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, &frame))) {
		return false;
	}
	UINT width = 0;
	UINT height = 0;
	if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0
		|| width > kMaximumDimension || height > kMaximumDimension
		|| static_cast<std::uint64_t>(width) * height > limits.maxSourcePixels) {
		return false;
	}

	// 大きい画像は、長い辺がmaxEdgeに収まるように縮める(メモリと描画の負担を抑える)
	UINT scaledWidth = width;
	UINT scaledHeight = height;
	const UINT longEdge = (std::max)(width, height);
	if (longEdge > limits.maxEdge && limits.maxEdge > 0) {
		scaledWidth = (std::max)(1U, static_cast<UINT>(static_cast<std::uint64_t>(width) * limits.maxEdge / longEdge));
		scaledHeight = (std::max)(1U, static_cast<UINT>(static_cast<std::uint64_t>(height) * limits.maxEdge / longEdge));
	}

	ComPtr<IWICBitmapScaler> scaler;
	IWICBitmapSource* source = frame.Get();
	if (scaledWidth != width || scaledHeight != height) {
		if (FAILED(factory->CreateBitmapScaler(&scaler))
			|| FAILED(scaler->Initialize(frame.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant))) {
			return false;
		}
		source = scaler.Get();
	}
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateFormatConverter(&converter))
		|| FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
		return false;
	}

	const std::uint64_t stride = static_cast<std::uint64_t>(scaledWidth) * 4U;
	const std::uint64_t bytes = stride * scaledHeight;
	if (bytes > 0x7FFFFFFFULL) {
		return false;
	}
	image->pixels.assign(static_cast<std::size_t>(bytes), 0);
	if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bytes), image->pixels.data()))) {
		image->pixels.clear();
		return false;
	}
	image->width = static_cast<int>(scaledWidth);
	image->height = static_cast<int>(scaledHeight);
	return true;
}

} // namespace

CMarkdownImageLoader::Image CMarkdownImageLoader::LoadOnThisThread(const ImageLocation& location, const Limits& limits)
{
	Image image;
	image.path = location.path;
	try {
		// フォルダとファイルを開き、実際に開いたものの場所を調べる
		ScopedHandle root(::CreateFileW(location.root.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
		ScopedHandle file(::CreateFileW(location.path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!root.IsValid() || !file.IsValid()) {
			return image;
		}
		if (::GetFileType(file.Get()) != FILE_TYPE_DISK) {
			return image;	// デバイスやパイプは読まない
		}
		BY_HANDLE_FILE_INFORMATION info{};
		if (!::GetFileInformationByHandle(file.Get(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			return image;
		}
		const std::uint64_t size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
		if (size == 0 || size > limits.maxFileBytes) {
			return image;
		}
		const std::wstring finalRoot = FinalPath(root.Get());
		const std::wstring finalFile = FinalPath(file.Get());
		if (!IsInside(finalRoot, finalFile)) {
			return image;	// リンクの先が、文書のフォルダの外
		}

		const ScopedCom com;
		if (!com.IsUsable()) {
			return image;
		}
		// 確認したハンドルから、そのままデコードする(パスは開き直さない)
		image.ok = Decode(file.Get(), limits, &image);
		if (!image.ok) {
			image.pixels.clear();
			image.width = image.height = 0;
		}
	}
	catch (...) {
		image.ok = false;
		image.pixels.clear();
		image.width = image.height = 0;
	}
	return image;
}

CMarkdownImageLoader::Image CMarkdownImageLoader::Load(const ImageLocation& location, const Limits& limits)
{
	// COMの初期化と終了を、呼び出したスレッドでは行わない(呼び出し側のスレッドのCOMの状態に影響しないようにする)
	Image image;
	image.path = location.path;
	try {
		std::thread worker([&] { image = LoadOnThisThread(location, limits); });
		worker.join();
	}
	catch (...) {
		image = Image{};
		image.path = location.path;
	}
	return image;
}

struct CMarkdownImageLoader::State {
	Limits						limits;
	std::mutex					mutex;
	std::condition_variable		condition;
	std::deque<ImageLocation>	queue;
	std::vector<Image>			ready;
	bool						stop = false;
	std::mutex					notifyMutex;
	Notify						notify;

	void Run();
};

void CMarkdownImageLoader::State::Run()
{
	for (;;) {
		ImageLocation location;
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this] { return stop || !queue.empty(); });
			if (stop) return;
			location = std::move(queue.front());
			queue.pop_front();
		}
		Image image = LoadOnThisThread(location, limits);
		bool first = false;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stop) return;
			first = ready.empty();
			ready.push_back(std::move(image));
		}
		if (first) {
			std::lock_guard<std::mutex> lock(notifyMutex);
			if (notify) notify();
		}
	}
}

CMarkdownImageLoader::CMarkdownImageLoader(Notify notify, Limits limits)
	: m_state(std::make_shared<State>())
{
	m_state->limits = limits;
	m_state->notify = std::move(notify);
}

CMarkdownImageLoader::~CMarkdownImageLoader()
{
	Shutdown();
}

bool CMarkdownImageLoader::Request(const ImageLocation& location)
{
	State& state = *m_state;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		if (state.stop || state.queue.size() >= kMaximumQueuedRequests) {
			return false;
		}
		state.queue.push_back(location);
	}
	if (!m_thread.joinable()) {
		m_thread = std::thread([shared = m_state] { shared->Run(); });
	}
	state.condition.notify_one();
	return true;
}

std::vector<CMarkdownImageLoader::Image> CMarkdownImageLoader::TakeReady()
{
	State& state = *m_state;
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<Image> result;
	result.swap(state.ready);
	return result;
}

void CMarkdownImageLoader::Shutdown() noexcept
{
	State& state = *m_state;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.stop = true;
		state.queue.clear();
		state.ready.clear();
	}
	state.condition.notify_all();
	{
		std::lock_guard<std::mutex> lock(state.notifyMutex);
		state.notify = nullptr;
	}
	if (m_thread.joinable()) {
		m_thread.detach();
	}
}

} // namespace markdown
