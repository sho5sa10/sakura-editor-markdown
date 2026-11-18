/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CMARKDOWNPREVIEWWND_A3D70B94_5E26_4C1F_9B83_6F02E8D1C547_H_
#define SAKURA_CMARKDOWNPREVIEWWND_A3D70B94_5E26_4C1F_9B83_6F02E8D1C547_H_
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "markdown/IMarkdownParser.h"
#include "markdown/MarkdownLayout.h"
#include "markdown/MarkdownModel.h"

namespace markdown {

class CMarkdownImageLoader;
class CMarkdownPreviewWorker;
class CPreviewFontCache;

/*!
	@brief Markdownのプレビューを表示するネイティブ(GDI)の子ウィンドウ

	文書(markdown::Document)を受け取り、MarkdownLayoutで配置して描画する。
	Markdownのパーサーや、エディタ本体のクラスには依存しない。
	すべてのメソッドはUIスレッドから呼ぶこと。
*/
class CMarkdownPreviewWnd {
public:
	CMarkdownPreviewWnd();
	~CMarkdownPreviewWnd();

	CMarkdownPreviewWnd(const CMarkdownPreviewWnd&) = delete;
	CMarkdownPreviewWnd& operator=(const CMarkdownPreviewWnd&) = delete;

	//! 子ウィンドウを作る。作成後は非表示
	bool Create(HWND hwndParent);
	//! ウィンドウを破棄する
	void Destroy() noexcept;

	HWND GetHwnd() const noexcept { return m_hWnd; }
	bool IsCreated() const noexcept { return m_hWnd != nullptr; }

	void Show(bool visible) noexcept;
	//! 親ウィンドウのクライアント座標で位置と大きさを指定する
	void Move(int x, int y, int width, int height) noexcept;

	//! 表示する文書を差し替える。スクロール位置は可能な範囲で保つ
	void SetDocument(Document document);

	/*!
		表示している文書のパスを指定する。画像の相対パスは、このパスのフォルダを基準に、
		そのフォルダの中にあるものだけを読む。空(未保存の文書)のときは、画像を読まない。
		パスが変わると、読み込み済みの画像は捨てる。
	*/
	void SetDocumentPath(std::wstring path);

	//! QueueSource()で使うパーサーを指定する
	void SetParser(std::shared_ptr<const IMarkdownParser> parser);
	/*!
		@brief Markdownのソースの解析をバックグラウンドに依頼し、できたら表示を差し替える
		依頼が続いたときは、最新のものだけを解析して表示する(古い依頼の結果は捨てる)。
		@param[in] source	解析するソース(呼び出し側のコピー)
		@param[in] options	解析の上限
		@retval false	パーサーが未指定、またはウィンドウが未作成
	*/
	bool QueueSource(std::wstring source, const ParseOptions& options);
	//! 文字色と背景色を指定する。ほかの色(罫線、コードの背景など)はここから決める
	void SetColors(COLORREF text, COLORREF background);
	/*!
		エディタのフォントを指定する。
		コードは同じフォント、本文は同じ大きさのUIフォントで描く。
		@param[in] font	lfHeightは画面のピクセル単位(負なら文字の高さ)
	*/
	void SetEditorFont(const LOGFONTW& font);
	//! 先頭へスクロールする
	void ScrollToTop();

	/*!
		文書を、書式付きの HTML とプレーンテキストでクリップボードに入れる。
		Teams、Outlook、Word などに貼ると、表や見出しがそのまま使える。
		表示できているローカルの画像は、data: URI で埋め込む。
		@retval false	文書が空、またはクリップボードを使えなかった
	*/
	bool CopyAsHtml();
	/*!
		文書全体を、プレビューと同じ見た目の画像(PNG と DIB)にしてクリップボードに入れる。
		幅はプレビューの幅。高さが上限を超えるときは、上の部分だけにする。
		@retval false	文書が空、またはクリップボードを使えなかった
	*/
	bool CopyAsImage();
	/*!
		プレビューの内容を印刷する。プリンターを選ぶダイアログを出す。
		プリンターの解像度でレイアウトし直し、白地に黒で、余白 15mm、下にページ番号を付けて印刷する。
		@retval false	文書が空、キャンセル、または印刷に失敗した
	*/
	bool Print();

	//! 最後にレイアウトした結果の高さ(テスト・診断用)
	int GetContentHeight() const noexcept { return m_layout.height; }
	int GetScrollPosition() const noexcept { return m_scrollY; }

private:
	//! 描画の範囲を探すための、項目の高さの最大
	struct LayoutBounds {
		int		text = 0;
		int		fill = 0;
		int		image = 0;
	};

	struct PreviewColors {
		COLORREF text = RGB(0, 0, 0);
		COLORREF background = RGB(255, 255, 255);
		COLORREF link = RGB(0, 102, 204);
		COLORREF border = RGB(200, 200, 200);
		COLORREF quoteBar = RGB(170, 170, 170);
		COLORREF codeBackground = RGB(240, 240, 240);
		COLORREF tableHeader = RGB(235, 235, 235);
	};

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	void OnPaint();
	void OnSize();
	void OnVScroll(UINT code);
	void OnMouseWheel(int delta);
	void OnKeyDown(UINT key);
	void OnContextMenu(int x, int y);
	bool OnAccelerator(WORD id);
	void OnParsed();
	void OnImagesLoaded();
	ImageSize LookupImage(const std::wstring& target);
	void ClearImages() noexcept;

	static PreviewColors MakeColors(COLORREF text, COLORREF background) noexcept;
	static LayoutBounds MeasureBounds(const LayoutResult& layout) noexcept;
	//! dpi に合わせたレイアウトの設定(余白などの寸法)
	LayoutSettings MakeLayoutSettings(UINT dpi, int width) const noexcept;
	//! 本文とコードのフォント。lfHeight は m_dpi のピクセル単位
	void GetBaseFonts(LOGFONTW& body, LOGFONTW& mono) const;
	//! 読み込み済みの画像の大きさを、dpi に合わせて返す(読み込みの依頼はしない)
	ImageSize LookupLoadedImage(const std::wstring& target, UINT dpi) const;
	void EnsureResources(HDC hdc);
	void Relayout();
	bool EnsureLayout();
	void UpdateScrollBar();
	void ScrollTo(int position);
	void ScrollBy(int delta);
	int ScaleDpi(int pixelsAt96) const noexcept;
	void PaintTo(HDC hdc, const RECT& area);
	//! レイアウトの結果を描く。offsetY は文書の座標から dc の座標を引く量(スクロール位置)
	//! printer のときは、AlphaBlend を使わずに画像を白地に合成して描く(対応しないプリンターがあるため)
	void DrawLayout(HDC dc, const LayoutResult& layout, const LayoutBounds& bounds, CPreviewFontCache& fonts,
		const PreviewColors& colors, bool printer, int originX, int offsetY, int viewTop, int viewBottom);
	//! 表示できている画像のファイルを data: URI にする。budget は残りのバイト数で、使った分を減らす
	std::string ImageDataUri(const std::wstring& target, std::size_t& budget);
	void DeriveColors();

	HWND							m_hWnd = nullptr;
	Document						m_document;
	LayoutResult					m_layout;
	LayoutSettings					m_layoutSettings;
	int								m_layoutWidth = -1;		//!< 最後にレイアウトしたときの幅
	bool							m_layoutDirty = true;
	LayoutBounds					m_bounds;	//!< m_layout の項目の高さの最大(描画範囲の検索用)
	int								m_scrollY = 0;
	int								m_wheelRemainder = 0;
	int								m_clientWidth = 0;
	int								m_clientHeight = 0;
	UINT							m_dpi = 96;
	PreviewColors					m_colors;
	COLORREF						m_userText = CLR_INVALID;
	COLORREF						m_userBackground = CLR_INVALID;
	LOGFONTW						m_editorFont = {};
	bool							m_hasEditorFont = false;
	std::unique_ptr<CPreviewFontCache>	m_fonts;
	std::shared_ptr<const IMarkdownParser>	m_parser;
	std::unique_ptr<CMarkdownPreviewWorker>	m_worker;

	//! 読み込んだ画像。キーは、読み込み先の絶対パス
	struct ImageEntry {
		enum class State { Loading, Ready, Failed };
		State		state = State::Loading;
		HBITMAP		bitmap = nullptr;
		int			width = 0;
		int			height = 0;
	};
	std::unordered_map<std::wstring, ImageEntry>	m_images;
	std::uint64_t									m_imagePixels = 0;	//!< 読み込み済みの画像の画素数の合計
	std::wstring									m_documentPath;
	std::unique_ptr<CMarkdownImageLoader>			m_imageLoader;

	//! 前回の印刷で選んだプリンターと設定(PrintDlgW の hDevMode, hDevNames)
	HGLOBAL											m_printDevMode = nullptr;
	HGLOBAL											m_printDevNames = nullptr;
};

} // namespace markdown

#endif /* SAKURA_CMARKDOWNPREVIEWWND_A3D70B94_5E26_4C1F_9B83_6F02E8D1C547_H_ */
