/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNCODETOKENIZER_8E2B5D17_4C3A_4F9E_9A61_2D7C0B4E5F83_H_
#define SAKURA_MARKDOWNCODETOKENIZER_8E2B5D17_4C3A_4F9E_9A61_2D7C0B4E5F83_H_
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace markdown {

//! コードブロックの中の字句の種類
enum class ECodeTokenKind : std::uint8_t {
	Keyword,	//!< キーワード
	String,		//!< 文字列
	Comment,	//!< コメント
	Number,		//!< 数値と定数(true、null など)
	Variable,	//!< 変数、プロパティ名、キー
	Function,	//!< 関数名、コマンド名
	Tag,		//!< HTMLやXMLのタグ
	Attribute,	//!< HTMLやXMLの属性名
	Inserted,	//!< 差分の追加行
	Deleted,	//!< 差分の削除行
};

//! 字句(行の中の[nBegin, nEnd)の範囲)
struct CodeToken {
	size_t			nBegin = 0;
	size_t			nEnd = 0;
	ECodeTokenKind	eKind = ECodeTokenKind::Keyword;

	bool operator==(const CodeToken&) const = default;
};

//! コードブロックの言語
enum class ECodeLanguage : std::uint8_t {
	None,		//!< 色分けしない(text など)
	Generic,	//!< 言語の指定がない、または知らない言語。文字列、数値、コメントだけ
	Shell,		//!< sh、bash など
	PowerShell,
	Python,
	JavaScript,	//!< JavaScript、TypeScript
	Json,
	Yaml,
	Sql,
	CLike,		//!< C、C++、C#、Java、Go、Rust など
	Html,		//!< HTML、XML
	Ini,		//!< ini、toml、properties など
	Dockerfile,
	Diff,
};

/*!
	@brief フェンスの情報文字列(```python の python の部分)から、言語を決める
	@param[in] infoString 情報文字列。最初の語だけを見る。大文字と小文字は区別しない
*/
ECodeLanguage GetCodeLanguage(std::wstring_view infoString);

/*!
	@brief コードブロックの1行を字句に分ける

	行ごとに独立して調べるので、行をまたぐ文字列やコメントは、その行の分だけを扱う。
	色を付けない部分は返さない。字句は先頭から順に並び、重ならない。

	@param[in] eLanguage	言語
	@param[in] line			行の文字列。末尾の改行はあってもなくてもよい(改行は字句に含めない)
*/
std::vector<CodeToken> TokenizeCodeLine(ECodeLanguage eLanguage, std::wstring_view line);

} // namespace markdown

#endif /* SAKURA_MARKDOWNCODETOKENIZER_8E2B5D17_4C3A_4F9E_9A61_2D7C0B4E5F83_H_ */
