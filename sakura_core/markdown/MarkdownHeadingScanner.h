/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNHEADINGSCANNER_3F8C1A52_9D47_4E1B_B6A0_7C25E94D0B18_H_
#define SAKURA_MARKDOWNHEADINGSCANNER_3F8C1A52_9D47_4E1B_B6A0_7C25E94D0B18_H_
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace markdown {

//! Markdownの見出し
struct MarkdownHeading {
	int				nLine = 0;		//!< 見出しの行(0開始)。Setext見出しは本文の最初の行
	int				nLevel = 0;		//!< 見出しのレベル(1～6)
	std::wstring	strTitle;		//!< 見出しの文字列(前後の空白と、ATX見出しの閉じの#は除く)
};

/*!
	@brief Markdownの文書を1行ずつ受け取り、見出しを見つける(アウトライン解析用)

	ATX見出し(# 見出し)と、Setext見出し(下線が=や-)を見つける。
	次のものの中は、見出しとしない。
	- フェンスで囲んだコードブロック(```や~~~)
	- 文書の先頭のYAML front matter(---で始まり、---か...で終わる)
	- 行頭から始まるHTMLのコメント(<!-- から -->まで)
	- インデントされたコードブロック(4桁以上の字下げ)

	CommonMarkの規則を、アウトラインに必要な範囲で簡略化して扱う。
	リストや引用の中の見出しは見つけない。
*/
class CMarkdownHeadingScanner {
public:
	/*!
		@brief 次の1行を渡す
		@param[in] line 行の文字列。末尾の改行はあってもなくてもよい
		@return 見出しが確定したときはその見出し。Setext見出しは下線の行を渡したときに返す
	*/
	std::optional<MarkdownHeading> Feed(std::wstring_view line);

private:
	enum class EState {
		Normal,			//!< 通常
		FrontMatter,	//!< YAML front matterの中
		Fence,			//!< フェンスで囲んだコードブロックの中
		HtmlComment,	//!< HTMLのコメントの中
	};

	bool IsFenceClose(std::wstring_view rest) const noexcept;
	void EndParagraph() noexcept;

	int				m_nLine = 0;					//!< 次に渡される行の番号
	EState			m_eState = EState::Normal;
	wchar_t			m_chFence = L'\0';				//!< 開いたフェンスの文字(`か~)
	size_t			m_nFenceLen = 0;				//!< 開いたフェンスの長さ
	int				m_nParagraphLine = -1;			//!< 段落の最初の行。段落の中でなければ-1
	std::wstring	m_strParagraphTitle;			//!< 段落の最初の行の文字列(Setext見出しの候補)
};

} // namespace markdown

#endif /* SAKURA_MARKDOWNHEADINGSCANNER_3F8C1A52_9D47_4E1B_B6A0_7C25E94D0B18_H_ */
