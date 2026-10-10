/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNLAYOUT_5F1C8E37_0A92_4B64_83D5_E7A4C19B2F60_H_
#define SAKURA_MARKDOWNLAYOUT_5F1C8E37_0A92_4B64_83D5_E7A4C19B2F60_H_
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "markdown/MarkdownModel.h"

/*!
	@file MarkdownLayout.h
	@brief Markdownプレビューのレイアウト(行の折り返しと配置)

	Win32に依存しない。文字の幅は ITextMeasurer から取得するので、
	描画に使うデバイスコンテキストがなくても単体テストできる。
	座標はすべてピクセル単位で、文書の左上が原点。
*/
namespace markdown {

//! フォントの指定。実際のHFONTへの対応付けは描画側が行う
struct FontSpec {
	int		sizePercent = 100;		//!< 本文のフォントに対する大きさ(%)
	bool	bold = false;
	bool	italic = false;
	bool	mono = false;			//!< 等幅(コード)
	bool	underline = false;
	bool	strike = false;

	bool operator==(const FontSpec&) const = default;
};

//! 画像の大きさ(読み込み済みで、表示できるもの)
struct ImageSize {
	bool			valid = false;	//!< falseなら、画像は表示できない(未読込、失敗、許可されないパスなど)
	int				width = 0;		//!< 表示するときの自然な大きさ(px)
	int				height = 0;
	std::wstring	key;			//!< 描画のときに画像を探すための名前(ImageItem::key)
};

//! 文字の寸法を返すインターフェース
class ITextMeasurer {
public:
	virtual ~ITextMeasurer() = default;
	//! 画像の大きさ。既定では、どの画像も表示できない(代替テキストを見せる)
	virtual ImageSize MeasureImage(const std::wstring& target) { (void)target; return {}; }
	//! textをfontで描いたときの幅(px)。textは改行を含まない
	virtual int Width(const FontSpec& font, std::wstring_view text) = 0;
	//! fontの行の高さ(px)。行間を含まない文字の高さ
	virtual int LineHeight(const FontSpec& font) = 0;
};

//! レイアウトの設定。ピクセル単位で、呼び出し側がDPIに合わせた値にする
struct LayoutSettings {
	int width = 600;				//!< 表示領域の幅
	int padding = 12;				//!< 上下左右の余白
	int listIndent = 24;			//!< リストの1段あたりのインデント
	int quoteIndent = 16;			//!< 引用の1段あたりのインデント
	int markerGap = 6;				//!< リストのマーカーと本文の間隔
	int blockGap = 12;				//!< ブロックの間隔
	int listItemGap = 4;			//!< リスト項目どうしの間隔
	int quoteBarWidth = 3;			//!< 引用の縦線の太さ
	int codePadding = 6;			//!< コードブロックの内側の余白
	int tableCellPadX = 8;			//!< 表のセルの左右の余白
	int tableCellPadY = 4;			//!< 表のセルの上下の余白
	int tableMinColumn = 48;		//!< 表が幅に収まらないときの列の最小幅
	int ruleThickness = 1;			//!< 線の太さ
	int tabSize = 4;				//!< コードブロックのタブ幅(桁)
	int lineSpacingPercent = 135;	//!< 行の高さ(文字の高さに対する%)
};

//! 描画する文字列
struct TextItem {
	int				x = 0;
	int				y = 0;
	int				width = 0;
	int				height = 0;
	FontSpec		font;
	std::uint8_t	style = InlineStyle::None;	//!< InlineStyle(リンクの色などの判定用)
	std::wstring	text;
};

//! 描画する画像
struct ImageItem {
	int				x = 0;
	int				y = 0;
	int				width = 0;	//!< 描画する大きさ。幅に収まるよう縮小済み
	int				height = 0;
	std::wstring	key;		//!< ImageSize::key
	std::wstring	alt;		//!< 代替テキスト
};

//! 塗りつぶしの種類
enum class FillKind : std::uint8_t {
	QuoteBar,				//!< 引用の縦線
	CodeBackground,			//!< コードブロックの背景
	InlineCodeBackground,	//!< インラインコードの背景
	Rule,					//!< 水平線
	TableHeaderBackground,	//!< 表の見出し行の背景
	TableBorder,			//!< 表の罫線
};

//! 塗りつぶす矩形
struct FillItem {
	int			x = 0;
	int			y = 0;
	int			width = 0;
	int			height = 0;
	FillKind	kind = FillKind::Rule;
};

//! レイアウトの結果。texts、fills、images は y の昇順
struct LayoutResult {
	int						height = 0;	//!< 文書全体の高さ(下の余白を含む)
	std::vector<TextItem>	texts;
	std::vector<FillItem>	fills;
	std::vector<ImageItem>	images;
};

/*!
	@brief 文書全体をレイアウトする
	@param[in] document		描画する文書
	@param[in] measurer		文字の幅を返すオブジェクト
	@param[in] settings		幅や余白
*/
LayoutResult BuildLayout(const Document& document, ITextMeasurer& measurer, const LayoutSettings& settings);

} // namespace markdown

#endif /* SAKURA_MARKDOWNLAYOUT_5F1C8E37_0A92_4B64_83D5_E7A4C19B2F60_H_ */
