/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/CMarkdownPreviewWnd.h"

#include <commdlg.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "markdown/CMarkdownImageLoader.h"
#include "markdown/CMarkdownPreviewWorker.h"
#include "markdown/MarkdownHtml.h"
#include "markdown/MarkdownImagePath.h"
#include "markdown/MarkdownText.h"

#ifdef _MSC_VER
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

namespace markdown {

namespace {

constexpr wchar_t kWindowClassName[] = L"SakuraMarkdownPreviewWnd";

//! ワーカーが解析を終えたことを、UIスレッドに知らせるメッセージ
constexpr UINT kMessageParsed = WM_APP + 1;
//! 画像の読み込みが終わったことを、UIスレッドに知らせるメッセージ
constexpr UINT kMessageImages = WM_APP + 2;
//! 読み込んだ画像の画素数の合計の上限(BGRAで、1画素4バイト)
constexpr std::uint64_t kMaximumImagePixels = 32ULL * 1024 * 1024;
//! 書式付きのコピーで、埋め込む画像のファイルの大きさの合計の上限
constexpr std::size_t kMaximumHtmlImageBytes = 32U * 1024 * 1024;
//! 画像としてコピーするときの高さの上限(px)。これを超える部分は切り捨てる
constexpr int kMaximumCopyImageHeight = 30000;
//! 画像としてコピーするときの画素数の上限
constexpr std::uint64_t kMaximumCopyImagePixels = 80ULL * 1024 * 1024;

//! 右クリックメニューのコマンド
constexpr UINT kCommandCopyHtml = 1;
constexpr UINT kCommandCopyImage = 2;
constexpr UINT kCommandPrint = 3;

//! 印刷の余白(mm)
constexpr int kPrintMarginMillimeters = 15;

//! アクセラレータの識別子(CKeyBind::CreateAccerelator)の上位バイトに入る、修飾キーのビット
constexpr UINT kAccelShift = 0x01;
constexpr UINT kAccelCtrl = 0x02;
constexpr UINT kAccelAlt = 0x04;

using Microsoft::WRL::ComPtr;

bool IsJapaneseUi() noexcept
{
	return PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_JAPANESE;
}

//! クリップボードを開いている間だけ持つ。中身は開いたときに空にする
class ClipboardSession {
public:
	explicit ClipboardSession(HWND owner) noexcept
	{
		// ほかのアプリが一瞬だけ開いていることがあるので、少し待って何度か試す
		for (int i = 0; i < 10 && !m_open; ++i) {
			m_open = ::OpenClipboard(owner) != FALSE;
			if (!m_open) ::Sleep(20);
		}
		if (m_open) ::EmptyClipboard();
	}
	~ClipboardSession()
	{
		if (m_open) ::CloseClipboard();
	}
	ClipboardSession(const ClipboardSession&) = delete;
	ClipboardSession& operator=(const ClipboardSession&) = delete;

	explicit operator bool() const noexcept { return m_open; }

	bool Put(UINT format, const void* data, std::size_t size) noexcept
	{
		if (!m_open || format == 0) return false;
		HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, size);
		if (memory == nullptr) return false;
		void* locked = ::GlobalLock(memory);
		if (locked == nullptr) {
			::GlobalFree(memory);
			return false;
		}
		std::memcpy(locked, data, size);
		::GlobalUnlock(memory);
		if (::SetClipboardData(format, memory) == nullptr) {
			::GlobalFree(memory);
			return false;
		}
		return true;	// 以後、メモリはクリップボードのもの
	}

private:
	bool m_open = false;
};

//! COMをこのスレッドで使える間だけ持つ(すでに初期化済みなら、それを使う)
class ScopedComApartment {
public:
	ScopedComApartment() noexcept : m_result(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
	~ScopedComApartment() { if (SUCCEEDED(m_result)) ::CoUninitialize(); }
	ScopedComApartment(const ScopedComApartment&) = delete;
	ScopedComApartment& operator=(const ScopedComApartment&) = delete;
private:
	HRESULT m_result;
};

//! 上から下へ並んだ 24bpp BGR の画素を PNG にする。失敗したら空
std::vector<BYTE> EncodePng(const BYTE* bits, int width, int height, int stride)
{
	ScopedComApartment com;
	std::vector<BYTE> result;
	ComPtr<IWICImagingFactory> factory;
	if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
		return result;
	}
	ComPtr<IStream> stream;
	if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return result;
	ComPtr<IWICBitmapEncoder> encoder;
	if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
		|| FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
		return result;
	}
	ComPtr<IWICBitmapFrameEncode> frame;
	if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))
		|| FAILED(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
		return result;
	}
	WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
	if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat24bppBGR) return result;
	const UINT size = static_cast<UINT>(stride) * static_cast<UINT>(height);
	if (FAILED(frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(stride), size, const_cast<BYTE*>(bits)))
		|| FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
		return result;
	}
	STATSTG stat{};
	if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.HighPart != 0) return result;
	LARGE_INTEGER zero{};
	if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) return result;
	result.resize(stat.cbSize.LowPart);
	ULONG read = 0;
	if (FAILED(stream->Read(result.data(), stat.cbSize.LowPart, &read)) || read != stat.cbSize.LowPart) {
		result.clear();
	}
	return result;
}

/*!
	premultiplied の 32bpp(BGRA)の DIB セクションを、白地に合成して描く。
	プリンターのドライバーは AlphaBlend に対応しないことがあるので、印刷ではこちらを使う。
*/
void DrawImageOpaque(HDC dc, HBITMAP bitmap, int sourceWidth, int sourceHeight, int x, int y, int width, int height)
{
	DIBSECTION section{};
	if (::GetObjectW(bitmap, sizeof(section), &section) != sizeof(section) || section.dsBm.bmBits == nullptr
		|| section.dsBm.bmBitsPixel != 32 || sourceWidth <= 0 || sourceHeight <= 0) {
		return;
	}
	const int stride = (sourceWidth * 3 + 3) & ~3;
	std::vector<BYTE> rgb(static_cast<std::size_t>(stride) * static_cast<std::size_t>(sourceHeight));
	const auto* source = static_cast<const BYTE*>(section.dsBm.bmBits);
	const bool topDown = section.dsBmih.biHeight < 0;
	for (int row = 0; row < sourceHeight; ++row) {
		const int sourceRow = topDown ? row : (sourceHeight - 1 - row);
		const BYTE* from = source + static_cast<std::size_t>(section.dsBm.bmWidthBytes) * static_cast<std::size_t>(sourceRow);
		BYTE* to = rgb.data() + static_cast<std::size_t>(stride) * static_cast<std::size_t>(row);
		for (int column = 0; column < sourceWidth; ++column, from += 4, to += 3) {
			const int alpha = from[3];
			// premultiplied なので、白(255)の上に重ねると c + (255 - a)
			to[0] = static_cast<BYTE>((std::min)(255, from[0] + 255 - alpha));
			to[1] = static_cast<BYTE>((std::min)(255, from[1] + 255 - alpha));
			to[2] = static_cast<BYTE>((std::min)(255, from[2] + 255 - alpha));
		}
	}
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = sourceWidth;
	info.bmiHeader.biHeight = -sourceHeight;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 24;
	info.bmiHeader.biCompression = BI_RGB;
	const int oldMode = ::SetStretchBltMode(dc, HALFTONE);
	::SetBrushOrgEx(dc, 0, 0, nullptr);
	::StretchDIBits(dc, x, y, width, height, 0, 0, sourceWidth, sourceHeight, rgb.data(), &info, DIB_RGB_COLORS, SRCCOPY);
	::SetStretchBltMode(dc, oldMode);
}

//! 文書のパスから、印刷のジョブ名にするファイル名を取り出す
std::wstring DocumentTitle(const std::wstring& path)
{
	const std::size_t slash = path.find_last_of(L"\\/");
	std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
	if (name.empty()) name = L"Markdown";
	return name;
}

//! ファイルを読む。maximumBytes を超えるもの、読めないものは空
std::vector<unsigned char> ReadSmallFile(const std::wstring& path, std::size_t maximumBytes)
{
	std::vector<unsigned char> data;
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return data;
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(file, &size) && size.QuadPart > 0
		&& static_cast<unsigned long long>(size.QuadPart) <= maximumBytes) {
		data.resize(static_cast<std::size_t>(size.QuadPart));
		std::size_t done = 0;
		while (done < data.size()) {
			DWORD read = 0;
			const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(data.size() - done, 1U << 20));
			if (!::ReadFile(file, data.data() + done, chunk, &read, nullptr) || read == 0) break;
			done += read;
		}
		if (done != data.size()) data.clear();
	}
	::CloseHandle(file);
	return data;
}

COLORREF Blend(COLORREF from, COLORREF to, int percentTo) noexcept
{
	const auto mix = [percentTo](int a, int b) { return (a * (100 - percentTo) + b * percentTo) / 100; };
	return RGB(mix(GetRValue(from), GetRValue(to)), mix(GetGValue(from), GetGValue(to)), mix(GetBValue(from), GetBValue(to)));
}

int Luminance(COLORREF color) noexcept
{
	return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
}

} // namespace

/*!
	FontSpecからHFONTを作って覚えておく。
	文字の幅と高さの測定結果もここに貯める(同じ文字を何度も測らないため)。
*/
class CPreviewFontCache {
public:
	CPreviewFontCache(const LOGFONTW& body, const LOGFONTW& mono)
		: m_body(body)
		, m_mono(mono)
	{
	}

	~CPreviewFontCache()
	{
		for (auto& entry : m_entries) {
			if (entry.font != nullptr) ::DeleteObject(entry.font);
		}
	}

	CPreviewFontCache(const CPreviewFontCache&) = delete;
	CPreviewFontCache& operator=(const CPreviewFontCache&) = delete;

	struct Entry {
		FontSpec	spec;
		HFONT		font = nullptr;
		int			lineHeight = 0;
		std::unordered_map<std::uint32_t, int>	widths;	//!< 1コードポイントの幅
	};

	Entry& Get(HDC hdc, const FontSpec& spec)
	{
		for (auto& entry : m_entries) {
			if (entry.spec == spec) return entry;
		}
		Entry entry;
		entry.spec = spec;
		entry.font = Create(spec);
		HGDIOBJ old = ::SelectObject(hdc, entry.font);
		TEXTMETRICW metrics{};
		if (::GetTextMetricsW(hdc, &metrics)) {
			entry.lineHeight = metrics.tmHeight + metrics.tmExternalLeading;
		}
		::SelectObject(hdc, old);
		if (entry.lineHeight <= 0) entry.lineHeight = 16;
		m_entries.push_back(std::move(entry));
		return m_entries.back();
	}

private:
	HFONT Create(const FontSpec& spec) const
	{
		LOGFONTW lf = spec.mono ? m_mono : m_body;
		const int height = (std::max)(1, std::abs(static_cast<int>(lf.lfHeight)));
		lf.lfHeight = -(std::max)(1, ::MulDiv(height, spec.sizePercent, 100));
		lf.lfWidth = 0;
		lf.lfEscapement = 0;
		lf.lfOrientation = 0;
		lf.lfWeight = spec.bold ? FW_BOLD : FW_NORMAL;
		lf.lfItalic = spec.italic ? TRUE : FALSE;
		lf.lfUnderline = spec.underline ? TRUE : FALSE;
		lf.lfStrikeOut = spec.strike ? TRUE : FALSE;
		lf.lfQuality = CLEARTYPE_QUALITY;
		return ::CreateFontIndirectW(&lf);
	}

	LOGFONTW			m_body;
	LOGFONTW			m_mono;
	std::deque<Entry>	m_entries;	//!< 参照を返すので、追加でアドレスが変わらないdequeにする
};

namespace {

//! デバイスコンテキストを使って文字の寸法を返す
class GdiMeasurer final : public ITextMeasurer {
public:
	using ImageLookup = std::function<ImageSize(const std::wstring&)>;

	GdiMeasurer(HDC hdc, CPreviewFontCache& fonts, ImageLookup imageLookup)
		: m_hdc(hdc)
		, m_fonts(fonts)
		, m_imageLookup(std::move(imageLookup))
	{
	}

	~GdiMeasurer() override
	{
		if (m_selected != nullptr) ::SelectObject(m_hdc, m_original);
	}

	int Width(const FontSpec& font, std::wstring_view text) override
	{
		if (text.empty()) return 0;
		auto& entry = m_fonts.Get(m_hdc, font);
		std::uint32_t key = 0;
		const bool single = IsSingleCodePoint(text, &key);
		if (single) {
			const auto found = entry.widths.find(key);
			if (found != entry.widths.end()) return found->second;
		}
		Select(entry);
		SIZE size{};
		const int length = static_cast<int>((std::min<std::size_t>)(text.size(), 0x7FFFFFFF));
		if (!::GetTextExtentPoint32W(m_hdc, text.data(), length, &size)) {
			size.cx = 0;
		}
		if (single) entry.widths.emplace(key, size.cx);
		return size.cx;
	}

	int LineHeight(const FontSpec& font) override
	{
		return m_fonts.Get(m_hdc, font).lineHeight;
	}

	ImageSize MeasureImage(const std::wstring& target) override
	{
		return m_imageLookup ? m_imageLookup(target) : ImageSize{};
	}

private:
	static bool IsSingleCodePoint(std::wstring_view text, std::uint32_t* cp) noexcept
	{
		std::size_t len = 1;
		const std::uint32_t value = text::CodePointAt(text, 0, &len);
		if (len != text.size()) return false;
		*cp = value;
		return true;
	}

	void Select(CPreviewFontCache::Entry& entry)
	{
		if (m_selected == entry.font) return;
		const HGDIOBJ previous = ::SelectObject(m_hdc, entry.font);
		if (m_selected == nullptr) m_original = previous;
		m_selected = entry.font;
	}

	HDC					m_hdc;
	CPreviewFontCache&	m_fonts;
	ImageLookup			m_imageLookup;
	HGDIOBJ				m_original = nullptr;
	HFONT				m_selected = nullptr;
};

} // namespace

CMarkdownPreviewWnd::CMarkdownPreviewWnd() = default;

CMarkdownPreviewWnd::~CMarkdownPreviewWnd()
{
	Destroy();
	if (m_printDevMode != nullptr) ::GlobalFree(m_printDevMode);
	if (m_printDevNames != nullptr) ::GlobalFree(m_printDevNames);
}

bool CMarkdownPreviewWnd::Create(HWND hwndParent)
{
	if (m_hWnd != nullptr) return true;
	const HINSTANCE instance = ::GetModuleHandleW(nullptr);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = &CMarkdownPreviewWnd::StaticWndProc;
	wc.hInstance = instance;
	wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = kWindowClassName;
	if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		return false;
	}

	m_hWnd = ::CreateWindowExW(0, kWindowClassName, L"",
		WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL,
		0, 0, 0, 0, hwndParent, nullptr, instance, this);
	return m_hWnd != nullptr;
}

void CMarkdownPreviewWnd::Destroy() noexcept
{
	// 先にワーカーを止める。以後、破棄したウィンドウにメッセージが投げられることはない
	m_worker.reset();
	m_imageLoader.reset();
	if (m_hWnd != nullptr) {
		::DestroyWindow(m_hWnd);	// WM_NCDESTROYでm_hWndがnullptrになる
		m_hWnd = nullptr;
	}
	ClearImages();
	m_fonts.reset();
}

void CMarkdownPreviewWnd::Show(bool visible) noexcept
{
	if (m_hWnd != nullptr) {
		::ShowWindow(m_hWnd, visible ? SW_SHOWNA : SW_HIDE);
	}
}

void CMarkdownPreviewWnd::Move(int x, int y, int width, int height) noexcept
{
	if (m_hWnd != nullptr) {
		::MoveWindow(m_hWnd, x, y, (std::max)(0, width), (std::max)(0, height), TRUE);
	}
}

void CMarkdownPreviewWnd::SetDocument(Document document)
{
	m_document = std::move(document);
	if (m_document.truncated) {
		// 上限を超えて、途中までしか表示していないことを末尾に示す
		Block notice;
		notice.kind = BlockKind::Paragraph;
		InlineRun run;
		run.style = InlineStyle::Italic;
		run.text = IsJapaneseUi()
			? L"(文書が大きいため、先頭の部分だけを表示しています)"
			: L"(The document is large, so only the first part is shown.)";
		notice.runs.push_back(std::move(run));
		m_document.blocks.push_back(std::move(notice));
	}
	m_layoutDirty = true;
	if (m_hWnd != nullptr) {
		// まだ大きさが決まっていない(幅0)ときは、最初のWM_SIZEまでレイアウトを待つ
		if (m_clientWidth > 0) {
			Relayout();
		}
		::InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void CMarkdownPreviewWnd::ClearImages() noexcept
{
	for (auto& entry : m_images) {
		if (entry.second.bitmap != nullptr) {
			::DeleteObject(entry.second.bitmap);
		}
	}
	m_images.clear();
	m_imagePixels = 0;
}

void CMarkdownPreviewWnd::SetDocumentPath(std::wstring path)
{
	if (path == m_documentPath) {
		return;
	}
	m_documentPath = std::move(path);
	// 基準のフォルダが変わるので、読み込み済みの画像も、読み込み中のものも捨てる
	m_imageLoader.reset();
	ClearImages();
	m_layoutDirty = true;
}

/*!
	レイアウトから、画像の大きさを聞かれたときの処理(UIスレッド)。
	読み込み済みなら大きさを返す。未読込なら読み込みを依頼して「まだ表示できない」と答える
	(代替テキストを見せる)。読み込みが終わったら、OnImagesLoaded()でレイアウトし直す。
*/
ImageSize CMarkdownPreviewWnd::LookupImage(const std::wstring& target)
{
	const auto location = ResolveLocalImagePath(m_documentPath, target);
	if (!location) {
		return {};	// 保存されていない文書、URL、フォルダの外を指すパスなど
	}
	auto found = m_images.find(location->path);
	if (found == m_images.end()) {
		ImageEntry entry;
		if (m_hWnd != nullptr) {
			if (!m_imageLoader) {
				const HWND hwnd = m_hWnd;
				m_imageLoader = std::make_unique<CMarkdownImageLoader>([hwnd] {
					::PostMessageW(hwnd, kMessageImages, 0, 0);	// ワーカーのスレッドからは、メッセージを投げるだけ
				});
			}
			if (!m_imageLoader->Request(*location)) {
				entry.state = ImageEntry::State::Failed;
			}
		} else {
			entry.state = ImageEntry::State::Failed;
		}
		found = m_images.emplace(location->path, entry).first;
	}
	const ImageEntry& entry = found->second;
	if (entry.state != ImageEntry::State::Ready) {
		return {};
	}
	ImageSize size;
	size.valid = true;
	size.width = ::MulDiv(entry.width, static_cast<int>(m_dpi), 96);	// 96dpiの画像として、画面のDPIに合わせる
	size.height = ::MulDiv(entry.height, static_cast<int>(m_dpi), 96);
	size.key = location->path;
	return size;
}

ImageSize CMarkdownPreviewWnd::LookupLoadedImage(const std::wstring& target, UINT dpi) const
{
	const auto location = ResolveLocalImagePath(m_documentPath, target);
	if (!location) return {};
	const auto found = m_images.find(location->path);
	if (found == m_images.end() || found->second.state != ImageEntry::State::Ready) return {};
	ImageSize size;
	size.valid = true;
	size.width = ::MulDiv(found->second.width, static_cast<int>(dpi), 96);
	size.height = ::MulDiv(found->second.height, static_cast<int>(dpi), 96);
	size.key = location->path;
	return size;
}

//! 画像の読み込みが終わったとき(UIスレッド)。GDIのビットマップはここで作る
void CMarkdownPreviewWnd::OnImagesLoaded()
{
	if (!m_imageLoader) {
		return;
	}
	bool changed = false;
	for (auto& image : m_imageLoader->TakeReady()) {
		const auto found = m_images.find(image.path);
		if (found == m_images.end() || found->second.state != ImageEntry::State::Loading) {
			continue;	// 文書が変わって、不要になった
		}
		ImageEntry& entry = found->second;
		entry.state = ImageEntry::State::Failed;
		changed = true;
		const std::uint64_t pixels = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
		if (!image.ok || pixels == 0 || m_imagePixels + pixels > kMaximumImagePixels
			|| image.pixels.size() != static_cast<std::size_t>(pixels) * 4U) {
			continue;
		}
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = image.width;
		info.bmiHeader.biHeight = -image.height;	// 上から下へ
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP bitmap = ::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (bitmap == nullptr || bits == nullptr) {
			if (bitmap != nullptr) ::DeleteObject(bitmap);
			continue;
		}
		std::memcpy(bits, image.pixels.data(), image.pixels.size());
		entry.bitmap = bitmap;
		entry.width = image.width;
		entry.height = image.height;
		entry.state = ImageEntry::State::Ready;
		m_imagePixels += pixels;
	}
	if (changed && m_hWnd != nullptr) {
		m_layoutDirty = true;
		if (m_clientWidth > 0) {
			Relayout();
		}
		::InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void CMarkdownPreviewWnd::SetParser(std::shared_ptr<const IMarkdownParser> parser)
{
	m_parser = std::move(parser);
	m_worker.reset();	// 次の依頼で、新しいパーサーのワーカーを作る
}

bool CMarkdownPreviewWnd::QueueSource(std::wstring source, const ParseOptions& options)
{
	if (m_hWnd == nullptr || !m_parser) {
		return false;
	}
	if (!m_worker) {
		const HWND hwnd = m_hWnd;
		m_worker = std::make_unique<CMarkdownPreviewWorker>(m_parser, [hwnd] {
			::PostMessageW(hwnd, kMessageParsed, 0, 0);	// ワーカーのスレッドからは、メッセージを投げるだけ
		});
	}
	m_worker->Queue(std::move(source), options);
	return true;
}

//! ワーカーの結果を表示に反映する(UIスレッド)。古い依頼の結果はワーカーが渡さない
void CMarkdownPreviewWnd::OnParsed()
{
	if (!m_worker) {
		return;
	}
	if (auto result = m_worker->TakeResult()) {
		SetDocument(std::move(result->document));
	}
}

void CMarkdownPreviewWnd::SetColors(COLORREF text, COLORREF background)
{
	m_userText = text;
	m_userBackground = background;
	DeriveColors();
	if (m_hWnd != nullptr) {
		::InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void CMarkdownPreviewWnd::SetEditorFont(const LOGFONTW& font)
{
	m_editorFont = font;
	m_hasEditorFont = true;
	m_fonts.reset();
	m_layoutDirty = true;
	if (m_hWnd != nullptr) {
		if (m_clientWidth > 0) {
			Relayout();
		}
		::InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void CMarkdownPreviewWnd::ScrollToTop()
{
	ScrollTo(0);
}

CMarkdownPreviewWnd::PreviewColors CMarkdownPreviewWnd::MakeColors(COLORREF text, COLORREF back) noexcept
{
	PreviewColors colors;
	colors.text = text;
	colors.background = back;
	colors.border = Blend(back, text, 25);
	colors.quoteBar = Blend(back, text, 40);
	colors.codeBackground = Blend(back, text, 7);
	colors.tableHeader = Blend(back, text, 10);
	colors.link = (Luminance(back) < 128) ? RGB(102, 178, 255) : RGB(0, 102, 204);
	return colors;
}

CMarkdownPreviewWnd::LayoutBounds CMarkdownPreviewWnd::MeasureBounds(const LayoutResult& layout) noexcept
{
	LayoutBounds bounds;
	for (const auto& item : layout.texts) bounds.text = (std::max)(bounds.text, item.height);
	for (const auto& item : layout.fills) bounds.fill = (std::max)(bounds.fill, item.height);
	for (const auto& item : layout.images) bounds.image = (std::max)(bounds.image, item.height);
	return bounds;
}

void CMarkdownPreviewWnd::DeriveColors()
{
	const COLORREF text = (m_userText == CLR_INVALID) ? ::GetSysColor(COLOR_WINDOWTEXT) : m_userText;
	const COLORREF back = (m_userBackground == CLR_INVALID) ? ::GetSysColor(COLOR_WINDOW) : m_userBackground;
	m_colors = MakeColors(text, back);
}

int CMarkdownPreviewWnd::ScaleDpi(int pixelsAt96) const noexcept
{
	return ::MulDiv(pixelsAt96, static_cast<int>(m_dpi), 96);
}

void CMarkdownPreviewWnd::EnsureResources(HDC hdc)
{
	const UINT dpi = static_cast<UINT>((std::max)(48, ::GetDeviceCaps(hdc, LOGPIXELSY)));
	if (dpi != m_dpi) {
		m_dpi = dpi;
		m_fonts.reset();
		m_layoutDirty = true;
	}
	if (m_fonts) return;
	LOGFONTW body{};
	LOGFONTW mono{};
	GetBaseFonts(body, mono);
	m_fonts = std::make_unique<CPreviewFontCache>(body, mono);
}

void CMarkdownPreviewWnd::GetBaseFonts(LOGFONTW& body, LOGFONTW& mono) const
{
	// 本文: メッセージフォントの書体を、エディタと同じ大きさで使う。コード: エディタのフォント
	NONCLIENTMETRICSW metrics{};
	metrics.cbSize = sizeof(metrics);
	body = LOGFONTW{};
	if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
		body = metrics.lfMessageFont;
	} else {
		body.lfHeight = -ScaleDpi(15);
		::lstrcpynW(body.lfFaceName, L"Segoe UI", LF_FACESIZE);
	}
	body.lfCharSet = DEFAULT_CHARSET;
	mono = LOGFONTW{};
	if (m_hasEditorFont) {
		mono = m_editorFont;
	} else {
		mono.lfHeight = body.lfHeight;
		::lstrcpynW(mono.lfFaceName, L"Consolas", LF_FACESIZE);
	}
	mono.lfCharSet = DEFAULT_CHARSET;
	if (m_hasEditorFont && m_editorFont.lfHeight != 0) {
		body.lfHeight = m_editorFont.lfHeight;
	}
	if (body.lfHeight == 0) body.lfHeight = -ScaleDpi(15);
	if (mono.lfHeight == 0) mono.lfHeight = body.lfHeight;
}

LayoutSettings CMarkdownPreviewWnd::MakeLayoutSettings(UINT dpi, int width) const noexcept
{
	const auto scale = [dpi](int pixelsAt96) { return ::MulDiv(pixelsAt96, static_cast<int>(dpi), 96); };
	LayoutSettings settings;
	settings.width = (std::max)(1, width);
	settings.padding = scale(14);
	settings.listIndent = scale(26);
	settings.quoteIndent = scale(18);
	settings.markerGap = scale(6);
	settings.blockGap = scale(12);
	settings.listItemGap = scale(4);
	settings.quoteBarWidth = (std::max)(2, scale(3));
	settings.codePadding = scale(8);
	settings.tableCellPadX = scale(8);
	settings.tableCellPadY = scale(4);
	settings.tableMinColumn = scale(48);
	settings.ruleThickness = (std::max)(1, scale(1));
	return settings;
}

void CMarkdownPreviewWnd::Relayout()
{
	if (m_hWnd == nullptr) return;
	HDC hdc = ::GetDC(m_hWnd);
	if (hdc == nullptr) return;
	EnsureResources(hdc);
	DeriveColors();

	const LayoutSettings settings = MakeLayoutSettings(m_dpi, m_clientWidth - 1);	// 左端の区切り線の分
	m_layoutSettings = settings;

	{
		GdiMeasurer measurer(hdc, *m_fonts, [this](const std::wstring& target) { return LookupImage(target); });
		m_layout = BuildLayout(m_document, measurer, settings);
	}
	::ReleaseDC(m_hWnd, hdc);

	m_bounds = MeasureBounds(m_layout);

	m_layoutWidth = m_clientWidth;
	m_layoutDirty = false;
	m_scrollY = std::clamp(m_scrollY, 0, (std::max)(0, m_layout.height - m_clientHeight));
	UpdateScrollBar();
}

void CMarkdownPreviewWnd::UpdateScrollBar()
{
	if (m_hWnd == nullptr) return;
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	info.nMin = 0;
	info.nMax = (std::max)(0, m_layout.height - 1);
	info.nPage = static_cast<UINT>((std::max)(0, m_clientHeight));
	info.nPos = m_scrollY;
	::SetScrollInfo(m_hWnd, SB_VERT, &info, TRUE);
}

void CMarkdownPreviewWnd::ScrollTo(int position)
{
	const int maxScroll = (std::max)(0, m_layout.height - m_clientHeight);
	position = std::clamp(position, 0, maxScroll);
	if (position == m_scrollY) return;
	m_scrollY = position;
	UpdateScrollBar();
	if (m_hWnd != nullptr) {
		::InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void CMarkdownPreviewWnd::ScrollBy(int delta)
{
	ScrollTo(m_scrollY + delta);
}

void CMarkdownPreviewWnd::OnSize()
{
	RECT rc{};
	::GetClientRect(m_hWnd, &rc);
	const int width = rc.right - rc.left;
	m_clientHeight = rc.bottom - rc.top;
	const bool widthChanged = (width != m_clientWidth);
	m_clientWidth = width;
	if (width > 0 && (widthChanged || m_layoutDirty)) {
		Relayout();	// 幅のないウィンドウ(隠れているとき)では、文書をレイアウトしない
	} else {
		m_scrollY = std::clamp(m_scrollY, 0, (std::max)(0, m_layout.height - m_clientHeight));
		UpdateScrollBar();
	}
	::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void CMarkdownPreviewWnd::OnVScroll(UINT code)
{
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_ALL;
	::GetScrollInfo(m_hWnd, SB_VERT, &info);
	const int line = (std::max)(1, ScaleDpi(24));
	int position = m_scrollY;
	switch (code) {
	case SB_LINEUP:			position -= line; break;
	case SB_LINEDOWN:		position += line; break;
	case SB_PAGEUP:			position -= (std::max)(line, m_clientHeight - line); break;
	case SB_PAGEDOWN:		position += (std::max)(line, m_clientHeight - line); break;
	case SB_TOP:			position = 0; break;
	case SB_BOTTOM:			position = m_layout.height; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:	position = info.nTrackPos; break;
	default:				return;
	}
	ScrollTo(position);
}

void CMarkdownPreviewWnd::OnMouseWheel(int delta)
{
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	const int line = (std::max)(1, ScaleDpi(24));
	m_wheelRemainder += delta;
	if (lines == WHEEL_PAGESCROLL) {
		const int pages = m_wheelRemainder / WHEEL_DELTA;
		m_wheelRemainder -= pages * WHEEL_DELTA;
		ScrollBy(-pages * (std::max)(line, m_clientHeight - line));
		return;
	}
	const int notches = m_wheelRemainder / WHEEL_DELTA;
	m_wheelRemainder -= notches * WHEEL_DELTA;
	ScrollBy(-notches * static_cast<int>(lines) * line);
}

void CMarkdownPreviewWnd::OnKeyDown(UINT key)
{
	switch (key) {
	case VK_UP:		OnVScroll(SB_LINEUP); break;
	case VK_DOWN:	OnVScroll(SB_LINEDOWN); break;
	case VK_PRIOR:	OnVScroll(SB_PAGEUP); break;
	case VK_NEXT:	OnVScroll(SB_PAGEDOWN); break;
	case VK_HOME:	OnVScroll(SB_TOP); break;
	case VK_END:	OnVScroll(SB_BOTTOM); break;
	case 'C':
		// アクセラレータに割り当てのない組み合わせ(Ctrl+Shift+C など)は、ここに届く
		if (::GetKeyState(VK_CONTROL) < 0 && ::GetKeyState(VK_MENU) >= 0) {
			const bool ok = (::GetKeyState(VK_SHIFT) < 0) ? CopyAsImage() : CopyAsHtml();
			if (!ok) ::MessageBeep(MB_ICONWARNING);
		}
		break;
	default:		break;
	}
}

void CMarkdownPreviewWnd::PaintTo(HDC hdc, const RECT& area)
{
	const int width = area.right - area.left;
	const int height = area.bottom - area.top;
	if (width <= 0 || height <= 0) return;
	EnsureResources(hdc);

	HDC mem = ::CreateCompatibleDC(hdc);
	HBITMAP bitmap = ::CreateCompatibleBitmap(hdc, width, height);
	if (mem == nullptr || bitmap == nullptr) {
		if (bitmap != nullptr) ::DeleteObject(bitmap);
		if (mem != nullptr) ::DeleteDC(mem);
		return;
	}
	const HGDIOBJ oldBitmap = ::SelectObject(mem, bitmap);
	::SetViewportOrgEx(mem, -area.left, -area.top, nullptr);

	const auto fillRect = [&](const RECT& rc, COLORREF color) {
		::SetDCBrushColor(mem, color);
		::FillRect(mem, &rc, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
	};

	fillRect(area, m_colors.background);
	// 左端に、エディタとの区切り線
	{
		RECT line{ 0, area.top, 1, area.bottom };
		fillRect(line, m_colors.border);
	}

	const int originX = 1;	// 区切り線の分
	if (m_fonts) {
		DrawLayout(mem, m_layout, m_bounds, *m_fonts, m_colors, false, originX, m_scrollY, m_scrollY + area.top, m_scrollY + area.bottom);
	}

	::BitBlt(hdc, area.left, area.top, width, height, mem, area.left, area.top, SRCCOPY);
	::SelectObject(mem, oldBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(mem);
}

void CMarkdownPreviewWnd::DrawLayout(HDC dc, const LayoutResult& layout, const LayoutBounds& bounds, CPreviewFontCache& fonts,
	const PreviewColors& colors, bool printer, int originX, int offsetY, int viewTop, int viewBottom)
{
	const auto fillRect = [dc, printer](const RECT& rc, COLORREF color) {
		if (printer) {
			// プリンター(スプールの EMF)では DC_BRUSH の色が効かないので、ふつうのブラシで塗る
			HBRUSH brush = ::CreateSolidBrush(color);
			if (brush != nullptr) {
				::FillRect(dc, &rc, brush);
				::DeleteObject(brush);
			}
			return;
		}
		::SetDCBrushColor(dc, color);
		::FillRect(dc, &rc, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
	};

	// 塗りつぶし(背景や罫線)を先に描く
	{
		const auto first = std::lower_bound(layout.fills.begin(), layout.fills.end(), viewTop - bounds.fill,
			[](const FillItem& item, int top) { return item.y < top; });
		for (auto it = first; it != layout.fills.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop) continue;
			COLORREF color = colors.border;
			switch (it->kind) {
			case FillKind::QuoteBar:				color = colors.quoteBar; break;
			case FillKind::CodeBackground:			color = colors.codeBackground; break;
			case FillKind::InlineCodeBackground:	color = colors.codeBackground; break;
			case FillKind::TableHeaderBackground:	color = colors.tableHeader; break;
			case FillKind::Rule:
			case FillKind::TableBorder:				color = colors.border; break;
			}
			const RECT rc{ originX + it->x, it->y - offsetY, originX + it->x + it->width, it->y - offsetY + it->height };
			fillRect(rc, color);
		}
	}

	// 画像
	{
		const auto first = std::lower_bound(layout.images.begin(), layout.images.end(), viewTop - bounds.image,
			[](const ImageItem& item, int top) { return item.y < top; });
		HDC source = nullptr;
		for (auto it = first; it != layout.images.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop) continue;
			const auto found = m_images.find(it->key);
			if (found == m_images.end() || found->second.state != ImageEntry::State::Ready) continue;
			if (printer) {
				DrawImageOpaque(dc, found->second.bitmap, found->second.width, found->second.height,
					originX + it->x, it->y - offsetY, it->width, it->height);
				continue;
			}
			if (source == nullptr) {
				source = ::CreateCompatibleDC(dc);
				if (source == nullptr) break;
			}
			const HGDIOBJ previous = ::SelectObject(source, found->second.bitmap);
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			::AlphaBlend(dc, originX + it->x, it->y - offsetY, it->width, it->height,
				source, 0, 0, found->second.width, found->second.height, blend);
			::SelectObject(source, previous);
		}
		if (source != nullptr) ::DeleteDC(source);
	}

	// 文字列
	{
		::SetBkMode(dc, TRANSPARENT);
		::SetTextAlign(dc, TA_LEFT | TA_TOP);
		HGDIOBJ original = nullptr;
		HFONT selected = nullptr;
		const auto first = std::lower_bound(layout.texts.begin(), layout.texts.end(), viewTop - bounds.text,
			[](const TextItem& item, int top) { return item.y < top; });
		for (auto it = first; it != layout.texts.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop || it->text.empty()) continue;
			auto& entry = fonts.Get(dc, it->font);
			if (selected != entry.font) {
				const HGDIOBJ previous = ::SelectObject(dc, entry.font);
				if (original == nullptr) original = previous;
				selected = entry.font;
			}
			::SetTextColor(dc, (it->style & InlineStyle::Link) ? colors.link : colors.text);
			::ExtTextOutW(dc, originX + it->x, it->y - offsetY, 0, nullptr,
				it->text.c_str(), static_cast<UINT>(it->text.size()), nullptr);
		}
		if (original != nullptr) ::SelectObject(dc, original);
	}
}

bool CMarkdownPreviewWnd::EnsureLayout()
{
	if (m_hWnd == nullptr) return false;
	if (m_layoutDirty || m_layoutWidth != m_clientWidth) {
		if (m_clientWidth <= 0) return false;	// 隠れていて、まだ幅がない
		Relayout();
	}
	return !m_layoutDirty;
}

std::string CMarkdownPreviewWnd::ImageDataUri(const std::wstring& target, std::size_t& budget)
{
	// プレビューに表示できている画像だけを埋め込む(読み込みのときに、場所の確認を済ませている)
	const auto location = ResolveLocalImagePath(m_documentPath, target);
	if (!location) return {};
	const auto found = m_images.find(location->path);
	if (found == m_images.end() || found->second.state != ImageEntry::State::Ready) return {};
	const std::string mime = ImageMimeTypeFromPath(location->path);
	if (mime.empty()) return {};
	const std::vector<unsigned char> data = ReadSmallFile(location->path, budget);
	if (data.empty()) return {};
	budget -= data.size();
	std::string uri = "data:";
	uri += mime;
	uri += ";base64,";
	uri += Base64Encode(data.data(), data.size());
	return uri;
}

bool CMarkdownPreviewWnd::CopyAsHtml()
{
	if (m_hWnd == nullptr || m_document.blocks.empty()) return false;
	std::size_t budget = kMaximumHtmlImageBytes;
	HtmlExportOptions options;
	options.imageSource = [this, &budget](const std::wstring& target) { return ImageDataUri(target, budget); };
	const std::string html = BuildClipboardHtml(BuildHtmlFragment(m_document, options));
	const std::wstring text = BuildPlainText(m_document);

	static const UINT htmlFormat = ::RegisterClipboardFormatW(L"HTML Format");
	ClipboardSession clipboard(m_hWnd);
	if (!clipboard) return false;
	bool ok = clipboard.Put(htmlFormat, html.c_str(), html.size() + 1);
	ok = clipboard.Put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t)) && ok;
	return ok;
}

bool CMarkdownPreviewWnd::CopyAsImage()
{
	if (m_document.blocks.empty() || !EnsureLayout()) return false;
	const int width = (std::max)(1, m_layoutSettings.width);
	int height = (std::min)((std::max)(1, m_layout.height), kMaximumCopyImageHeight);
	const std::uint64_t maxHeight = kMaximumCopyImagePixels / static_cast<std::uint64_t>(width);
	height = static_cast<int>((std::min<std::uint64_t>)(static_cast<std::uint64_t>(height), (std::max<std::uint64_t>)(1, maxHeight)));
	const int stride = (width * 3 + 3) & ~3;

	HDC screen = ::GetDC(m_hWnd);
	if (screen == nullptr) return false;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;	// 上から下へ(PNGの並びと同じ)
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 24;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	HDC mem = ::CreateCompatibleDC(screen);
	::ReleaseDC(m_hWnd, screen);
	if (bitmap == nullptr || bits == nullptr || mem == nullptr) {
		if (bitmap != nullptr) ::DeleteObject(bitmap);
		if (mem != nullptr) ::DeleteDC(mem);
		return false;
	}
	const HGDIOBJ oldBitmap = ::SelectObject(mem, bitmap);
	{
		const RECT all{ 0, 0, width, height };
		::SetDCBrushColor(mem, m_colors.background);
		::FillRect(mem, &all, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
	}
	if (m_fonts) {
		DrawLayout(mem, m_layout, m_bounds, *m_fonts, m_colors, false, 0, 0, 0, height);
	}
	::GdiFlush();

	const auto* top = static_cast<const BYTE*>(bits);
	const std::size_t imageBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
	const std::vector<BYTE> png = EncodePng(top, width, height, stride);

	// CF_DIB は下から上へ並べるのが一般的なので、行を逆にする
	std::vector<BYTE> dib(sizeof(BITMAPINFOHEADER) + imageBytes);
	BITMAPINFOHEADER header = info.bmiHeader;
	header.biHeight = height;
	header.biSizeImage = static_cast<DWORD>(imageBytes);
	std::memcpy(dib.data(), &header, sizeof(header));
	for (int y = 0; y < height; ++y) {
		std::memcpy(dib.data() + sizeof(header) + static_cast<std::size_t>(stride) * static_cast<std::size_t>(height - 1 - y),
			top + static_cast<std::size_t>(stride) * static_cast<std::size_t>(y), static_cast<std::size_t>(stride));
	}
	::SelectObject(mem, oldBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(mem);

	static const UINT pngFormat = ::RegisterClipboardFormatW(L"PNG");
	ClipboardSession clipboard(m_hWnd);
	if (!clipboard) return false;
	bool ok = false;
	if (!png.empty()) ok = clipboard.Put(pngFormat, png.data(), png.size());	// 先に入れたものを、貼り付け先が優先する
	ok = clipboard.Put(CF_DIB, dib.data(), dib.size()) || ok;
	return ok;
}

bool CMarkdownPreviewWnd::Print()
{
	if (m_hWnd == nullptr || m_document.blocks.empty()) return false;
	{
		// 画面のDPIとフォントを確定させる(印刷のフォントは、画面のフォントを拡大して作る)
		HDC screen = ::GetDC(m_hWnd);
		if (screen == nullptr) return false;
		EnsureResources(screen);
		::ReleaseDC(m_hWnd, screen);
	}

	PRINTDLGW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = ::GetAncestor(m_hWnd, GA_ROOT);
	dialog.hDevMode = m_printDevMode;
	dialog.hDevNames = m_printDevNames;
	dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
	dialog.nMinPage = 1;
	dialog.nMaxPage = 9999;
	dialog.nFromPage = 1;
	dialog.nToPage = 9999;
	const BOOL accepted = ::PrintDlgW(&dialog);
	m_printDevMode = dialog.hDevMode;	// 次回のために、選んだプリンターと設定を覚えておく
	m_printDevNames = dialog.hDevNames;
	if (!accepted || dialog.hDC == nullptr) {
		return false;	// キャンセル
	}
	HDC printer = dialog.hDC;
	const HCURSOR oldCursor = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));

	// 用紙の寸法(デバイスの単位)。余白は用紙の端から測り、印刷できない領域(物理オフセット)を引く
	const int dpiX = (std::max)(1, ::GetDeviceCaps(printer, LOGPIXELSX));
	const int dpiY = (std::max)(1, ::GetDeviceCaps(printer, LOGPIXELSY));
	const int marginX = ::MulDiv(kPrintMarginMillimeters * 10, dpiX, 254);
	const int marginY = ::MulDiv(kPrintMarginMillimeters * 10, dpiY, 254);
	const int offsetX = ::GetDeviceCaps(printer, PHYSICALOFFSETX);
	const int offsetY = ::GetDeviceCaps(printer, PHYSICALOFFSETY);
	const int physicalWidth = ::GetDeviceCaps(printer, PHYSICALWIDTH);
	const int physicalHeight = ::GetDeviceCaps(printer, PHYSICALHEIGHT);
	const int left = (std::max)(0, marginX - offsetX);
	const int top = (std::max)(0, marginY - offsetY);
	const int right = (std::min)(::GetDeviceCaps(printer, HORZRES), physicalWidth - marginX - offsetX);
	const int bottom = (std::min)(::GetDeviceCaps(printer, VERTRES), physicalHeight - marginY - offsetY);
	const int contentWidth = right - left;
	const int contentHeight = bottom - top;

	bool ok = contentWidth > 0 && contentHeight > 0;
	if (ok) {
		// 画面と同じポイント数になるように、フォントをプリンターのDPIに拡大する
		LOGFONTW body{};
		LOGFONTW mono{};
		GetBaseFonts(body, mono);
		body.lfHeight = ::MulDiv(body.lfHeight, dpiY, static_cast<int>(m_dpi));
		mono.lfHeight = ::MulDiv(mono.lfHeight, dpiY, static_cast<int>(m_dpi));
		CPreviewFontCache fonts(body, mono);

		LayoutSettings settings = MakeLayoutSettings(static_cast<UINT>(dpiY), contentWidth);
		settings.padding = 0;	// 用紙の余白があるので要らない
		LayoutResult layout;
		{
			const UINT dpi = static_cast<UINT>(dpiY);
			GdiMeasurer measurer(printer, fonts, [this, dpi](const std::wstring& target) { return LookupLoadedImage(target, dpi); });
			layout = BuildLayout(m_document, measurer, settings);
		}
		const LayoutBounds bounds = MeasureBounds(layout);
		const std::vector<int> pages = PaginateLayout(layout, contentHeight);
		const PreviewColors colors = MakeColors(RGB(0, 0, 0), RGB(255, 255, 255));	// 画面の配色によらず、白地に黒

		const int pageCount = static_cast<int>(pages.size());
		int firstPage = 1;
		int lastPage = pageCount;
		if ((dialog.Flags & PD_PAGENUMS) != 0) {
			firstPage = (std::max)(1, static_cast<int>(dialog.nFromPage));
			lastPage = (std::min)(pageCount, static_cast<int>(dialog.nToPage));
		}
		// ドライバーが部数を扱えないときは、nCopies に部数が入る
		const int copies = (std::max)(1, static_cast<int>(dialog.nCopies));

		const std::wstring title = DocumentTitle(m_documentPath);
		DOCINFOW docInfo{};
		docInfo.cbSize = sizeof(docInfo);
		docInfo.lpszDocName = title.c_str();
		ok = firstPage <= lastPage && ::StartDocW(printer, &docInfo) > 0;
		for (int copy = 0; ok && copy < copies; ++copy) {
			for (int page = firstPage; ok && page <= lastPage; ++page) {
				if (::StartPage(printer) <= 0) {
					ok = false;
					break;
				}
				const int pageTop = pages[static_cast<std::size_t>(page - 1)];
				const int pageBottom = (page < pageCount) ? pages[static_cast<std::size_t>(page)] : layout.height;
				const int saved = ::SaveDC(printer);
				::IntersectClipRect(printer, left, top, right, top + (pageBottom - pageTop));
				DrawLayout(printer, layout, bounds, fonts, colors, true, left, pageTop - top, pageTop, pageBottom);
				::RestoreDC(printer, saved);

				// ページ番号(下の余白の中央)
				const std::wstring number = std::to_wstring(page) + L" / " + std::to_wstring(pageCount);
				FontSpec spec;
				spec.sizePercent = 80;
				auto& entry = fonts.Get(printer, spec);
				const HGDIOBJ oldFont = ::SelectObject(printer, entry.font);
				::SetBkMode(printer, TRANSPARENT);
				::SetTextColor(printer, RGB(96, 96, 96));
				::SetTextAlign(printer, TA_CENTER | TA_TOP);
				const int footerY = bottom + (std::max)(0, (marginY - entry.lineHeight) / 2);
				::ExtTextOutW(printer, (left + right) / 2, footerY, 0, nullptr, number.c_str(), static_cast<UINT>(number.size()), nullptr);
				::SetTextAlign(printer, TA_LEFT | TA_TOP);
				::SelectObject(printer, oldFont);

				if (::EndPage(printer) <= 0) ok = false;
			}
		}
		if (ok) {
			ok = ::EndDoc(printer) > 0;
		} else {
			::AbortDoc(printer);
		}
	}
	::DeleteDC(printer);
	::SetCursor(oldCursor);
	if (!ok) {
		const bool japanese = IsJapaneseUi();
		::MessageBoxW(::GetAncestor(m_hWnd, GA_ROOT),
			japanese ? L"プレビューを印刷できませんでした。" : L"Could not print the preview.",
			japanese ? L"Markdown プレビュー" : L"Markdown Preview", MB_OK | MB_ICONWARNING);
	}
	return ok;
}

void CMarkdownPreviewWnd::OnContextMenu(int x, int y)
{
	if (x == -1 && y == -1) {	// キーボード(Shift+F10、アプリケーションキー)から
		POINT point{ ScaleDpi(8), ScaleDpi(8) };
		::ClientToScreen(m_hWnd, &point);
		x = point.x;
		y = point.y;
	}
	HMENU menu = ::CreatePopupMenu();
	if (menu == nullptr) return;
	const bool japanese = IsJapaneseUi();
	const UINT flags = MF_STRING | (m_document.blocks.empty() ? MF_GRAYED : 0U);
	::AppendMenuW(menu, flags, kCommandCopyHtml,
		japanese ? L"書式付きでコピー(&C)\tCtrl+C" : L"&Copy with Formatting\tCtrl+C");
	::AppendMenuW(menu, flags, kCommandCopyImage,
		japanese ? L"画像としてコピー(&I)\tCtrl+Shift+C" : L"Copy as &Image\tCtrl+Shift+C");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, flags, kCommandPrint,
		japanese ? L"プレビューを印刷(&P)...\tCtrl+P" : L"&Print Preview...\tCtrl+P");
	const UINT command = static_cast<UINT>(::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
		x, y, 0, m_hWnd, nullptr));
	::DestroyMenu(menu);
	bool ok = true;
	if (command == kCommandCopyHtml) {
		ok = CopyAsHtml();
	} else if (command == kCommandCopyImage) {
		ok = CopyAsImage();
	} else if (command == kCommandPrint) {
		Print();	// キャンセルもあるので、音は鳴らさない(失敗はPrintが知らせる)
	}
	if (!ok) ::MessageBeep(MB_ICONWARNING);
}

/*!
	エディタのキー割り当て(アクセラレータ)から届いたコマンド。
	Ctrl+C と Ctrl+Shift+C はプレビューのコピー、Ctrl+P は印刷、修飾キーなしのカーソルキーなどはスクロールにする。
	@retval false	プレビューでは使わないキー
*/
bool CMarkdownPreviewWnd::OnAccelerator(WORD id)
{
	const UINT key = id & 0xFFU;
	const UINT modifiers = (id >> 8) & 0xFFU;
	if (key == 'C' && (modifiers & kAccelCtrl) != 0 && (modifiers & kAccelAlt) == 0) {
		const bool ok = (modifiers & kAccelShift) != 0 ? CopyAsImage() : CopyAsHtml();
		if (!ok) ::MessageBeep(MB_ICONWARNING);
		return true;
	}
	if (key == 'P' && modifiers == kAccelCtrl) {
		Print();
		return true;
	}
	if (modifiers == 0) {
		switch (key) {
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
			OnKeyDown(key);
			return true;
		default:
			break;
		}
	}
	return false;
}

void CMarkdownPreviewWnd::OnPaint()
{
	PAINTSTRUCT ps{};
	HDC hdc = ::BeginPaint(m_hWnd, &ps);
	if (hdc != nullptr) {
		EnsureResources(hdc);
		if (m_layoutDirty || m_layoutWidth != m_clientWidth) {
			Relayout();	// リサイズ後などで未反映のとき
		}
		PaintTo(hdc, ps.rcPaint);
	}
	::EndPaint(m_hWnd, &ps);
}

LRESULT CALLBACK CMarkdownPreviewWnd::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CMarkdownPreviewWnd* self = nullptr;
	if (message == WM_NCCREATE) {
		const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
		self = static_cast<CMarkdownPreviewWnd*>(create->lpCreateParams);
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		if (self != nullptr) self->m_hWnd = hwnd;
	} else {
		self = reinterpret_cast<CMarkdownPreviewWnd*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}
	if (self == nullptr) {
		return ::DefWindowProcW(hwnd, message, wParam, lParam);
	}
	const LRESULT result = self->WndProc(hwnd, message, wParam, lParam);
	if (message == WM_NCDESTROY) {
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		self->m_hWnd = nullptr;
	}
	return result;
}

LRESULT CMarkdownPreviewWnd::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == kMessageParsed) {
		OnParsed();
		return 0;
	}
	if (message == kMessageImages) {
		OnImagesLoaded();
		return 0;
	}
	switch (message) {
	case WM_ERASEBKGND:
		return 1;	// 描画時に全体を塗るのでちらつかせない
	case WM_PAINT:
		OnPaint();
		return 0;
	case WM_PRINTCLIENT: {
		// スクリーンショットなどのために、指定されたDCへクライアント領域を描く
		HDC hdc = reinterpret_cast<HDC>(wParam);
		RECT rc{};
		::GetClientRect(hwnd, &rc);
		EnsureResources(hdc);
		if (m_layoutDirty || m_layoutWidth != m_clientWidth) {
			Relayout();
		}
		PaintTo(hdc, rc);
		return 0;
	}
	case WM_SIZE:
		OnSize();
		return 0;
	case WM_VSCROLL:
		OnVScroll(LOWORD(wParam));
		return 0;
	case WM_MOUSEWHEEL:
		OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
		return 0;
	case WM_KEYDOWN:
		OnKeyDown(static_cast<UINT>(wParam));
		return 0;
	case WM_GETDLGCODE:
		return DLGC_WANTARROWS;
	case WM_LBUTTONDOWN:
		::SetFocus(hwnd);	// ホイールやキーでスクロールできるようにする
		return 0;
	case WM_RBUTTONDOWN:
		::SetFocus(hwnd);	// Ctrl+C でもコピーできるようにする
		return 0;
	case WM_CONTEXTMENU:
		OnContextMenu(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
		return 0;
	case WM_COMMAND:
		// エディタのメッセージループが、フォーカスのあるこのウィンドウへアクセラレータを送ってくる
		// (WM_KEYDOWN の代わりに届く)。プレビューで使わないキーは無視する(エディタの文書を変えないため)
		if (HIWORD(wParam) == 1) {
			OnAccelerator(LOWORD(wParam));
			return 0;
		}
		break;
	default:
		break;
	}
	return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace markdown
