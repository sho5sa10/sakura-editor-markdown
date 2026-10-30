/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "view/colors/CColor_MarkdownFence.h"

#include <string_view>

#include "doc/layout/CLayoutExInfo.h"
#include "mem/CNativeW.h"
#include "types/CType.h"

namespace {

//! 行をまたいで引き継ぐ、開いたフェンスの情報
class CLayoutColorMarkdownFenceInfo : public CLayoutColorInfo{
public:
	wchar_t					m_chFence = L'\0';
	int						m_nFenceLen = 0;
	markdown::ECodeLanguage	m_eLanguage = markdown::ECodeLanguage::Generic;

	bool IsEqual(const CLayoutColorInfo* p) const override{
		const auto* info = dynamic_cast<const CLayoutColorMarkdownFenceInfo*>(p);
		return info != nullptr
			&& info->m_chFence == m_chFence
			&& info->m_nFenceLen == m_nFenceLen
			&& info->m_eLanguage == m_eLanguage;
	}
};

std::wstring_view ToView(const CStringRef& cStr) noexcept
{
	return cStr.IsValid() ? std::wstring_view(cStr.GetPtr(), cStr.GetLength()) : std::wstring_view();
}

bool IsBlank(wchar_t ch) noexcept
{
	return ch == L' ' || ch == L'\t';
}

//! 行頭の空白の後ろの位置
size_t SkipIndent(std::wstring_view line) noexcept
{
	size_t i = 0;
	while (i < line.size() && IsBlank(line[i])) ++i;
	return i;
}

//! 同じ文字が続く数
size_t CountRun(std::wstring_view s, wchar_t ch) noexcept
{
	size_t n = 0;
	while (n < s.size() && s[n] == ch) ++n;
	return n;
}

//! 空白と改行しかない
bool IsBlankToEnd(std::wstring_view s) noexcept
{
	return s.find_first_not_of(L" \t\r\n") == std::wstring_view::npos;
}

} // namespace

bool CColor_MarkdownFence::Disp() const
{
	return m_pTypeData->m_bMarkdownCodeBlock && m_pTypeData->m_ColorInfoArr[COLORIDX_COMMENT].m_bDisp;
}

EColorIndexType CColor_MarkdownFence::ToColorIndex(markdown::ECodeTokenKind eKind) noexcept
{
	using enum markdown::ECodeTokenKind;
	switch (eKind) {
	case Keyword:	return COLORIDX_KEYWORD1;
	case String:	return COLORIDX_KEYWORD2;
	case Comment:	return COLORIDX_KEYWORD3;
	case Number:	return COLORIDX_KEYWORD4;
	case Variable:	return COLORIDX_KEYWORD5;
	case Function:	return COLORIDX_KEYWORD6;
	case Tag:		return COLORIDX_KEYWORD7;
	case Attribute:	return COLORIDX_KEYWORD8;
	case Inserted:	return COLORIDX_KEYWORD9;
	case Deleted:	return COLORIDX_KEYWORD10;
	default:		return COLORIDX_MARKDOWN_FENCE;
	}
}

CLayoutColorInfo* CColor_MarkdownFence::GetStrategyColorInfo() const
{
	auto* info = new CLayoutColorMarkdownFenceInfo();
	info->m_chFence = m_chFence;
	info->m_nFenceLen = m_nFenceLen;
	info->m_eLanguage = m_eLanguage;
	return info;
}

void CColor_MarkdownFence::SetStrategyColorInfo(const CLayoutColorInfo* colorInfo)
{
	if (const auto* info = dynamic_cast<const CLayoutColorMarkdownFenceInfo*>(colorInfo)) {
		m_chFence = info->m_chFence;
		m_nFenceLen = info->m_nFenceLen;
		m_eLanguage = info->m_eLanguage;
	}
}

void CColor_MarkdownFence::InitStrategyStatus()
{
	m_nCOMMENTEND = 0;
	m_bFenceLine = false;
	m_bTokenized = false;
	m_tokens.clear();
	m_nTokenIndex = 0;
	m_nLastPos = -1;
	m_eDrawColor = COLORIDX_MARKDOWN_FENCE;
}

//! 行頭(前の空白は何桁でもよい)の ``` か ~~~ で始まる。`のフェンスは、後ろの情報文字列に`を含まない
bool CColor_MarkdownFence::BeginColor(const CStringRef& cStr, int nPos)
{
	if (nPos != 0 || !cStr.IsValid()) {
		return false;
	}
	const auto line = ToView(cStr);
	const auto rest = line.substr(SkipIndent(line));
	if (rest.empty() || (rest.front() != L'`' && rest.front() != L'~')) {
		return false;
	}
	const wchar_t ch = rest.front();
	const size_t nRun = CountRun(rest, ch);
	if (nRun < 3) {
		return false;
	}
	auto info = rest.substr(nRun);
	while (!info.empty() && (info.back() == L'\r' || info.back() == L'\n')) info.remove_suffix(1);
	if (ch == L'`' && info.find(L'`') != std::wstring_view::npos) {
		return false;
	}

	m_chFence = ch;
	m_nFenceLen = int(nRun);
	m_eLanguage = markdown::GetCodeLanguage(info);
	InitStrategyStatus();
	m_bFenceLine = true;
	m_nCOMMENTEND = cStr.GetLength();	// 開きのフェンスの行では終わらない
	return true;
}

bool CColor_MarkdownFence::IsClosingFence(const CStringRef& cStr) const
{
	const auto line = ToView(cStr);
	const auto rest = line.substr(SkipIndent(line));
	const size_t nRun = CountRun(rest, m_chFence);
	return 0 < m_nFenceLen && size_t(m_nFenceLen) <= nRun && IsBlankToEnd(rest.substr(nRun));
}

/*!
	行の先頭で、その行が閉じのフェンスかを調べる。
	閉じのフェンスなら、その行の改行の前で範囲を終える(フェンスの行もコードブロックの色にする)。
*/
bool CColor_MarkdownFence::EndColor(const CStringRef& cStr, int nPos)
{
	if (m_nCOMMENTEND == 0) {
		if (IsClosingFence(cStr)) {
			m_bFenceLine = true;
			auto line = ToView(cStr);
			while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.remove_suffix(1);
			m_nCOMMENTEND = int(line.size());
		} else {
			m_bFenceLine = false;
			m_nCOMMENTEND = cStr.GetLength();
		}
		return false;
	}
	return nPos == m_nCOMMENTEND;
}

bool CColor_MarkdownFence::UpdateDrawColor(const CStringRef& cStr, int nPos)
{
	EColorIndexType eColor = COLORIDX_MARKDOWN_FENCE;
	if (!m_bFenceLine && cStr.IsValid()) {
		if (!m_bTokenized) {
			m_tokens = markdown::TokenizeCodeLine(m_eLanguage, ToView(cStr));
			m_bTokenized = true;
			m_nTokenIndex = 0;
		}
		if (nPos < m_nLastPos) {
			m_nTokenIndex = 0;
		}
		m_nLastPos = nPos;
		while (m_nTokenIndex < m_tokens.size() && m_tokens[m_nTokenIndex].nEnd <= size_t(nPos)) {
			++m_nTokenIndex;
		}
		if (m_nTokenIndex < m_tokens.size() && m_tokens[m_nTokenIndex].nBegin <= size_t(nPos)) {
			eColor = ToColorIndex(m_tokens[m_nTokenIndex].eKind);
		}
	}
	if (eColor == m_eDrawColor) {
		return false;
	}
	m_eDrawColor = eColor;
	return true;
}
