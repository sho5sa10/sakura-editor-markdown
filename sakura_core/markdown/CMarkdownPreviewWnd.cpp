/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/CMarkdownPreviewWnd.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>

#include "markdown/CMarkdownImageLoader.h"
#include "markdown/CMarkdownPreviewWorker.h"
#include "markdown/MarkdownImagePath.h"
#include "markdown/MarkdownText.h"

#ifdef _MSC_VER
#pragma comment(lib, "msimg32.lib")
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
		const bool japanese = (PRIMARYLANGID(::GetUserDefaultUILanguage()) == LANG_JAPANESE);
		run.text = japanese
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

void CMarkdownPreviewWnd::DeriveColors()
{
	const COLORREF text = (m_userText == CLR_INVALID) ? ::GetSysColor(COLOR_WINDOWTEXT) : m_userText;
	const COLORREF back = (m_userBackground == CLR_INVALID) ? ::GetSysColor(COLOR_WINDOW) : m_userBackground;
	m_colors.text = text;
	m_colors.background = back;
	m_colors.border = Blend(back, text, 25);
	m_colors.quoteBar = Blend(back, text, 40);
	m_colors.codeBackground = Blend(back, text, 7);
	m_colors.tableHeader = Blend(back, text, 10);
	m_colors.link = (Luminance(back) < 128) ? RGB(102, 178, 255) : RGB(0, 102, 204);
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

	// 本文: メッセージフォントの書体を、エディタと同じ大きさで使う。コード: エディタのフォント
	NONCLIENTMETRICSW metrics{};
	metrics.cbSize = sizeof(metrics);
	LOGFONTW body{};
	if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
		body = metrics.lfMessageFont;
	} else {
		body.lfHeight = -ScaleDpi(15);
		::lstrcpynW(body.lfFaceName, L"Segoe UI", LF_FACESIZE);
	}
	body.lfCharSet = DEFAULT_CHARSET;
	LOGFONTW mono{};
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
	m_fonts = std::make_unique<CPreviewFontCache>(body, mono);
}

void CMarkdownPreviewWnd::Relayout()
{
	if (m_hWnd == nullptr) return;
	HDC hdc = ::GetDC(m_hWnd);
	if (hdc == nullptr) return;
	EnsureResources(hdc);
	DeriveColors();

	LayoutSettings settings;
	settings.width = (std::max)(1, m_clientWidth - 1);	// 左端の区切り線の分
	settings.padding = ScaleDpi(14);
	settings.listIndent = ScaleDpi(26);
	settings.quoteIndent = ScaleDpi(18);
	settings.markerGap = ScaleDpi(6);
	settings.blockGap = ScaleDpi(12);
	settings.listItemGap = ScaleDpi(4);
	settings.quoteBarWidth = (std::max)(2, ScaleDpi(3));
	settings.codePadding = ScaleDpi(8);
	settings.tableCellPadX = ScaleDpi(8);
	settings.tableCellPadY = ScaleDpi(4);
	settings.tableMinColumn = ScaleDpi(48);
	settings.ruleThickness = (std::max)(1, ScaleDpi(1));
	m_layoutSettings = settings;

	{
		GdiMeasurer measurer(hdc, *m_fonts, [this](const std::wstring& target) { return LookupImage(target); });
		m_layout = BuildLayout(m_document, measurer, settings);
	}
	::ReleaseDC(m_hWnd, hdc);

	m_maxTextHeight = 0;
	for (const auto& item : m_layout.texts) m_maxTextHeight = (std::max)(m_maxTextHeight, item.height);
	m_maxFillHeight = 0;
	for (const auto& fill : m_layout.fills) m_maxFillHeight = (std::max)(m_maxFillHeight, fill.height);
	m_maxImageHeight = 0;
	for (const auto& image : m_layout.images) m_maxImageHeight = (std::max)(m_maxImageHeight, image.height);

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
	const int viewTop = m_scrollY + area.top;
	const int viewBottom = m_scrollY + area.bottom;

	// 塗りつぶし(背景や罫線)を先に描く
	{
		const auto first = std::lower_bound(m_layout.fills.begin(), m_layout.fills.end(), viewTop - m_maxFillHeight,
			[](const FillItem& item, int top) { return item.y < top; });
		for (auto it = first; it != m_layout.fills.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop) continue;
			COLORREF color = m_colors.border;
			switch (it->kind) {
			case FillKind::QuoteBar:				color = m_colors.quoteBar; break;
			case FillKind::CodeBackground:			color = m_colors.codeBackground; break;
			case FillKind::InlineCodeBackground:	color = m_colors.codeBackground; break;
			case FillKind::TableHeaderBackground:	color = m_colors.tableHeader; break;
			case FillKind::Rule:
			case FillKind::TableBorder:				color = m_colors.border; break;
			}
			const RECT rc{ originX + it->x, it->y - m_scrollY, originX + it->x + it->width, it->y - m_scrollY + it->height };
			fillRect(rc, color);
		}
	}

	// 画像
	{
		const auto first = std::lower_bound(m_layout.images.begin(), m_layout.images.end(), viewTop - m_maxImageHeight,
			[](const ImageItem& item, int top) { return item.y < top; });
		HDC source = nullptr;
		for (auto it = first; it != m_layout.images.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop) continue;
			const auto found = m_images.find(it->key);
			if (found == m_images.end() || found->second.state != ImageEntry::State::Ready) continue;
			if (source == nullptr) {
				source = ::CreateCompatibleDC(mem);
				if (source == nullptr) break;
			}
			const HGDIOBJ previous = ::SelectObject(source, found->second.bitmap);
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
			::AlphaBlend(mem, originX + it->x, it->y - m_scrollY, it->width, it->height,
				source, 0, 0, found->second.width, found->second.height, blend);
			::SelectObject(source, previous);
		}
		if (source != nullptr) ::DeleteDC(source);
	}

	// 文字列
	if (m_fonts) {
		::SetBkMode(mem, TRANSPARENT);
		::SetTextAlign(mem, TA_LEFT | TA_TOP);
		HGDIOBJ original = nullptr;
		HFONT selected = nullptr;
		const auto first = std::lower_bound(m_layout.texts.begin(), m_layout.texts.end(), viewTop - m_maxTextHeight,
			[](const TextItem& item, int top) { return item.y < top; });
		for (auto it = first; it != m_layout.texts.end() && it->y < viewBottom; ++it) {
			if (it->y + it->height <= viewTop || it->text.empty()) continue;
			auto& entry = m_fonts->Get(mem, it->font);
			if (selected != entry.font) {
				const HGDIOBJ previous = ::SelectObject(mem, entry.font);
				if (original == nullptr) original = previous;
				selected = entry.font;
			}
			::SetTextColor(mem, (it->style & InlineStyle::Link) ? m_colors.link : m_colors.text);
			::ExtTextOutW(mem, originX + it->x, it->y - m_scrollY, 0, nullptr,
				it->text.c_str(), static_cast<UINT>(it->text.size()), nullptr);
		}
		if (original != nullptr) ::SelectObject(mem, original);
	}

	::BitBlt(hdc, area.left, area.top, width, height, mem, area.left, area.top, SRCCOPY);
	::SelectObject(mem, oldBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(mem);
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
	default:
		break;
	}
	return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace markdown
