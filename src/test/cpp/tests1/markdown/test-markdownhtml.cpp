/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/MarkdownHtml.h"

#include <cstring>
#include <string>

using namespace markdown;

namespace {

InlineRun Text(std::wstring text, std::uint8_t style = InlineStyle::None, std::wstring target = {})
{
	InlineRun run;
	run.style = style;
	run.text = std::move(text);
	run.target = std::move(target);
	return run;
}

Block Paragraph(std::wstring text, int listDepth = 0, std::wstring marker = {}, int quoteDepth = 0)
{
	Block block;
	block.listDepth = listDepth;
	block.marker = std::move(marker);
	block.quoteDepth = quoteDepth;
	block.runs.push_back(Text(std::move(text)));
	return block;
}

bool Contains(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}

} // namespace

TEST(MarkdownHtml, EscapesText)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"<a href=\"x\">&'"));
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "&lt;a href=&quot;x&quot;&gt;&amp;&#39;"));
	EXPECT_FALSE(Contains(html, "<a href"));
}

TEST(MarkdownHtml, InlineStyles)
{
	Document doc;
	Block block;
	block.runs.push_back(Text(L"b", InlineStyle::Bold));
	block.runs.push_back(Text(L"i", InlineStyle::Italic));
	block.runs.push_back(Text(L"s", InlineStyle::Strike));
	block.runs.push_back(Text(L"c", InlineStyle::Code));
	doc.blocks.push_back(block);
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<strong>b</strong>"));
	EXPECT_TRUE(Contains(html, "<em>i</em>"));
	EXPECT_TRUE(Contains(html, "<s>s</s>"));
	EXPECT_FALSE(Contains(html, "<del>"));	// Word は <del> の文字を隠す
	EXPECT_TRUE(Contains(html, ">c</code>"));
}

TEST(MarkdownHtml, LinksOnlyForSafeSchemes)
{
	Document doc;
	Block block;
	block.runs.push_back(Text(L"ok", InlineStyle::Link, L"https://example.com/?a=1&b=2"));
	block.runs.push_back(Text(L"ng", InlineStyle::Link, L"javascript:alert(1)"));
	block.runs.push_back(Text(L"file", InlineStyle::Link, L"file:///C:/x"));
	doc.blocks.push_back(block);
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<a href=\"https://example.com/?a=1&amp;b=2\""));
	EXPECT_FALSE(Contains(html, "javascript"));
	EXPECT_FALSE(Contains(html, "file:"));
	EXPECT_TRUE(Contains(html, "ng"));
}

TEST(MarkdownHtml, ImagesUseProvidedDataUriOrAltText)
{
	Document doc;
	Block block;
	InlineRun image;
	image.kind = InlineKind::Image;
	image.text = L"図1";
	image.target = L"a.png";
	block.runs.push_back(image);
	image.target = L"b.png";
	block.runs.push_back(image);
	image.target = L"c.png";
	block.runs.push_back(image);
	doc.blocks.push_back(block);
	HtmlExportOptions options;
	options.imageSource = [](const std::wstring& target) -> std::string {
		if (target == L"a.png") return "data:image/png;base64,AAAA";
		if (target == L"b.png") return "https://example.com/b.png";	// 外部の URL は使わない
		return {};
	};
	const std::string html = BuildHtmlFragment(doc, options);
	EXPECT_TRUE(Contains(html, "<img src=\"data:image/png;base64,AAAA\""));
	EXPECT_FALSE(Contains(html, "example.com"));
	EXPECT_TRUE(Contains(html, "[\xE5\x9B\xB3" "1]"));	// [図1]
}

TEST(MarkdownHtml, NestedLists)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"a", 1, L"•"));
	doc.blocks.push_back(Paragraph(L"a-1", 2, L"3."));
	doc.blocks.push_back(Paragraph(L"b", 1, L"•"));
	doc.blocks.push_back(Paragraph(L"after"));
	const std::string html = BuildHtmlFragment(doc);
	const auto ul = html.find("<ul");
	const auto ol = html.find("<ol");
	const auto olEnd = html.find("</ol></li><li>");
	const auto ulEnd = html.find("</li></ul>");
	ASSERT_NE(ul, std::string::npos);
	ASSERT_NE(ol, std::string::npos);
	EXPECT_TRUE(Contains(html, " start=\"3\""));
	EXPECT_LT(ul, ol);
	ASSERT_NE(olEnd, std::string::npos);
	ASSERT_NE(ulEnd, std::string::npos);
	EXPECT_LT(ulEnd, html.find(">after<"));
}

TEST(MarkdownHtml, ListKindChangeStartsNewList)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"a", 1, L"•"));
	doc.blocks.push_back(Paragraph(L"b", 1, L"1."));
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "</li></ul><ol"));
}

TEST(MarkdownHtml, TaskList)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"done", 1, L"[x]"));
	doc.blocks.push_back(Paragraph(L"todo", 1, L"[ ]"));
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "list-style-type:none"));
	// チェックと本文を同じ行にするため、項目の最初の段落は <p> で囲まない
	EXPECT_TRUE(Contains(html, "<li>\xE2\x98\x91 done</li>"));	// ☑
	EXPECT_TRUE(Contains(html, "<li>\xE2\x98\x90 todo</li>"));	// ☐
}

TEST(MarkdownHtml, ListItemParagraphs)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"first", 1, L"•"));
	doc.blocks.push_back(Paragraph(L"second", 1));	// 同じ項目の2つ目の段落
	doc.blocks.push_back(Paragraph(L"next", 1, L"•"));
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<li>first<p style=\"margin:0;\">second</p></li><li>next</li>"));
}

TEST(MarkdownHtml, HeadingFontSizeInSpan)
{
	Document doc;
	Block heading;
	heading.kind = BlockKind::Heading;
	heading.level = 2;
	heading.runs.push_back(Text(L"h"));
	doc.blocks.push_back(heading);
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<h2 style=\""));
	EXPECT_TRUE(Contains(html, "<span style=\"font-size:15pt;font-weight:bold;\">h</span></h2>"));
}

TEST(MarkdownHtml, Quote)
{
	Document doc;
	doc.blocks.push_back(Paragraph(L"q", 0, {}, 2));
	doc.blocks.push_back(Paragraph(L"p"));
	const std::string html = BuildHtmlFragment(doc);
	const auto first = html.find("<blockquote");
	ASSERT_NE(first, std::string::npos);
	EXPECT_NE(html.find("<blockquote", first + 1), std::string::npos);
	EXPECT_TRUE(Contains(html, "</blockquote></blockquote><p"));
}

TEST(MarkdownHtml, Table)
{
	Document doc;
	Block table;
	table.kind = BlockKind::Table;
	TableRow head;
	head.header = true;
	head.cells.push_back({ { Text(L"名前") }, TableAlign::Default });
	head.cells.push_back({ { Text(L"数") }, TableAlign::Right });
	TableRow body;
	body.cells.push_back({ { Text(L"x") }, TableAlign::Default });
	body.cells.push_back({ { Text(L"1") }, TableAlign::Right });
	table.rows = { head, body };
	doc.blocks.push_back(table);
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<table"));
	EXPECT_TRUE(Contains(html, "<thead><tr><th"));
	EXPECT_TRUE(Contains(html, "</thead><tbody><tr><td"));
	EXPECT_TRUE(Contains(html, "text-align:right;\">1</td>"));

	const std::wstring text = BuildPlainText(doc);
	EXPECT_EQ(L"名前\t数\r\nx\t1\r\n", text);
}

TEST(MarkdownHtml, CodeBlock)
{
	Document doc;
	Block code;
	code.kind = BlockKind::CodeBlock;
	code.code = L"if (a < b) {\n}\n";
	doc.blocks.push_back(code);
	const std::string html = BuildHtmlFragment(doc);
	EXPECT_TRUE(Contains(html, "<pre style=\""));
	EXPECT_TRUE(Contains(html, "<code>if (a &lt; b) {\n}</code></pre>"));
}

TEST(MarkdownHtml, PlainText)
{
	Document doc;
	Block heading;
	heading.kind = BlockKind::Heading;
	heading.level = 1;
	heading.runs.push_back(Text(L"題"));
	doc.blocks.push_back(heading);
	doc.blocks.push_back(Paragraph(L"a", 1, L"•"));
	doc.blocks.push_back(Paragraph(L"b", 2, L"1."));
	doc.blocks.push_back(Paragraph(L"q", 0, {}, 1));
	EXPECT_EQ(L"題\r\n\r\n・ a\r\n  1. b\r\n\r\n> q\r\n", BuildPlainText(doc));
}

TEST(MarkdownHtml, ClipboardHtmlOffsets)
{
	const std::string fragment = "<b>\xE6\x97\xA5</b>";
	const std::string cf = BuildClipboardHtml(fragment);
	const auto number = [&cf](const char* key) {
		const auto pos = cf.find(key);
		return static_cast<std::size_t>(std::stoul(cf.substr(pos + std::strlen(key), 10)));
	};
	const std::size_t startHtml = number("StartHTML:");
	const std::size_t endHtml = number("EndHTML:");
	const std::size_t startFragment = number("StartFragment:");
	const std::size_t endFragment = number("EndFragment:");
	EXPECT_EQ(0U, cf.compare(startHtml, 6, "<html>"));
	EXPECT_EQ(cf.size(), endHtml);
	EXPECT_EQ(fragment, cf.substr(startFragment, endFragment - startFragment));
}

TEST(MarkdownHtml, Utf8)
{
	EXPECT_EQ("A\xE3\x81\x82\xF0\x9F\x98\x80", ToUtf8(std::wstring(L"A\u3042") + L"\xD83D\xDE00"));
	EXPECT_EQ("\xEF\xBF\xBD", ToUtf8(std::wstring(1, static_cast<wchar_t>(0xD800))));
}

TEST(MarkdownHtml, Base64)
{
	EXPECT_EQ("", Base64Encode("", 0));
	EXPECT_EQ("TQ==", Base64Encode("M", 1));
	EXPECT_EQ("TWE=", Base64Encode("Ma", 2));
	EXPECT_EQ("TWFu", Base64Encode("Man", 3));
	EXPECT_EQ("TWFuTQ==", Base64Encode("ManM", 4));
}

TEST(MarkdownHtml, ImageMimeType)
{
	EXPECT_EQ("image/png", ImageMimeTypeFromPath(L"C:\\a\\b.PNG"));
	EXPECT_EQ("image/jpeg", ImageMimeTypeFromPath(L"x.jpeg"));
	EXPECT_EQ("", ImageMimeTypeFromPath(L"x.svg"));
	EXPECT_EQ("", ImageMimeTypeFromPath(L"C:\\a.b\\noext"));
}
