/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/MarkdownHeadingScanner.h"

namespace markdown {

namespace {

bool IsSpaceOrTab(wchar_t ch) noexcept
{
	return ch == L' ' || ch == L'\t';
}

std::wstring_view TrimEol(std::wstring_view s) noexcept
{
	while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.remove_suffix(1);
	return s;
}

std::wstring_view Trim(std::wstring_view s) noexcept
{
	while (!s.empty() && IsSpaceOrTab(s.front())) s.remove_prefix(1);
	while (!s.empty() && IsSpaceOrTab(s.back())) s.remove_suffix(1);
	return s;
}

//! 行頭の字下げの桁数を返し、字下げの後ろをrestに入れる。タブは次の4の倍数の桁まで進める
size_t CountIndent(std::wstring_view line, std::wstring_view& rest) noexcept
{
	size_t nColumn = 0;
	size_t i = 0;
	for (; i < line.size(); ++i) {
		if (line[i] == L' ') {
			++nColumn;
		} else if (line[i] == L'\t') {
			nColumn = (nColumn / 4 + 1) * 4;
		} else {
			break;
		}
	}
	rest = line.substr(i);
	return nColumn;
}

//! 先頭から同じ文字が続く数
size_t CountRun(std::wstring_view s, wchar_t ch) noexcept
{
	size_t n = 0;
	while (n < s.size() && s[n] == ch) ++n;
	return n;
}

//! 区切り線(*、-、_のどれか1種類が3個以上。間に空白を挟んでよい)
bool IsThematicBreak(std::wstring_view rest) noexcept
{
	const wchar_t ch = rest.front();
	if (ch != L'*' && ch != L'-' && ch != L'_') return false;
	size_t n = 0;
	for (const wchar_t c : rest) {
		if (c == ch) {
			++n;
		} else if (!IsSpaceOrTab(c)) {
			return false;
		}
	}
	return 3 <= n;
}

//! リストの項目の始まり(-、*、+、または9桁までの数字に.か))
bool IsListItemStart(std::wstring_view rest) noexcept
{
	size_t i = 0;
	if (rest.front() == L'-' || rest.front() == L'*' || rest.front() == L'+') {
		i = 1;
	} else {
		while (i < rest.size() && i < 9 && L'0' <= rest[i] && rest[i] <= L'9') ++i;
		if (i == 0 || i == rest.size() || (rest[i] != L'.' && rest[i] != L')')) return false;
		++i;
	}
	return i == rest.size() || IsSpaceOrTab(rest[i]);
}

//! ATX見出しの本文から、前後の空白と閉じの#の並びを除く
std::wstring_view TrimClosingHashes(std::wstring_view s) noexcept
{
	s = Trim(s);
	size_t n = s.size();
	while (0 < n && s[n - 1] == L'#') --n;
	if (n == 0) return {};
	if (n < s.size() && IsSpaceOrTab(s[n - 1])) s = Trim(s.substr(0, n));
	return s;
}

} // namespace

std::optional<MarkdownHeading> CMarkdownHeadingScanner::Feed(std::wstring_view line)
{
	const int nLine = m_nLine++;
	line = TrimEol(line);

	switch (m_eState) {
	case EState::FrontMatter:
		if (const auto trimmed = Trim(line); trimmed == L"---" || trimmed == L"...") {
			m_eState = EState::Normal;
		}
		return std::nullopt;
	case EState::Fence:
		if (std::wstring_view rest; CountIndent(line, rest) < 4 && IsFenceClose(rest)) {
			m_eState = EState::Normal;
		}
		return std::nullopt;
	case EState::HtmlComment:
		if (line.find(L"-->") != std::wstring_view::npos) {
			m_eState = EState::Normal;
		}
		return std::nullopt;
	default:
		break;
	}

	if (nLine == 0 && Trim(line) == L"---") {
		m_eState = EState::FrontMatter;
		return std::nullopt;
	}

	std::wstring_view rest;
	const size_t nIndent = CountIndent(line, rest);
	if (rest.empty()) {
		EndParagraph();
		return std::nullopt;
	}
	if (4 <= nIndent) {
		// 段落の続きか、インデントされたコードブロック
		return std::nullopt;
	}

	// フェンスの始まり。`のフェンスは、後ろの情報文字列に`を含まない
	if (const wchar_t ch = rest.front(); ch == L'`' || ch == L'~') {
		const size_t n = CountRun(rest, ch);
		if (3 <= n && (ch == L'~' || rest.substr(n).find(L'`') == std::wstring_view::npos)) {
			m_eState = EState::Fence;
			m_chFence = ch;
			m_nFenceLen = n;
			EndParagraph();
			return std::nullopt;
		}
	}

	if (rest.starts_with(L"<!--")) {
		if (rest.find(L"-->", 4) == std::wstring_view::npos) {
			m_eState = EState::HtmlComment;
		}
		EndParagraph();
		return std::nullopt;
	}

	// ATX見出し: #が1～6個で、後ろが空白か行末
	if (rest.front() == L'#') {
		const size_t n = CountRun(rest, L'#');
		if (n <= 6 && (n == rest.size() || IsSpaceOrTab(rest[n]))) {
			EndParagraph();
			return MarkdownHeading{ nLine, int(n), std::wstring(TrimClosingHashes(rest.substr(n))) };
		}
	}

	// Setext見出し: 段落の直後の、=か-だけの行
	if (0 <= m_nParagraphLine && (rest.front() == L'=' || rest.front() == L'-')) {
		const size_t n = CountRun(rest, rest.front());
		if (Trim(rest.substr(n)).empty()) {
			MarkdownHeading heading{ m_nParagraphLine, rest.front() == L'=' ? 1 : 2, m_strParagraphTitle };
			EndParagraph();
			return heading;
		}
	}

	// 引用、区切り線、リストは段落を終える。それ以外は段落の行
	if (rest.front() == L'>' || IsThematicBreak(rest) || IsListItemStart(rest)) {
		EndParagraph();
		return std::nullopt;
	}
	if (m_nParagraphLine < 0) {
		m_nParagraphLine = nLine;
		m_strParagraphTitle = Trim(rest);
	}
	return std::nullopt;
}

//! フェンスの終わり: 開いたときと同じ文字が同じ数以上並び、後ろが空白だけ
bool CMarkdownHeadingScanner::IsFenceClose(std::wstring_view rest) const noexcept
{
	const size_t n = CountRun(rest, m_chFence);
	return m_nFenceLen <= n && Trim(rest.substr(n)).empty();
}

void CMarkdownHeadingScanner::EndParagraph() noexcept
{
	m_nParagraphLine = -1;
	m_strParagraphTitle.clear();
}

} // namespace markdown
