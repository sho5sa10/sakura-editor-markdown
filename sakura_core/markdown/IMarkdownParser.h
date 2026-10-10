/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_IMARKDOWNPARSER_4C81E7A2_93B5_4D16_8F0A_17D2B6C95E34_H_
#define SAKURA_IMARKDOWNPARSER_4C81E7A2_93B5_4D16_8F0A_17D2B6C95E34_H_
#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

#include "markdown/MarkdownModel.h"

namespace markdown {

//! 解析の上限。巨大な文書でUIが固まらないようにするための値
struct ParseOptions {
	//! 解析する最大の文字数(UTF-16のコードユニット数)。超えた分は無視する
	std::size_t maxInputChars = 2U * 1024U * 1024U;
	//! 生成する最大のブロック数。超えたら解析を打ち切る
	std::size_t maxBlocks = 100000U;
	/*!
		解析の途中で呼ばれる。trueを返すと、解析を中止してDocument::cancelledを立てて戻る。
		古くなった解析を止めるために使う。解析を呼び出したスレッドで呼ばれる。空なら中止しない。
		間隔は不定(ブロックや文字列を何百か処理するごと)なので、軽い処理にすること。
	*/
	std::function<bool()> shouldCancel;
};

/*!
	@brief Markdownパーサーのインターフェース

	実装はスレッドセーフであること(Parseは複数のスレッドから同時に呼ばれ得る)。
	Parseは例外を投げず、不正な入力でもクラッシュしないこと。
*/
class IMarkdownParser {
public:
	virtual ~IMarkdownParser() = default;

	/*!
		@brief MarkdownをUTF-16の文字列から中間モデルへ変換する
		@param[in] source	Markdownのソース。CRLF/LFのどちらでもよい
		@param[in] options	解析の上限
		@return 解析結果。失敗時も、取得できた範囲のブロックを返す
	*/
	virtual Document Parse(std::wstring_view source, const ParseOptions& options) const = 0;
};

} // namespace markdown

#endif /* SAKURA_IMARKDOWNPARSER_4C81E7A2_93B5_4D16_8F0A_17D2B6C95E34_H_ */
