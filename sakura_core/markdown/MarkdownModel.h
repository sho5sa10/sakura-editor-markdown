/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNMODEL_9D3A6C51_2B7E_4F08_A1C4_5E6B0F72D813_H_
#define SAKURA_MARKDOWNMODEL_9D3A6C51_2B7E_4F08_A1C4_5E6B0F72D813_H_
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/*!
	@file MarkdownModel.h
	@brief Markdownプレビュー用の中間データモデル

	パーサー(md4cなど)の出力を、描画側が扱いやすい平坦な形にしたもの。
	このヘッダーはWin32にもパーサーのライブラリにも依存しない。
	パーサーを差し替えても、このモデルを作れば描画側は変更不要。
*/
namespace markdown {

//! インライン装飾のビットフラグ
namespace InlineStyle {
	constexpr std::uint8_t None   = 0;
	constexpr std::uint8_t Bold   = 1;	//!< 太字
	constexpr std::uint8_t Italic = 2;	//!< 斜体
	constexpr std::uint8_t Strike = 4;	//!< 取り消し線
	constexpr std::uint8_t Code   = 8;	//!< インラインコード
	constexpr std::uint8_t Link   = 16;	//!< リンク
}

//! インライン要素の種類
enum class InlineKind : std::uint8_t {
	Text,		//!< 文字列
	LineBreak,	//!< 強制改行
	Image,		//!< 画像(text=代替テキスト, target=画像のパス)
};

//! インライン要素。同じ装飾が続く範囲を1つにまとめる。
struct InlineRun {
	InlineKind		kind = InlineKind::Text;
	std::uint8_t	style = InlineStyle::None;	//!< InlineStyleの組み合わせ
	std::wstring	text;						//!< 表示する文字列。UTF-16
	std::wstring	target;						//!< リンク先、または画像のパス(未検証の文字列)
};

using InlineRuns = std::vector<InlineRun>;

//! 表の列の配置
enum class TableAlign : std::uint8_t {
	Default,
	Left,
	Center,
	Right,
};

struct TableCell {
	InlineRuns		runs;
	TableAlign		align = TableAlign::Default;
};

struct TableRow {
	bool					header = false;
	std::vector<TableCell>	cells;
};

//! ブロック要素の種類
enum class BlockKind : std::uint8_t {
	Paragraph,		//!< 段落(リスト項目の本文も含む)
	Heading,		//!< 見出し
	CodeBlock,		//!< コードブロック
	Table,			//!< 表
	ThematicBreak,	//!< 水平線
};

/*!
	ブロック要素。

	入れ子(引用・リスト)は階層構造にせず、深さの数値で表す。
	リスト項目の先頭のブロックだけがmarkerを持つ。
*/
struct Block {
	BlockKind				kind = BlockKind::Paragraph;
	int						level = 0;			//!< 見出しのレベル(1〜6)
	int						quoteDepth = 0;		//!< 引用の入れ子の深さ
	int						listDepth = 0;		//!< リストの入れ子の深さ
	std::wstring			marker;				//!< リストのマーカー。項目の先頭ブロックのみ
	InlineRuns				runs;				//!< Paragraph, Heading の内容
	std::wstring			code;				//!< CodeBlock の内容(改行はLF)
	std::wstring			language;			//!< CodeBlock の言語指定(先頭の単語)
	std::vector<TableRow>	rows;				//!< Table の内容
};

struct Document {
	std::vector<Block>		blocks;
	bool					truncated = false;	//!< 入力またはブロック数の上限で打ち切った
	bool					cancelled = false;	//!< 呼び出し側の要求(ParseOptions::shouldCancel)で解析を中止した。内容は不完全
};

} // namespace markdown

#endif /* SAKURA_MARKDOWNMODEL_9D3A6C51_2B7E_4F08_A1C4_5E6B0F72D813_H_ */
