/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNTEXT_B2E94F07_6A13_4C58_9D70_3F81A5C2E6B9_H_
#define SAKURA_MARKDOWNTEXT_B2E94F07_6A13_4C58_9D70_3F81A5C2E6B9_H_
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/*!
	@file MarkdownText.h
	@brief Markdownプレビューで使うUTF-16の文字判定

	Win32に依存しない。wchar_tが16ビットでも32ビットでも動くように、
	コードポイント単位で扱う。
*/
namespace markdown::text {

constexpr std::uint32_t kReplacementChar = 0xFFFDU;

constexpr bool IsHighSurrogate(std::uint32_t unit) noexcept { return 0xD800U <= unit && unit <= 0xDBFFU; }
constexpr bool IsLowSurrogate(std::uint32_t unit) noexcept { return 0xDC00U <= unit && unit <= 0xDFFFU; }

//! 有効なUnicodeスカラー値か(サロゲートと範囲外を除く)
constexpr bool IsScalarValue(std::uint32_t cp) noexcept
{
	return cp <= 0x10FFFFU && !(0xD800U <= cp && cp <= 0xDFFFU);
}

//! sourceのposにあるコードポイントを返す。lenに消費したコードユニット数を返す。
//! 孤立したサロゲートはU+FFFDとして扱い、1ユニットだけ消費する。
inline std::uint32_t CodePointAt(std::wstring_view source, std::size_t pos, std::size_t* len) noexcept
{
	if constexpr (sizeof(wchar_t) > 2) {
		// wchar_tが32ビットの環境(MinGW以外のLinuxなど)でも動くようにしておく
		const std::uint32_t wide = static_cast<std::uint32_t>(source[pos]);
		*len = 1;
		return IsScalarValue(wide) ? wide : kReplacementChar;
	}
	const std::uint32_t unit = static_cast<std::uint16_t>(source[pos]);
	if (IsHighSurrogate(unit)) {
		if (pos + 1 < source.size() && IsLowSurrogate(static_cast<std::uint16_t>(source[pos + 1]))) {
			const std::uint32_t low = static_cast<std::uint16_t>(source[pos + 1]);
			*len = 2;
			return 0x10000U + ((unit - 0xD800U) << 10U) + (low - 0xDC00U);
		}
		*len = 1;
		return kReplacementChar;
	}
	*len = 1;
	return IsLowSurrogate(unit) ? kReplacementChar : unit;
}

//! コードポイントをUTF-16で末尾に追加する。不正な値はU+FFFDにする。
inline void AppendCodePoint(std::wstring& out, std::uint32_t cp)
{
	if (!IsScalarValue(cp) || cp == 0) {
		cp = kReplacementChar;
	}
	if constexpr (sizeof(wchar_t) > 2) {
		out.push_back(static_cast<wchar_t>(cp));
		return;
	}
	if (cp <= 0xFFFFU) {
		out.push_back(static_cast<wchar_t>(cp));
		return;
	}
	cp -= 0x10000U;
	out.push_back(static_cast<wchar_t>(0xD800U + (cp >> 10U)));
	out.push_back(static_cast<wchar_t>(0xDC00U + (cp & 0x3FFU)));
}

//! 孤立サロゲートをU+FFFDに置き換えながら追加する。
inline void AppendSanitized(std::wstring& out, std::wstring_view source)
{
	std::size_t pos = 0;
	while (pos < source.size()) {
		std::size_t len = 1;
		const std::uint32_t cp = CodePointAt(source, pos, &len);
		if (len == 1 && cp != static_cast<std::uint16_t>(source[pos])) {
			AppendCodePoint(out, cp);	// 孤立サロゲート
		} else {
			out.append(source.substr(pos, len));
		}
		pos += len;
	}
}

/*!
	空白(折り返し時に行末へぶら下げ、行頭では捨てる文字)。
	全角空白(U+3000)は、日本語の字下げに使われるので含めない。
*/
constexpr bool IsBreakSpace(std::uint32_t cp) noexcept
{
	return cp == 0x20U || cp == 0x09U;
}

/*!
	東アジアの文字(語の途中でも改行できる文字)か。
	漢字・かな・全角記号・絵文字などを含む。ハングルは語間に空白を使うので含めない。
*/
constexpr bool IsWide(std::uint32_t cp) noexcept
{
	return (0x2E80U <= cp && cp <= 0x303FU)		// CJK部首、記号と句読点(全角空白含む)
		|| (0x3040U <= cp && cp <= 0x30FFU)		// ひらがな、カタカナ
		|| (0x3100U <= cp && cp <= 0x31FFU)		// 注音、ハングル互換字母以外のCJK拡張
		|| (0x3200U <= cp && cp <= 0x4DBFU)		// 囲み文字、CJK統合漢字拡張A
		|| (0x4E00U <= cp && cp <= 0x9FFFU)		// CJK統合漢字
		|| (0xF900U <= cp && cp <= 0xFAFFU)		// CJK互換漢字
		|| (0xFE30U <= cp && cp <= 0xFE4FU)		// CJK互換形
		|| (0xFF00U <= cp && cp <= 0xFF60U)		// 全角英数・記号
		|| (0xFFE0U <= cp && cp <= 0xFFE6U)		// 全角記号
		|| (0x1F000U <= cp && cp <= 0x1FAFFU)	// 絵文字など
		|| (0x20000U <= cp && cp <= 0x3FFFFU);	// CJK統合漢字拡張B以降
}

//! 行頭に置いてはいけない文字(禁則処理)
inline bool IsNoBreakBefore(std::uint32_t cp) noexcept
{
	static constexpr std::wstring_view kChars =
		L"、。，．・：；？！゛゜ヽヾゝゞ々ー"	// 、。，．・：；？！゛゜ヽヾゝゞ々ー
		L"）〕］｝〉》」』】｠｣､･"							// ）〕］｝〉》」』】｠｣､･
		L"ぁぃぅぇぉっゃゅょゎ"												// ぁぃぅぇぉっゃゅょゎ
		L"ァィゥェォッャュョヮヵヶ"									// ァィゥェォッャュョヮヵヶ
		L"’”…‥‐゠"																		// ’”…‥‐゠
		L",.:;?!)]}%";
	return cp <= 0xFFFFU && kChars.find(static_cast<wchar_t>(cp)) != std::wstring_view::npos;
}

//! 行末に置いてはいけない文字(禁則処理)
inline bool IsNoBreakAfter(std::uint32_t cp) noexcept
{
	static constexpr std::wstring_view kChars =
		L"（〔［｛〈《「『【｟｢"	// （〔［｛〈《「『【｟｢
		L"‘“"															// ‘“
		L"([{";
	return cp <= 0xFFFFU && kChars.find(static_cast<wchar_t>(cp)) != std::wstring_view::npos;
}

/*!
	prevとnextの間で改行してよいか。
	- 空白の直後は改行してよい。空白の直前は改行しない(空白は行末にぶら下がる)。
	- どちらかが東アジアの文字なら、禁則に当たらない限り改行してよい。
	- 英単語の途中では改行しない。
*/
inline bool CanBreakBetween(std::uint32_t prev, std::uint32_t next) noexcept
{
	if (IsBreakSpace(next)) {
		return false;
	}
	if (IsBreakSpace(prev)) {
		return true;
	}
	if (!IsWide(prev) && !IsWide(next)) {
		return false;
	}
	return !IsNoBreakBefore(next) && !IsNoBreakAfter(prev);
}

} // namespace markdown::text

#endif /* SAKURA_MARKDOWNTEXT_B2E94F07_6A13_4C58_9D70_3F81A5C2E6B9_H_ */
