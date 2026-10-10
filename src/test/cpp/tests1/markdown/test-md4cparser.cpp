/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/Md4cParser.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace markdown;

namespace {

// gtestがマクロの引数を文字列にするとき、コードページ932で表せない文字は警告になるので、定数にしておく
const wchar_t* const kBullet = L"\u2022";

Document Parse(std::wstring_view source, const ParseOptions& options = {})
{
	const auto parser = CreateMd4cParser();
	return parser->Parse(source, options);
}

//! ブロックのインライン要素の文字列をつなげる
std::wstring PlainText(const InlineRuns& runs)
{
	std::wstring text;
	for (const auto& run : runs) {
		if (run.kind == InlineKind::LineBreak) {
			text += L'\n';
		} else {
			text += run.text;
		}
	}
	return text;
}

} // namespace

TEST(Md4cParser, EmptyInput)
{
	EXPECT_TRUE(Parse(L"").blocks.empty());
	EXPECT_TRUE(Parse(L"\n\n  \r\n").blocks.empty());
}

TEST(Md4cParser, HeadingsAndJapanese)
{
	const auto doc = Parse(L"# 見出し1\n\n## 見出し2 **強調**\n\n###### 6\n");
	ASSERT_EQ(doc.blocks.size(), 3U);
	EXPECT_TRUE(doc.blocks[0].kind == BlockKind::Heading);
	EXPECT_EQ(doc.blocks[0].level, 1);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"見出し1");
	EXPECT_EQ(doc.blocks[1].level, 2);
	EXPECT_EQ(PlainText(doc.blocks[1].runs), L"見出し2 強調");
	EXPECT_EQ(doc.blocks[2].level, 6);
	EXPECT_FALSE(doc.truncated);
}

TEST(Md4cParser, ParagraphsAreSeparatedByBlankLines)
{
	const auto doc = Parse(L"最初の段落です。\n\n次の段落です。\n");
	ASSERT_EQ(doc.blocks.size(), 2U);
	EXPECT_TRUE(doc.blocks[0].kind == BlockKind::Paragraph);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"最初の段落です。");
	EXPECT_EQ(PlainText(doc.blocks[1].runs), L"次の段落です。");
}

TEST(Md4cParser, CrLfAndLfGiveTheSameResult)
{
	const auto lf = Parse(L"# 題\n\n本文1\n本文2\n\n- a\n- b\n");
	const auto crlf = Parse(L"# 題\r\n\r\n本文1\r\n本文2\r\n\r\n- a\r\n- b\r\n");
	ASSERT_EQ(lf.blocks.size(), crlf.blocks.size());
	for (std::size_t i = 0; i < lf.blocks.size(); ++i) {
		EXPECT_EQ(PlainText(lf.blocks[i].runs), PlainText(crlf.blocks[i].runs));
		EXPECT_EQ(lf.blocks[i].marker, crlf.blocks[i].marker);
	}
}

TEST(Md4cParser, SoftBreakBetweenJapaneseAddsNoSpace)
{
	// 日本語の文章を途中で改行して書いても、表示に空白を入れない
	EXPECT_EQ(PlainText(Parse(L"日本語の\n文章").blocks.at(0).runs), L"日本語の文章");
	EXPECT_EQ(PlainText(Parse(L"日本語\nabc").blocks.at(0).runs), L"日本語abc");
	EXPECT_EQ(PlainText(Parse(L"abc\n日本語").blocks.at(0).runs), L"abc日本語");
	// 英単語どうしは空白1つにする
	EXPECT_EQ(PlainText(Parse(L"hello\nworld").blocks.at(0).runs), L"hello world");
}

TEST(Md4cParser, HardBreak)
{
	const auto doc = Parse(L"一行目  \n二行目\n");
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"一行目\n二行目");
}

TEST(Md4cParser, InlineStyles)
{
	const auto doc = Parse(L"a **b** *c* ~~d~~ `e`");
	ASSERT_EQ(doc.blocks.size(), 1U);
	const auto& runs = doc.blocks[0].runs;
	ASSERT_EQ(runs.size(), 8U);
	EXPECT_EQ(runs[0].text, L"a ");
	EXPECT_EQ(runs[0].style, InlineStyle::None);
	EXPECT_EQ(runs[1].text, L"b");
	EXPECT_EQ(runs[1].style, InlineStyle::Bold);
	EXPECT_EQ(runs[3].text, L"c");
	EXPECT_EQ(runs[3].style, InlineStyle::Italic);
	EXPECT_EQ(runs[5].text, L"d");
	EXPECT_EQ(runs[5].style, InlineStyle::Strike);
	EXPECT_EQ(runs[7].text, L"e");
	EXPECT_EQ(runs[7].style, InlineStyle::Code);
}

TEST(Md4cParser, NestedStylesCombine)
{
	const auto doc = Parse(L"***太字かつ斜体***");
	ASSERT_EQ(doc.blocks.size(), 1U);
	ASSERT_EQ(doc.blocks[0].runs.size(), 1U);
	EXPECT_EQ(doc.blocks[0].runs[0].style, static_cast<std::uint8_t>(InlineStyle::Bold | InlineStyle::Italic));
	EXPECT_EQ(doc.blocks[0].runs[0].text, L"太字かつ斜体");
}

TEST(Md4cParser, LinkAndImage)
{
	const auto doc = Parse(L"[リンク](https://example.com/a?b=1&amp;c=2) と ![代替](img/pic.png)");
	ASSERT_EQ(doc.blocks.size(), 1U);
	const auto& runs = doc.blocks[0].runs;
	ASSERT_EQ(runs.size(), 3U);
	EXPECT_EQ(runs[0].text, L"リンク");
	EXPECT_EQ(runs[0].style, InlineStyle::Link);
	EXPECT_EQ(runs[0].target, L"https://example.com/a?b=1&c=2");
	EXPECT_TRUE(runs[2].kind == InlineKind::Image);
	EXPECT_EQ(runs[2].text, L"代替");
	EXPECT_EQ(runs[2].target, L"img/pic.png");
}

TEST(Md4cParser, BulletAndOrderedLists)
{
	const auto doc = Parse(L"- 項目1\n- 項目2\n\n3. 三\n4. 四\n");
	ASSERT_EQ(doc.blocks.size(), 4U);
	EXPECT_EQ(doc.blocks[0].marker, kBullet);
	EXPECT_EQ(doc.blocks[0].listDepth, 1);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"項目1");
	EXPECT_EQ(doc.blocks[1].marker, kBullet);
	EXPECT_EQ(doc.blocks[2].marker, L"3.");
	EXPECT_EQ(doc.blocks[3].marker, L"4.");
	EXPECT_EQ(PlainText(doc.blocks[3].runs), L"四");
}

TEST(Md4cParser, NestedListsAndLooseItems)
{
	const auto doc = Parse(L"- a\n  - b\n    - c\n- d\n");
	ASSERT_EQ(doc.blocks.size(), 4U);
	EXPECT_EQ(doc.blocks[0].listDepth, 1);
	EXPECT_EQ(doc.blocks[1].listDepth, 2);
	EXPECT_EQ(doc.blocks[2].listDepth, 3);
	EXPECT_EQ(doc.blocks[3].listDepth, 1);
	EXPECT_EQ(PlainText(doc.blocks[1].runs), L"b");

	// ゆるいリスト(項目の間に空行がある)でも、マーカーは項目の先頭ブロックだけが持つ
	const auto loose = Parse(L"- a\n\n  続き\n\n- b\n");
	ASSERT_EQ(loose.blocks.size(), 3U);
	EXPECT_EQ(loose.blocks[0].marker, kBullet);
	EXPECT_TRUE(loose.blocks[1].marker.empty());
	EXPECT_EQ(loose.blocks[1].listDepth, 1);
	EXPECT_EQ(loose.blocks[2].marker, kBullet);
}

TEST(Md4cParser, EmptyListItemKeepsItsMarker)
{
	const auto doc = Parse(L"- a\n-\n- c\n");
	ASSERT_EQ(doc.blocks.size(), 3U);
	EXPECT_EQ(doc.blocks[1].marker, kBullet);
	EXPECT_TRUE(doc.blocks[1].runs.empty());
}

TEST(Md4cParser, TaskList)
{
	const auto doc = Parse(L"- [x] 済み\n- [ ] 未了\n");
	ASSERT_EQ(doc.blocks.size(), 2U);
	EXPECT_EQ(doc.blocks[0].marker, L"[x]");
	EXPECT_EQ(doc.blocks[1].marker, L"[ ]");
}

TEST(Md4cParser, BlockQuote)
{
	const auto doc = Parse(L"> 引用1\n>\n> 引用2\n\n通常\n\n> 外\n>> 内\n");
	ASSERT_EQ(doc.blocks.size(), 5U);
	EXPECT_EQ(doc.blocks[0].quoteDepth, 1);
	EXPECT_EQ(doc.blocks[1].quoteDepth, 1);
	EXPECT_EQ(doc.blocks[2].quoteDepth, 0);
	EXPECT_EQ(doc.blocks[3].quoteDepth, 1);
	EXPECT_EQ(doc.blocks[4].quoteDepth, 2);
}

TEST(Md4cParser, FencedAndIndentedCodeBlocks)
{
	const auto doc = Parse(L"```cpp\nint x = 1;\n  return x;\n```\n\n    indented\n");
	ASSERT_EQ(doc.blocks.size(), 2U);
	EXPECT_TRUE(doc.blocks[0].kind == BlockKind::CodeBlock);
	EXPECT_EQ(doc.blocks[0].language, L"cpp");
	EXPECT_EQ(doc.blocks[0].code, L"int x = 1;\n  return x;");
	EXPECT_TRUE(doc.blocks[1].kind == BlockKind::CodeBlock);
	EXPECT_EQ(doc.blocks[1].code, L"indented");
}

TEST(Md4cParser, UnclosedFenceIsClosedAtEndOfDocument)
{
	const auto doc = Parse(L"```\nコード\n");
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(doc.blocks[0].code, L"コード");
}

TEST(Md4cParser, CodeBlockKeepsMarkupAndEntitiesLiteral)
{
	const auto doc = Parse(L"```\n**not bold** &amp; <b>\n```\n");
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(doc.blocks[0].code, L"**not bold** &amp; <b>");
}

TEST(Md4cParser, Table)
{
	const auto doc = Parse(
		L"| 名前 | 値 | 備考 |\n"
		L"|:-----|---:|:----:|\n"
		L"| あ | 1 | **x** |\n"
		L"| い | 2 | |\n");
	ASSERT_EQ(doc.blocks.size(), 1U);
	const auto& table = doc.blocks[0];
	EXPECT_TRUE(table.kind == BlockKind::Table);
	ASSERT_EQ(table.rows.size(), 3U);
	EXPECT_TRUE(table.rows[0].header);
	EXPECT_FALSE(table.rows[1].header);
	ASSERT_EQ(table.rows[0].cells.size(), 3U);
	EXPECT_EQ(PlainText(table.rows[0].cells[0].runs), L"名前");
	EXPECT_TRUE(table.rows[0].cells[0].align == TableAlign::Left);
	EXPECT_TRUE(table.rows[0].cells[1].align == TableAlign::Right);
	EXPECT_TRUE(table.rows[0].cells[2].align == TableAlign::Center);
	EXPECT_EQ(PlainText(table.rows[1].cells[2].runs), L"x");
	EXPECT_EQ(table.rows[1].cells[2].runs[0].style, InlineStyle::Bold);
	EXPECT_TRUE(table.rows[2].cells[2].runs.empty());
}

TEST(Md4cParser, ThematicBreak)
{
	const auto doc = Parse(L"上\n\n---\n\n下\n");
	ASSERT_EQ(doc.blocks.size(), 3U);
	EXPECT_TRUE(doc.blocks[1].kind == BlockKind::ThematicBreak);
}

TEST(Md4cParser, SurrogatePairsArePreserved)
{
	const std::wstring emoji = L"\U0001F600";
	const auto doc = Parse(L"絵文字" + emoji + L"と\U00020BB7(つちよし)");
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"絵文字" + emoji + L"と\U00020BB7(つちよし)");
}

TEST(Md4cParser, LoneSurrogatesAndNulAreReplaced)
{
	std::wstring source = L"前";
	source.push_back(static_cast<wchar_t>(0xD800));	// 孤立した上位サロゲート
	source += L"中";
	source.push_back(static_cast<wchar_t>(0xDC00));	// 孤立した下位サロゲート
	source.push_back(L'\0');
	source += L"後";
	const auto doc = Parse(source);
	ASSERT_EQ(doc.blocks.size(), 1U);
	{ const std::wstring expected = L"前" L"\uFFFD" L"中" L"\uFFFD" L"" L"\uFFFD" L"後"; EXPECT_EQ(PlainText(doc.blocks[0].runs), expected); }
}

TEST(Md4cParser, EntitiesAreDecoded)
{
	const auto doc = Parse(L"&amp; &lt;b&gt; &#35; &#x1F600; &unknownname; &#0;");
	ASSERT_EQ(doc.blocks.size(), 1U);
	{ const std::wstring expected = L"& <b> # \U0001F600 &unknownname; " L"\uFFFD" L""; EXPECT_EQ(PlainText(doc.blocks[0].runs), expected); }
}

TEST(Md4cParser, RawHtmlIsNotInterpreted)
{
	const auto block = Parse(L"<script>alert(1)</script>\n\n<div>\ntext\n</div>\n");
	std::wstring all;
	for (const auto& b : block.blocks) {
		all += PlainText(b.runs) + b.code;
	}
	EXPECT_NE(all.find(L"<script>"), std::wstring::npos);
	EXPECT_NE(all.find(L"<div>"), std::wstring::npos);

	const auto inlined = Parse(L"a <b>b</b> c <img src=x onerror=y>");
	ASSERT_EQ(inlined.blocks.size(), 1U);
	EXPECT_EQ(PlainText(inlined.blocks[0].runs), L"a <b>b</b> c <img src=x onerror=y>");
	for (const auto& run : inlined.blocks[0].runs) {
		EXPECT_TRUE(run.kind == InlineKind::Text);
	}
}

TEST(Md4cParser, VeryLongLine)
{
	const std::wstring line(1024U * 1024U, L'a');
	const auto doc = Parse(line);
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(PlainText(doc.blocks[0].runs).size(), line.size());
	EXPECT_FALSE(doc.truncated);

	const std::wstring wideLine(512U * 1024U, L'あ');
	const auto wide = Parse(wideLine);
	ASSERT_EQ(wide.blocks.size(), 1U);
	EXPECT_EQ(PlainText(wide.blocks[0].runs).size(), wideLine.size());
}

TEST(Md4cParser, InputLimitTruncates)
{
	ParseOptions options;
	options.maxInputChars = 5;
	const auto doc = Parse(L"abcdefghij", options);
	EXPECT_TRUE(doc.truncated);
	ASSERT_EQ(doc.blocks.size(), 1U);
	EXPECT_EQ(PlainText(doc.blocks[0].runs), L"abcde");

	// サロゲートペアの途中では切らない
	std::wstring source = L"a\U0001F600";
	options.maxInputChars = 2;
	const auto cut = Parse(source, options);
	EXPECT_TRUE(cut.truncated);
	ASSERT_EQ(cut.blocks.size(), 1U);
	EXPECT_EQ(PlainText(cut.blocks[0].runs), L"a");
}

TEST(Md4cParser, BlockLimitStopsParsing)
{
	ParseOptions options;
	options.maxBlocks = 2;
	const auto doc = Parse(L"1\n\n2\n\n3\n\n4\n\n5\n", options);
	EXPECT_TRUE(doc.truncated);
	EXPECT_EQ(doc.blocks.size(), 2U);
}

TEST(Md4cParser, DeepNestingDoesNotCrash)
{
	std::wstring quotes(2000, L'>');
	const auto doc = Parse(quotes + L" 深い引用\n");
	EXPECT_FALSE(doc.blocks.empty());

	std::wstring emphasis;
	for (int i = 0; i < 3000; ++i) emphasis += L"*a ";
	const auto many = Parse(emphasis);
	EXPECT_FALSE(many.blocks.empty());

	std::wstring brackets(5000, L'[');
	const auto links = Parse(brackets + L"x" + std::wstring(5000, L']') + L"(u)");
	EXPECT_FALSE(links.blocks.empty());
}

TEST(Md4cParser, ParserCanBeUsedFromSeveralThreads)
{
	const auto parser = CreateMd4cParser();
	std::vector<std::thread> threads;
	std::atomic<int> okCount = 0;
	for (int t = 0; t < 4; ++t) {
		threads.emplace_back([&] {
			for (int i = 0; i < 50; ++i) {
				const auto doc = parser->Parse(L"# 題\n\n- a\n- b\n", ParseOptions{});
				if (doc.blocks.size() == 3U) ++okCount;
			}
		});
	}
	for (auto& th : threads) th.join();
	EXPECT_EQ(okCount.load(), 200);
}
