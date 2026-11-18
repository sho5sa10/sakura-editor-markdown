/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/MarkdownHtml.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <vector>

namespace markdown {

namespace {

// 貼り付け先(Teams、Outlook、Word)で崩れにくい、控えめな書式。配色は GitHub に合わせる
constexpr char kStyleRoot[] = "font-family:'Segoe UI','Yu Gothic UI',Meiryo,sans-serif;font-size:10.5pt;line-height:1.5;color:#1f2328;";
constexpr char kStyleParagraph[] = "margin:0 0 8px 0;";
constexpr char kStyleListParagraph[] = "margin:0;";
constexpr char kStyleCodeBlock[] = "margin:0 0 8px 0;padding:8px 12px;background-color:#f6f8fa;border:1px solid #d0d7de;font-family:Consolas,'Courier New',monospace;font-size:9.5pt;line-height:1.4;white-space:pre-wrap;";
constexpr char kStyleInlineCode[] = "font-family:Consolas,'Courier New',monospace;font-size:9.5pt;background-color:#eff1f3;padding:0 3px;";
constexpr char kStyleTable[] = "border-collapse:collapse;margin:0 0 8px 0;";
constexpr char kStyleCell[] = "border:1px solid #d0d7de;padding:4px 8px;vertical-align:top;";
constexpr char kStyleHeaderCell[] = "border:1px solid #d0d7de;padding:4px 8px;vertical-align:top;background-color:#f6f8fa;font-weight:bold;";
constexpr char kStyleQuote[] = "margin:0 0 8px 0;padding:0 0 0 12px;border-left:3px solid #d0d7de;color:#59636e;";
constexpr char kStyleRule[] = "border:none;border-top:1px solid #d0d7de;margin:12px 0;";
constexpr char kStyleList[] = "margin:0 0 8px 0;padding-left:24px;";
constexpr char kStyleNestedList[] = "margin:0;padding-left:24px;";
constexpr char kStyleTaskList[] = "padding-left:24px;list-style-type:none;";
constexpr char kStyleLink[] = "color:#0969da;";

const char* HeadingStyle(int level) noexcept
{
	switch (level) {
	case 1:	 return "margin:12px 0 8px 0;font-size:18pt;font-weight:bold;border-bottom:1px solid #d0d7de;";
	case 2:	 return "margin:12px 0 8px 0;font-size:15pt;font-weight:bold;border-bottom:1px solid #d0d7de;";
	case 3:	 return "margin:10px 0 6px 0;font-size:13pt;font-weight:bold;";
	case 4:	 return "margin:10px 0 6px 0;font-size:11pt;font-weight:bold;";
	default: return "margin:8px 0 4px 0;font-size:10.5pt;font-weight:bold;";
	}
}

//! 見出しの文字の大きさ。Word は <hN> の style の font-size を使わないので、中の <span> にも書く
const char* HeadingFontStyle(int level) noexcept
{
	switch (level) {
	case 1:	 return "font-size:18pt;font-weight:bold;";
	case 2:	 return "font-size:15pt;font-weight:bold;";
	case 3:	 return "font-size:13pt;font-weight:bold;";
	case 4:	 return "font-size:11pt;font-weight:bold;";
	default: return "font-size:10.5pt;font-weight:bold;";
	}
}

std::uint32_t NextCodePoint(std::wstring_view text, std::size_t& i) noexcept
{
	const std::uint32_t c = static_cast<std::uint32_t>(text[i++]);
	if (c >= 0xD800 && c <= 0xDBFF) {
		if (i < text.size()) {
			const std::uint32_t d = static_cast<std::uint32_t>(text[i]);
			if (d >= 0xDC00 && d <= 0xDFFF) {
				++i;
				return 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
			}
		}
		return 0xFFFD;
	}
	if (c >= 0xDC00 && c <= 0xDFFF) return 0xFFFD;
	if (c > 0x10FFFF) return 0xFFFD;
	return c;
}

void AppendUtf8(std::string& out, std::uint32_t cp)
{
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

//! 文字列を HTML の本文・属性値として安全な形で追加する
void AppendEscaped(std::string& out, std::wstring_view text)
{
	std::size_t i = 0;
	while (i < text.size()) {
		const std::uint32_t cp = NextCodePoint(text, i);
		switch (cp) {
		case '&':	out += "&amp;"; break;
		case '<':	out += "&lt;"; break;
		case '>':	out += "&gt;"; break;
		case '"':	out += "&quot;"; break;
		case '\'':	out += "&#39;"; break;
		case 0:		break;
		default:
			if (cp < 0x20 && cp != '\n' && cp != '\t') break;	// 制御文字は捨てる
			AppendUtf8(out, cp);
			break;
		}
	}
}

//! リンクとして開いてよい URL か(javascript: などは、リンクにしない)
bool IsSafeLinkTarget(std::wstring_view target) noexcept
{
	const auto startsWith = [target](std::wstring_view prefix) {
		if (target.size() < prefix.size()) return false;
		for (std::size_t i = 0; i < prefix.size(); ++i) {
			if (static_cast<wchar_t>(std::towlower(static_cast<wint_t>(target[i]))) != prefix[i]) return false;
		}
		return true;
	};
	return startsWith(L"http://") || startsWith(L"https://") || startsWith(L"mailto:");
}

//! data: URI の画像だけを受け付ける(貼り付け先が外部へ読みに行かないように)
bool IsSafeImageSource(std::string_view src) noexcept
{
	constexpr std::string_view prefix = "data:image/";
	if (src.substr(0, prefix.size()) != prefix) return false;
	for (const char c : src) {
		if (c == '"' || c == '<' || c == '>' || c == '\'' || static_cast<unsigned char>(c) < 0x20) return false;
	}
	return true;
}

void AppendInlines(std::string& out, const InlineRuns& runs, const HtmlExportOptions& options)
{
	for (const auto& run : runs) {
		if (run.kind == InlineKind::LineBreak) {
			out += "<br>";
			continue;
		}
		if (run.kind == InlineKind::Image) {
			std::string src;
			if (options.imageSource) src = options.imageSource(run.target);
			if (!src.empty() && IsSafeImageSource(src)) {
				out += "<img src=\"";
				out += src;
				out += "\" alt=\"";
				AppendEscaped(out, run.text);
				out += "\" style=\"max-width:100%;\">";
			} else {
				out += '[';
				AppendEscaped(out, run.text);
				out += ']';
			}
			continue;
		}
		const bool link = (run.style & InlineStyle::Link) != 0 && IsSafeLinkTarget(run.target);
		if (link) {
			out += "<a href=\"";
			AppendEscaped(out, run.target);
			out += "\" style=\"";
			out += kStyleLink;
			out += "\">";
		}
		if (run.style & InlineStyle::Bold) out += "<strong>";
		if (run.style & InlineStyle::Italic) out += "<em>";
		if (run.style & InlineStyle::Strike) out += "<s>";	// Word は <del> を変更履歴の削除として隠すので、<s> にする
		if (run.style & InlineStyle::Code) {
			out += "<code style=\"";
			out += kStyleInlineCode;
			out += "\">";
		}
		AppendEscaped(out, run.text);
		if (run.style & InlineStyle::Code) out += "</code>";
		if (run.style & InlineStyle::Strike) out += "</s>";
		if (run.style & InlineStyle::Italic) out += "</em>";
		if (run.style & InlineStyle::Bold) out += "</strong>";
		if (link) out += "</a>";
	}
}

const char* CellAlign(TableAlign align) noexcept
{
	switch (align) {
	case TableAlign::Center:	return "text-align:center;";
	case TableAlign::Right:		return "text-align:right;";
	case TableAlign::Left:		return "text-align:left;";
	case TableAlign::Default:	break;
	}
	return "";
}

void AppendTable(std::string& out, const Block& block, const HtmlExportOptions& options)
{
	out += "<table style=\"";
	out += kStyleTable;
	out += "\">";
	bool inHead = false;
	bool inBody = false;
	for (const auto& row : block.rows) {
		if (row.header && !inHead && !inBody) {
			out += "<thead>";
			inHead = true;
		} else if (!row.header && !inBody) {
			if (inHead) {
				out += "</thead>";
				inHead = false;
			}
			out += "<tbody>";
			inBody = true;
		}
		out += "<tr>";
		for (const auto& cell : row.cells) {
			const char* tag = row.header ? "th" : "td";
			out += '<';
			out += tag;
			out += " style=\"";
			out += row.header ? kStyleHeaderCell : kStyleCell;
			out += CellAlign(cell.align);
			out += "\">";
			AppendInlines(out, cell.runs, options);
			out += "</";
			out += tag;
			out += '>';
		}
		out += "</tr>";
	}
	if (inHead) out += "</thead>";
	if (inBody) out += "</tbody>";
	out += "</table>";
}

bool IsTaskMarker(std::wstring_view marker) noexcept
{
	return marker == L"[x]" || marker == L"[ ]";
}

bool IsOrderedMarker(std::wstring_view marker) noexcept
{
	return !marker.empty() && marker[0] >= L'0' && marker[0] <= L'9';
}

//! 平らなブロックの並び(深さの数値)から、入れ子の <blockquote> と <ul>/<ol> を組み立てる
class HtmlWriter {
public:
	HtmlWriter(std::string& out, const HtmlExportOptions& options)
		: m_out(out)
		, m_options(options)
	{
	}

	void Write(const Block& block)
	{
		const int quoteDepth = (std::max)(0, block.quoteDepth);
		const std::size_t listDepth = static_cast<std::size_t>((std::max)(0, block.listDepth));
		if (quoteDepth != m_quoteDepth) {
			// 引用の深さが変わるときは、開いているリストを閉じる(リストは引用の内側とみなす)
			CloseLists(0);
			while (m_quoteDepth < quoteDepth) {
				m_out += "<blockquote style=\"";
				m_out += kStyleQuote;
				m_out += "\">";
				++m_quoteDepth;
			}
			while (m_quoteDepth > quoteDepth) {
				m_out += "</blockquote>";
				--m_quoteDepth;
			}
		}
		CloseLists(listDepth);
		if (listDepth > 0) {
			const bool startsItem = !block.marker.empty();
			if (startsItem && m_lists.size() == listDepth) {
				ListLevel& top = m_lists.back();
				if (top.itemOpen) {
					m_out += "</li>";
					top.itemOpen = false;
				}
				if (top.ordered != IsOrderedMarker(block.marker) || top.task != IsTaskMarker(block.marker)) {
					CloseLists(listDepth - 1);	// 種類の違うリストが続いた
				}
			}
			while (m_lists.size() < listDepth) {
				if (!m_lists.empty() && !m_lists.back().itemOpen) {
					m_out += "<li>";
					m_lists.back().itemOpen = true;
				}
				m_itemStart = false;
				const bool innermost = (m_lists.size() + 1 == listDepth);
				OpenList(innermost ? block.marker : std::wstring());
			}
			ListLevel& top = m_lists.back();
			if (!top.itemOpen) {
				m_out += "<li>";
				top.itemOpen = true;
				m_itemStart = true;
				if (startsItem && IsTaskMarker(block.marker)) {
					m_out += (block.marker == L"[x]") ? "\xE2\x98\x91 " : "\xE2\x98\x90 ";	// ☑ ☐
				}
			}
		}
		WriteContent(block, listDepth > 0);
		m_itemStart = false;
	}

	void Finish()
	{
		CloseLists(0);
		while (m_quoteDepth > 0) {
			m_out += "</blockquote>";
			--m_quoteDepth;
		}
	}

private:
	struct ListLevel {
		bool	ordered = false;
		bool	task = false;
		bool	itemOpen = false;
	};

	void OpenList(const std::wstring& marker)
	{
		ListLevel level;
		level.ordered = IsOrderedMarker(marker);
		level.task = IsTaskMarker(marker);
		if (level.ordered) {
			const long start = std::wcstol(marker.c_str(), nullptr, 10);
			m_out += "<ol style=\"";
			m_out += m_lists.empty() ? kStyleList : kStyleNestedList;
			m_out += '"';
			if (start != 1 && start > 0) {
				char buffer[32];
				std::snprintf(buffer, sizeof(buffer), " start=\"%ld\"", start);
				m_out += buffer;
			}
			m_out += '>';
		} else {
			m_out += "<ul style=\"";
			if (level.task) {
				m_out += m_lists.empty() ? "margin:0 0 8px 0;" : "margin:0;";
				m_out += kStyleTaskList;
			} else {
				m_out += m_lists.empty() ? kStyleList : kStyleNestedList;
			}
			m_out += "\">";
		}
		m_lists.push_back(level);
	}

	void CloseLists(std::size_t depth)
	{
		while (m_lists.size() > depth) {
			const ListLevel& level = m_lists.back();
			if (level.itemOpen) m_out += "</li>";
			m_out += level.ordered ? "</ol>" : "</ul>";
			m_lists.pop_back();
		}
	}

	void WriteContent(const Block& block, bool inList)
	{
		switch (block.kind) {
		case BlockKind::Paragraph:
			if (block.runs.empty()) return;	// マーカーだけの項目
			if (inList && m_itemStart) {
				// 項目の最初の段落は <p> で囲まない(Teams は <li> の中の <p> の前で改行するので、
				// タスクリストのチェックと本文が別の行になる)
				AppendInlines(m_out, block.runs, m_options);
				return;
			}
			m_out += "<p style=\"";
			m_out += inList ? kStyleListParagraph : kStyleParagraph;
			m_out += "\">";
			AppendInlines(m_out, block.runs, m_options);
			m_out += "</p>";
			break;
		case BlockKind::Heading: {
			const int level = (block.level < 1) ? 1 : (block.level > 6 ? 6 : block.level);
			char tag[4] = { 'h', static_cast<char>('0' + level), '\0', '\0' };
			m_out += '<';
			m_out += tag;
			m_out += " style=\"";
			m_out += HeadingStyle(level);
			m_out += "\"><span style=\"";
			m_out += HeadingFontStyle(level);
			m_out += "\">";
			AppendInlines(m_out, block.runs, m_options);
			m_out += "</span></";
			m_out += tag;
			m_out += '>';
			break;
		}
		case BlockKind::CodeBlock: {
			m_out += "<pre style=\"";
			m_out += kStyleCodeBlock;
			m_out += "\"><code>";
			std::wstring_view code = block.code;
			if (!code.empty() && code.back() == L'\n') code.remove_suffix(1);
			AppendEscaped(m_out, code);
			m_out += "</code></pre>";
			break;
		}
		case BlockKind::Table:
			AppendTable(m_out, block, m_options);
			break;
		case BlockKind::ThematicBreak:
			m_out += "<hr style=\"";
			m_out += kStyleRule;
			m_out += "\">";
			break;
		}
	}

	std::string&				m_out;
	const HtmlExportOptions&	m_options;
	int							m_quoteDepth = 0;
	std::vector<ListLevel>		m_lists;
	bool						m_itemStart = false;	//!< <li> を開いた直後で、まだ中身を書いていない
};

void AppendPlainInlines(std::wstring& out, const InlineRuns& runs)
{
	for (const auto& run : runs) {
		switch (run.kind) {
		case InlineKind::LineBreak:
			out += L"\r\n";
			break;
		case InlineKind::Image:
			out += L'[';
			out += run.text;
			out += L']';
			break;
		case InlineKind::Text:
			out += run.text;
			break;
		}
	}
}

//! 改行(LF)のあとに、行頭の文字列を差し込みながら追加する
void AppendWithPrefix(std::wstring& out, std::wstring_view text, std::wstring_view prefix)
{
	out += prefix;
	for (const wchar_t c : text) {
		if (c == L'\r') continue;
		if (c == L'\n') {
			out += L"\r\n";
			out += prefix;
		} else {
			out += c;
		}
	}
}

} // namespace

std::string ToUtf8(std::wstring_view text)
{
	std::string out;
	out.reserve(text.size());
	std::size_t i = 0;
	while (i < text.size()) {
		AppendUtf8(out, NextCodePoint(text, i));
	}
	return out;
}

std::string Base64Encode(const void* data, std::size_t size)
{
	static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const auto* bytes = static_cast<const unsigned char*>(data);
	std::string out;
	out.reserve((size + 2) / 3 * 4);
	std::size_t i = 0;
	for (; i + 2 < size; i += 3) {
		const std::uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
		out.push_back(table[(v >> 18) & 63]);
		out.push_back(table[(v >> 12) & 63]);
		out.push_back(table[(v >> 6) & 63]);
		out.push_back(table[v & 63]);
	}
	if (i < size) {
		std::uint32_t v = bytes[i] << 16;
		if (i + 1 < size) v |= bytes[i + 1] << 8;
		out.push_back(table[(v >> 18) & 63]);
		out.push_back(table[(v >> 12) & 63]);
		out.push_back((i + 1 < size) ? table[(v >> 6) & 63] : '=');
		out.push_back('=');
	}
	return out;
}

std::string ImageMimeTypeFromPath(std::wstring_view path)
{
	const std::size_t dot = path.find_last_of(L'.');
	if (dot == std::wstring_view::npos || path.find_first_of(L"\\/", dot) != std::wstring_view::npos) return {};
	std::wstring ext;
	for (const wchar_t c : path.substr(dot + 1)) ext.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))));
	if (ext == L"png") return "image/png";
	if (ext == L"jpg" || ext == L"jpeg" || ext == L"jfif") return "image/jpeg";
	if (ext == L"gif") return "image/gif";
	if (ext == L"bmp") return "image/bmp";
	if (ext == L"webp") return "image/webp";
	return {};
}

std::string BuildHtmlFragment(const Document& document, const HtmlExportOptions& options)
{
	std::string out;
	out += "<div style=\"";
	out += kStyleRoot;
	out += "\">";
	HtmlWriter writer(out, options);
	for (const auto& block : document.blocks) {
		writer.Write(block);
	}
	writer.Finish();
	out += "</div>";
	return out;
}

std::wstring BuildPlainText(const Document& document)
{
	std::wstring out;
	int previousListDepth = 0;
	for (const auto& block : document.blocks) {
		std::wstring prefix;
		for (int i = 0; i < block.quoteDepth; ++i) prefix += L"> ";
		const int listDepth = (std::max)(0, block.listDepth);
		if (listDepth > 0) prefix.append(static_cast<std::size_t>(listDepth - 1) * 2, L' ');
		// リストの中のブロックは、続けて書く(項目の間に空行を入れない)
		if (!out.empty() && (listDepth == 0 || previousListDepth == 0)) out += L"\r\n";
		previousListDepth = listDepth;
		std::wstring head = prefix;
		if (listDepth > 0) {
			if (!block.marker.empty()) {
				head += (block.marker == L"•") ? L"・" : block.marker;
				head += L' ';
			} else {
				head += L"  ";
			}
		}
		switch (block.kind) {
		case BlockKind::Paragraph:
		case BlockKind::Heading: {
			std::wstring text;
			AppendPlainInlines(text, block.runs);
			out += head;
			AppendWithPrefix(out, text, std::wstring_view());
			out += L"\r\n";
			break;
		}
		case BlockKind::CodeBlock: {
			std::wstring_view code = block.code;
			if (!code.empty() && code.back() == L'\n') code.remove_suffix(1);
			AppendWithPrefix(out, code, prefix);
			out += L"\r\n";
			break;
		}
		case BlockKind::Table:
			for (const auto& row : block.rows) {
				out += prefix;
				for (std::size_t c = 0; c < row.cells.size(); ++c) {
					if (c > 0) out += L'\t';
					std::wstring cell;
					AppendPlainInlines(cell, row.cells[c].runs);
					for (auto& ch : cell) {
						if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
					}
					out += cell;
				}
				out += L"\r\n";
			}
			break;
		case BlockKind::ThematicBreak:
			out += prefix;
			out += L"----------\r\n";
			break;
		}
	}
	return out;
}

std::string BuildClipboardHtml(std::string_view fragmentUtf8)
{
	// オフセットは、この文字列の先頭からのバイト数。桁数を固定して、先にヘッダーの長さを決める
	constexpr char kHeaderFormat[] =
		"Version:0.9\r\n"
		"StartHTML:%010zu\r\n"
		"EndHTML:%010zu\r\n"
		"StartFragment:%010zu\r\n"
		"EndFragment:%010zu\r\n";
	constexpr std::string_view kBefore =
		"<html>\r\n<head><meta charset=\"utf-8\"></head>\r\n<body>\r\n<!--StartFragment-->";
	constexpr std::string_view kAfter = "<!--EndFragment-->\r\n</body>\r\n</html>\r\n";
	char header[160];
	const int headerLength = std::snprintf(header, sizeof(header), kHeaderFormat,
		std::size_t{0}, std::size_t{0}, std::size_t{0}, std::size_t{0});
	const std::size_t startHtml = static_cast<std::size_t>(headerLength);
	const std::size_t startFragment = startHtml + kBefore.size();
	const std::size_t endFragment = startFragment + fragmentUtf8.size();
	const std::size_t endHtml = endFragment + kAfter.size();
	std::snprintf(header, sizeof(header), kHeaderFormat, startHtml, endHtml, startFragment, endFragment);

	std::string out(header, static_cast<std::size_t>(headerLength));
	out.reserve(endHtml);
	out += kBefore;
	out += fragmentUtf8;
	out += kAfter;
	return out;
}

} // namespace markdown
