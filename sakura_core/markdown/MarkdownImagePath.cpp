/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/MarkdownImagePath.h"

#include <cstdint>
#include <vector>

#include "markdown/MarkdownText.h"

namespace markdown {

namespace {

constexpr std::size_t kMaximumPathLength = 32000;
constexpr std::size_t kMaximumComponentLength = 255;

int HexValue(wchar_t ch) noexcept
{
	if (L'0' <= ch && ch <= L'9') return ch - L'0';
	if (L'a' <= ch && ch <= L'f') return ch - L'a' + 10;
	if (L'A' <= ch && ch <= L'F') return ch - L'A' + 10;
	return -1;
}

//! %XXの並びをUTF-8のバイト列として展開し、outへ追加する。不正なUTF-8ならfalse
bool FlushBytes(std::string* bytes, std::wstring* out)
{
	if (bytes->empty()) return true;
	const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes->data(), static_cast<int>(bytes->size()), nullptr, 0);
	if (length <= 0) return false;
	const std::size_t old = out->size();
	out->resize(old + static_cast<std::size_t>(length));
	::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes->data(), static_cast<int>(bytes->size()), out->data() + old, length);
	bytes->clear();
	return true;
}

//! %XXを展開する(展開したバイト列はUTF-8として扱う)。%XXでない文字は、そのまま残す。不正ならfalse
bool PercentDecode(std::wstring_view text, std::wstring* out)
{
	out->clear();
	std::string bytes;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] != L'%') {
			if (!FlushBytes(&bytes, out)) return false;
			out->push_back(text[i]);
			continue;
		}
		if (i + 2 >= text.size()) return false;
		const int high = HexValue(text[i + 1]);
		const int low = HexValue(text[i + 2]);
		if (high < 0 || low < 0) return false;
		bytes.push_back(static_cast<char>((high << 4) | low));
		i += 2;
	}
	return FlushBytes(&bytes, out);
}

bool IsReservedDeviceName(std::wstring_view component)
{
	// 拡張子は無視する(CON.txt もデバイスとして扱われる)
	const std::size_t dot = component.find(L'.');
	std::wstring_view stem = component.substr(0, dot);
	while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
	std::wstring upper(stem);
	for (auto& ch : upper) {
		if (L'a' <= ch && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
	}
	if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" || upper == L"NUL" || upper == L"CONIN$" || upper == L"CONOUT$") {
		return true;
	}
	if (upper.size() == 4 && (upper.compare(0, 3, L"COM") == 0 || upper.compare(0, 3, L"LPT") == 0)) {
		const wchar_t digit = upper[3];
		return (L'1' <= digit && digit <= L'9');
	}
	return false;
}

bool IsSeparator(wchar_t ch) noexcept
{
	return ch == L'\\' || ch == L'/';
}

//! 文書のあるフォルダ。ドライブ付きの絶対パス、またはUNCパスのときだけ有効
std::optional<std::wstring> DocumentFolder(std::wstring_view documentPath)
{
	if (documentPath.size() < 4 || documentPath.size() > kMaximumPathLength) return std::nullopt;
	const wchar_t first = documentPath[0];
	const bool drive = ((L'A' <= first && first <= L'Z') || (L'a' <= first && first <= L'z'))
		&& documentPath[1] == L':' && IsSeparator(documentPath[2]);
	const bool unc = documentPath.size() > 2 && IsSeparator(documentPath[0]) && IsSeparator(documentPath[1])
		&& documentPath[2] != L'?' && documentPath[2] != L'.';
	if (!drive && !unc) return std::nullopt;
	const std::size_t last = documentPath.find_last_of(L"\\/");
	if (last == std::wstring_view::npos || last < 2) return std::nullopt;
	std::wstring folder(documentPath.substr(0, last));
	for (auto& ch : folder) {
		if (ch == L'/') ch = L'\\';
	}
	// ドライブの直下(C:\)は、末尾の\を残す
	if (folder.size() == 2 && folder[1] == L':') folder.push_back(L'\\');
	return folder;
}

} // namespace

std::optional<ImageLocation> ResolveLocalImagePath(std::wstring_view documentPath, std::wstring_view target)
{
	const auto folder = DocumentFolder(documentPath);
	if (!folder) return std::nullopt;

	// 前後の空白と、?や#以降を取り除く
	while (!target.empty() && (target.front() == L' ' || target.front() == L'\t')) target.remove_prefix(1);
	while (!target.empty() && (target.back() == L' ' || target.back() == L'\t')) target.remove_suffix(1);
	const std::size_t cut = target.find_first_of(L"?#");
	if (cut != std::wstring_view::npos) target = target.substr(0, cut);
	if (target.empty() || target.size() > kMaximumPathLength) return std::nullopt;

	std::wstring decoded;
	if (!PercentDecode(target, &decoded) || decoded.empty()) return std::nullopt;

	for (const wchar_t ch : decoded) {
		if (ch < 0x20 || ch == L':' || ch == L'<' || ch == L'>' || ch == L'"' || ch == L'|' || ch == L'*' || ch == L'?') {
			return std::nullopt;
		}
	}
	if (IsSeparator(decoded.front())) return std::nullopt;

	std::vector<std::wstring> parts;
	std::size_t start = 0;
	while (start <= decoded.size()) {
		std::size_t end = decoded.find_first_of(L"\\/", start);
		if (end == std::wstring::npos) end = decoded.size();
		const std::wstring part = decoded.substr(start, end - start);
		start = end + 1;
		if (part.empty() || part == L".") continue;
		if (part == L"..") {
			if (parts.empty()) return std::nullopt;	// 文書のフォルダの外に出る
			parts.pop_back();
			continue;
		}
		if (part.size() > kMaximumComponentLength) return std::nullopt;
		if (part.back() == L'.' || part.back() == L' ') return std::nullopt;
		if (IsReservedDeviceName(part)) return std::nullopt;
		parts.push_back(part);
	}
	if (parts.empty()) return std::nullopt;

	ImageLocation location;
	location.root = *folder;
	location.path = *folder;
	if (location.path.back() != L'\\') location.path.push_back(L'\\');
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) location.path.push_back(L'\\');
		location.path += parts[i];
	}
	if (location.path.size() > kMaximumPathLength) return std::nullopt;
	return location;
}

} // namespace markdown
