/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/MarkdownLayout.h"
#include "markdown/MarkdownText.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace markdown;

namespace {

/*!
	テスト用の文字幅。
	東アジアの文字は20px、それ以外は10px(どちらも本文の大きさのとき)。高さは本文で20px。
	デバイスコンテキストに依存しないので、結果が環境によって変わらない。
*/
class FakeMeasurer final : public ITextMeasurer {
public:
	int Width(const FontSpec& font, std::wstring_view text) override
	{
		int total = 0;
		std::size_t pos = 0;
		while (pos < text.size()) {
			std::size_t len = 1;
			const std::uint32_t cp = text::CodePointAt(text, pos, &len);
			total += text::IsWide(cp) ? 20 : 10;
			pos += len;
		}
		return total * font.sizePercent / 100;
	}

	int LineHeight(const FontSpec& font) override
	{
		return 20 * font.sizePercent / 100;
	}

	ImageSize MeasureImage(const std::wstring& target) override
	{
		const auto found = images.find(target);
		return found == images.end() ? ImageSize{} : found->second;
	}

	std::map<std::wstring, ImageSize> images;	//!< 読み込み済みの画像
};

LayoutSettings Settings(int width)
{
	LayoutSettings settings;
	settings.width = width;
	settings.padding = 10;
	settings.lineSpacingPercent = 100;	// 計算を単純にするため行間は付けない
	settings.blockGap = 10;
	settings.listItemGap = 2;
	return settings;
}

InlineRun Text(std::wstring text, std::uint8_t style = InlineStyle::None)
{
	InlineRun run;
	run.text = std::move(text);
	run.style = style;
	return run;
}

Block Paragraph(std::wstring text)
{
	Block block;
	block.kind = BlockKind::Paragraph;
	block.runs.push_back(Text(std::move(text)));
	return block;
}

Document DocOf(std::vector<Block> blocks)
{
	Document doc;
	doc.blocks = std::move(blocks);
	return doc;
}

LayoutResult Layout(const Document& doc, int width)
{
	FakeMeasurer measurer;
	return BuildLayout(doc, measurer, Settings(width));
}

//! 行(y座標)ごとに文字列をつなげる
std::map<int, std::wstring> LinesOf(const LayoutResult& result)
{
	std::map<int, std::wstring> lines;
	for (const auto& item : result.texts) {
		lines[item.y] += item.text;
	}
	return lines;
}

} // namespace

TEST(MarkdownLayout, EmptyDocumentHasOnlyPadding)
{
	const auto result = Layout(Document{}, 300);
	EXPECT_EQ(result.height, 20);
	EXPECT_TRUE(result.texts.empty());
	EXPECT_TRUE(result.fills.empty());
}

TEST(MarkdownLayout, ShortParagraphIsPlacedAtThePadding)
{
	const auto result = Layout(DocOf({ Paragraph(L"abc") }), 300);
	ASSERT_EQ(result.texts.size(), 1U);
	EXPECT_EQ(result.texts[0].x, 10);
	EXPECT_EQ(result.texts[0].y, 10);
	EXPECT_EQ(result.texts[0].width, 30);
	EXPECT_EQ(result.texts[0].text, L"abc");
	EXPECT_EQ(result.height, 10 + 20 + 10);
}

TEST(MarkdownLayout, EnglishWrapsAtSpacesAndDropsTrailingSpaces)
{
	// 幅100(余白を除くと80)。1文字10px。"aaa bbb ccc ddd"
	const auto result = Layout(DocOf({ Paragraph(L"aaa bbb ccc ddd") }), 100);
	const auto lines = LinesOf(result);
	ASSERT_EQ(lines.size(), 2U);
	auto it = lines.begin();
	EXPECT_EQ(it->second, L"aaa bbb");
	++it;
	EXPECT_EQ(it->second, L"ccc ddd");
	for (const auto& item : result.texts) {
		EXPECT_TRUE(item.x + item.width <= 100 - 10);
	}
}

TEST(MarkdownLayout, LongWordIsSplitToFit)
{
	const auto result = Layout(DocOf({ Paragraph(std::wstring(35, L'x')) }), 100);	// 幅80は8文字
	const auto lines = LinesOf(result);
	ASSERT_EQ(lines.size(), 5U);
	std::wstring joined;
	for (const auto& [y, line] : lines) {
		EXPECT_TRUE(line.size() <= 8U);
		joined += line;
	}
	EXPECT_EQ(joined, std::wstring(35, L'x'));
}

TEST(MarkdownLayout, JapaneseWrapsPerCharacter)
{
	// 1文字20px。幅100(余白を除くと80)で4文字ずつ
	const auto result = Layout(DocOf({ Paragraph(L"あいうえおかきくけこ") }), 100);
	const auto lines = LinesOf(result);
	ASSERT_EQ(lines.size(), 3U);
	auto it = lines.begin();
	EXPECT_EQ(it->second, L"あいうえ");
	++it;
	EXPECT_EQ(it->second, L"おかきく");
	++it;
	EXPECT_EQ(it->second, L"けこ");
}

TEST(MarkdownLayout, KinsokuKeepsPunctuationOffTheStartOfALine)
{
	// 5文字目が句点になる位置。句点が行頭に来ないよう、直前の文字ごと次の行へ送る
	const auto result = Layout(DocOf({ Paragraph(L"あいうえ。おかきく、けこ") }), 100);
	const auto lines = LinesOf(result);
	for (const auto& [y, line] : lines) {
		ASSERT_TRUE(!line.empty());
		EXPECT_FALSE(text::IsNoBreakBefore(line.front()));
		EXPECT_FALSE(text::IsNoBreakAfter(line.back()));
	}
	std::wstring joined;
	for (const auto& [y, line] : lines) joined += line;
	EXPECT_EQ(joined, L"あいうえ。おかきく、けこ");
}

TEST(MarkdownLayout, OpeningBracketIsNotLeftAtTheEndOfALine)
{
	const auto result = Layout(DocOf({ Paragraph(L"あいう「えお」かき") }), 100);
	const auto lines = LinesOf(result);
	for (const auto& [y, line] : lines) {
		EXPECT_FALSE(text::IsNoBreakAfter(line.back()));
	}
}

TEST(MarkdownLayout, LeadingFullWidthSpaceIsKeptAsIndent)
{
	const auto result = Layout(DocOf({ Paragraph(L"　吾輩は猫である") }), 300);
	ASSERT_EQ(result.texts.size(), 1U);
	EXPECT_EQ(result.texts[0].text, L"　吾輩は猫である");
}

TEST(MarkdownLayout, NarrowOrZeroWidthDoesNotHang)
{
	for (const int width : { -5, 0, 1, 15, 25 }) {
		const auto result = Layout(DocOf({ Paragraph(L"日本語のtext混じりの文章 with spaces") }), width);
		std::wstring joined;
		for (const auto& item : result.texts) joined += item.text;
		EXPECT_FALSE(joined.empty());
	}
}

TEST(MarkdownLayout, HardBreakStartsANewLine)
{
	Block block;
	block.runs.push_back(Text(L"一行目"));
	InlineRun lineBreak;
	lineBreak.kind = InlineKind::LineBreak;
	block.runs.push_back(lineBreak);
	block.runs.push_back(Text(L"二行目"));
	const auto lines = LinesOf(Layout(DocOf({ block }), 400));
	ASSERT_EQ(lines.size(), 2U);
	EXPECT_EQ(lines.begin()->second, L"一行目");
	EXPECT_EQ(std::next(lines.begin())->second, L"二行目");
}

TEST(MarkdownLayout, InlineStylesSelectTheFont)
{
	Block block;
	block.runs.push_back(Text(L"a "));
	block.runs.push_back(Text(L"b", InlineStyle::Bold));
	block.runs.push_back(Text(L" c", InlineStyle::Italic | InlineStyle::Strike));
	block.runs.push_back(Text(L" d", InlineStyle::Code));
	block.runs.push_back(Text(L" e", InlineStyle::Link));
	const auto result = Layout(DocOf({ block }), 500);
	bool bold = false, italic = false, strike = false, mono = false, underline = false;
	for (const auto& item : result.texts) {
		bold |= item.font.bold;
		italic |= item.font.italic;
		strike |= item.font.strike;
		mono |= item.font.mono;
		underline |= item.font.underline;
	}
	EXPECT_TRUE(bold && italic && strike && mono && underline);
	// インラインコードには背景を付ける
	int codeFills = 0;
	for (const auto& fill : result.fills) {
		if (fill.kind == FillKind::InlineCodeBackground) ++codeFills;
	}
	EXPECT_EQ(codeFills, 1);
}

TEST(MarkdownLayout, WordSplitAcrossStylesStaysTogether)
{
	// foo**bar** は途中で折り返さない
	Block block;
	block.runs.push_back(Text(L"xx foo"));
	block.runs.push_back(Text(L"bar", InlineStyle::Bold));
	const auto result = Layout(DocOf({ block }), 80);	// 余白を除いて60。"foobar"は60
	const auto lines = LinesOf(result);
	ASSERT_EQ(lines.size(), 2U);
	EXPECT_EQ(lines.begin()->second, L"xx");
	EXPECT_EQ(std::next(lines.begin())->second, L"foobar");
}

TEST(MarkdownLayout, HeadingIsLargerAndBold)
{
	Block heading;
	heading.kind = BlockKind::Heading;
	heading.level = 1;
	heading.runs.push_back(Text(L"題"));
	const auto result = Layout(DocOf({ heading, Paragraph(L"本文") }), 400);
	ASSERT_EQ(result.texts.size(), 2U);
	EXPECT_EQ(result.texts[0].font.sizePercent, 200);
	EXPECT_TRUE(result.texts[0].font.bold);
	EXPECT_EQ(result.texts[0].height, 40);
	EXPECT_EQ(result.texts[1].font.sizePercent, 100);
	EXPECT_TRUE(result.texts[1].y > result.texts[0].y + 40);
}

TEST(MarkdownLayout, ListMarkersSitLeftOfTheText)
{
	Block first = Paragraph(L"項目1");
	first.listDepth = 1;
	first.marker = L"•";
	Block second = Paragraph(L"項目2");
	second.listDepth = 1;
	second.marker = L"•";
	const auto result = Layout(DocOf({ first, second }), 400);
	ASSERT_EQ(result.texts.size(), 4U);
	std::vector<TextItem> markers, bodies;
	for (const auto& item : result.texts) {
		(item.text == L"•" ? markers : bodies).push_back(item);
	}
	ASSERT_EQ(markers.size(), 2U);
	ASSERT_EQ(bodies.size(), 2U);
	EXPECT_TRUE(markers[0].x + markers[0].width < bodies[0].x);
	EXPECT_EQ(bodies[0].x, 10 + 24);
	// 項目どうしの間隔は狭い
	EXPECT_EQ(bodies[1].y - (bodies[0].y + 20), 2);
}

TEST(MarkdownLayout, NestedListIsIndentedFurther)
{
	Block outer = Paragraph(L"外");
	outer.listDepth = 1;
	outer.marker = L"1.";
	Block inner = Paragraph(L"内");
	inner.listDepth = 2;
	inner.marker = L"2.";
	const auto result = Layout(DocOf({ outer, inner }), 400);
	int outerX = -1, innerX = -1;
	for (const auto& item : result.texts) {
		if (item.text == L"外") outerX = item.x;
		if (item.text == L"内") innerX = item.x;
	}
	EXPECT_EQ(innerX - outerX, 24);
}

TEST(MarkdownLayout, QuoteBarSpansConsecutiveQuotedBlocks)
{
	Block a = Paragraph(L"引用1");
	a.quoteDepth = 1;
	Block b = Paragraph(L"引用2");
	b.quoteDepth = 1;
	const auto result = Layout(DocOf({ a, Paragraph(L"通常"), b, b }), 400);
	std::vector<FillItem> bars;
	for (const auto& fill : result.fills) {
		if (fill.kind == FillKind::QuoteBar) bars.push_back(fill);
	}
	ASSERT_EQ(bars.size(), 3U);	// a, b, b。後ろの2つはつながるが、ブロックごとに1本ずつ出る
	EXPECT_EQ(bars[0].x, 10);
	// 引用の本文は縦線の分だけ右にずれる
	for (const auto& item : result.texts) {
		if (item.text == L"引用1") EXPECT_EQ(item.x, 10 + 16);
		if (item.text == L"通常") EXPECT_EQ(item.x, 10);
	}
	// 連続する引用はすき間なくつながる
	EXPECT_EQ(bars[1].y + bars[1].height, bars[2].y);
}

TEST(MarkdownLayout, CodeBlockHasBackgroundAndKeepsWhitespace)
{
	Block code;
	code.kind = BlockKind::CodeBlock;
	code.code = L"if (x) {\n\treturn  1;\n}";
	const auto result = Layout(DocOf({ code }), 400);
	int backgrounds = 0;
	for (const auto& fill : result.fills) {
		if (fill.kind == FillKind::CodeBackground) ++backgrounds;
	}
	EXPECT_EQ(backgrounds, 1);
	const auto lines = LinesOf(result);
	ASSERT_EQ(lines.size(), 3U);
	auto it = lines.begin();
	++it;
	EXPECT_EQ(it->second, L"    return  1;");	// タブは桁をそろえて展開し、連続する空白は詰めない
	for (const auto& item : result.texts) {
		EXPECT_TRUE(item.font.mono);
	}
}

TEST(MarkdownLayout, CodeBlockWrapsLongLinesByCharacter)
{
	Block code;
	code.kind = BlockKind::CodeBlock;
	code.code = std::wstring(50, L'z');
	const auto result = Layout(DocOf({ code }), 200);
	EXPECT_TRUE(LinesOf(result).size() > 1U);
	for (const auto& item : result.texts) {
		EXPECT_TRUE(item.x + item.width <= 200 - 10);
	}
}

TEST(MarkdownLayout, EmptyCodeBlockStillDrawsBackground)
{
	Block code;
	code.kind = BlockKind::CodeBlock;
	const auto result = Layout(DocOf({ code }), 200);
	ASSERT_EQ(result.fills.size(), 1U);
	EXPECT_TRUE(result.fills[0].height > 0);
}

TEST(MarkdownLayout, ThematicBreakIsARule)
{
	Block rule;
	rule.kind = BlockKind::ThematicBreak;
	const auto result = Layout(DocOf({ Paragraph(L"上"), rule, Paragraph(L"下") }), 300);
	int rules = 0;
	for (const auto& fill : result.fills) {
		if (fill.kind == FillKind::Rule) {
			++rules;
			EXPECT_EQ(fill.width, 300 - 20);
		}
	}
	EXPECT_EQ(rules, 1);
}

namespace {

TableCell Cell(std::wstring text, TableAlign align = TableAlign::Default)
{
	TableCell cell;
	cell.runs.push_back(Text(std::move(text)));
	cell.align = align;
	return cell;
}

Block MakeTable()
{
	Block table;
	table.kind = BlockKind::Table;
	TableRow header;
	header.header = true;
	header.cells = { Cell(L"名前"), Cell(L"値", TableAlign::Right), Cell(L"備考", TableAlign::Center) };
	TableRow row;
	row.cells = { Cell(L"あ"), Cell(L"12345"), Cell(L"x") };
	table.rows = { header, row };
	return table;
}

} // namespace

TEST(MarkdownLayout, TableHasGridAndHeaderBackground)
{
	const auto result = Layout(DocOf({ MakeTable() }), 600);
	int horizontal = 0, vertical = 0, headers = 0;
	for (const auto& fill : result.fills) {
		if (fill.kind == FillKind::TableBorder) {
			(fill.width > fill.height ? horizontal : vertical)++;
		}
		if (fill.kind == FillKind::TableHeaderBackground) ++headers;
	}
	EXPECT_EQ(horizontal, 3);	// 行数+1
	EXPECT_EQ(vertical, 4);		// 列数+1
	EXPECT_EQ(headers, 1);
	for (const auto& item : result.texts) {
		if (item.text == L"名前") EXPECT_TRUE(item.font.bold);
		if (item.text == L"あ") EXPECT_FALSE(item.font.bold);
	}
}

TEST(MarkdownLayout, TableCellsFollowAlignment)
{
	const auto result = Layout(DocOf({ MakeTable() }), 600);
	TextItem value, valueHeader;
	for (const auto& item : result.texts) {
		if (item.text == L"12345") value = item;
		if (item.text == L"値") valueHeader = item;
	}
	// 右揃えの列では、見出しと値の右端がそろう
	EXPECT_EQ(valueHeader.x + valueHeader.width, value.x + value.width);
}

TEST(MarkdownLayout, WideTableIsShrunkToTheAvailableWidthAndCellsWrap)
{
	Block table;
	table.kind = BlockKind::Table;
	TableRow row;
	row.header = true;
	row.cells = { Cell(std::wstring(40, L'a') + L" " + std::wstring(40, L'b')), Cell(std::wstring(30, L'c')) };
	table.rows = { row };
	const auto result = Layout(DocOf({ table }), 300);
	int maxRight = 0;
	for (const auto& fill : result.fills) {
		maxRight = (std::max)(maxRight, fill.x + fill.width);
	}
	EXPECT_TRUE(maxRight <= 300);
	for (const auto& item : result.texts) {
		EXPECT_TRUE(item.x + item.width <= 300);
	}
}

TEST(MarkdownLayout, NarrowTableKeepsShortCellsIntact)
{
	// 長い列が幅を取っても、短い列は(日本語の1語が)途中で折れない幅を保つ
	Block table;
	table.kind = BlockKind::Table;
	TableRow header;
	header.header = true;
	header.cells = { Cell(L"ab"), Cell(L"数量"), Cell(L"備考") };
	TableRow row;
	row.cells = { Cell(L"x"), Cell(L"1"), Cell(L"long words are wrapped inside the cell when the table is too wide for the pane") };
	table.rows = { header, row };
	const auto result = Layout(DocOf({ table }), 230);
	bool foundAb = false, foundQuantity = false;
	for (const auto& item : result.texts) {
		if (item.text == L"ab") foundAb = true;
		if (item.text == L"数量") foundQuantity = true;
		EXPECT_TRUE(item.x + item.width <= 230);
	}
	EXPECT_TRUE(foundAb);
	EXPECT_TRUE(foundQuantity);
}

TEST(MarkdownLayout, CellsInTheSameRowShareABaseline)
{
	// 小さいフォント(インラインコード)だけのセルでも、同じ行の他のセルと文字の位置がそろう
	Block table;
	table.kind = BlockKind::Table;
	TableRow row;
	TableCell code;
	code.runs.push_back(Text(L"c", InlineStyle::Code));
	row.cells = { Cell(L"b"), code };
	table.rows = { row };
	FakeMeasurer measurer;
	const auto result = BuildLayout(DocOf({ table }), measurer, Settings(300));
	int bodyBottom = -1, codeBottom = -1;
	for (const auto& item : result.texts) {
		if (item.text == L"b") bodyBottom = item.y + item.height;
		if (item.text == L"c") codeBottom = item.y + item.height;
	}
	EXPECT_EQ(bodyBottom, codeBottom);
}

TEST(MarkdownLayout, EmptyTableAndEmptyCells)
{
	Block table;
	table.kind = BlockKind::Table;
	EXPECT_TRUE(Layout(DocOf({ table }), 300).height > 0);
	TableRow row;
	row.cells = { TableCell{}, TableCell{} };
	table.rows = { row };
	const auto result = Layout(DocOf({ table }), 300);
	EXPECT_TRUE(result.texts.empty());
	EXPECT_TRUE(result.fills.size() >= 4U);
}

TEST(MarkdownLayout, ImageIsShownAsItsAlternativeText)
{
	Block block;
	InlineRun image;
	image.kind = InlineKind::Image;
	image.text = L"alt";
	image.target = L"a.png";
	block.runs.push_back(image);
	const auto result = Layout(DocOf({ block }), 300);
	ASSERT_EQ(result.texts.size(), 1U);
	EXPECT_EQ(result.texts[0].text, L"[alt]");
}

TEST(MarkdownLayout, ItemsAreSortedByY)
{
	std::vector<Block> blocks;
	blocks.push_back(MakeTable());
	for (int i = 0; i < 5; ++i) blocks.push_back(Paragraph(L"段落"));
	blocks.push_back(MakeTable());
	const auto result = Layout(DocOf(blocks), 500);
	for (std::size_t i = 1; i < result.texts.size(); ++i) {
		EXPECT_TRUE(result.texts[i - 1].y <= result.texts[i].y);
	}
	for (std::size_t i = 1; i < result.fills.size(); ++i) {
		EXPECT_TRUE(result.fills[i - 1].y <= result.fills[i].y);
	}
}

TEST(MarkdownLayout, LargeDocumentCompletes)
{
	std::vector<Block> blocks;
	for (int i = 0; i < 20000; ++i) {
		blocks.push_back(Paragraph(L"これは長い日本語の段落です。日本語の文章は語の途中でも折り返せます。 mixed English words are wrapped at spaces."));
	}
	const auto result = Layout(DocOf(blocks), 400);
	EXPECT_TRUE(result.height > 20000 * 20);
	EXPECT_TRUE(result.texts.size() >= 20000U);
}

TEST(MarkdownLayout, SurrogatePairsAreNeverSplit)
{
	std::wstring text;
	for (int i = 0; i < 40; ++i) text += L"\U0001F600";
	const auto result = Layout(DocOf({ Paragraph(text) }), 100);
	for (const auto& item : result.texts) {
		ASSERT_TRUE(!item.text.empty());
		EXPECT_FALSE(text::IsLowSurrogate(static_cast<std::uint16_t>(item.text.front())));
		EXPECT_FALSE(text::IsHighSurrogate(static_cast<std::uint16_t>(item.text.back())));
	}
}

namespace {

InlineRun ImageRun(std::wstring alt, std::wstring target)
{
	InlineRun run;
	run.kind = InlineKind::Image;
	run.text = std::move(alt);
	run.target = std::move(target);
	return run;
}

ImageSize Size(int width, int height, std::wstring key = L"")
{
	ImageSize size;
	size.valid = true;
	size.width = width;
	size.height = height;
	size.key = std::move(key);
	return size;
}

LayoutResult LayoutWithImages(const Document& doc, int width, std::map<std::wstring, ImageSize> images)
{
	FakeMeasurer measurer;
	measurer.images = std::move(images);
	return BuildLayout(doc, measurer, Settings(width));
}

} // namespace

TEST(MarkdownLayout, ImagesAreScaledToTheWidth)
{
	Block block;
	block.runs.push_back(ImageRun(L"alt", L"pic.png"));
	// 幅300の中身は280。400x200の画像は、縦横比を保って280x140になる
	const auto result = LayoutWithImages(DocOf({ block }), 300, { { L"pic.png", Size(400, 200, L"C:\\x\\pic.png") } });
	ASSERT_EQ(result.images.size(), 1U);
	EXPECT_TRUE(result.texts.empty());	// 代替テキストは出さない
	EXPECT_EQ(result.images[0].width, 280);
	EXPECT_EQ(result.images[0].height, 140);
	EXPECT_EQ(result.images[0].x, 10);
	EXPECT_EQ(result.images[0].y, 10);
	EXPECT_EQ(result.images[0].key, L"C:\\x\\pic.png");
	EXPECT_EQ(result.images[0].alt, L"alt");
	EXPECT_EQ(result.height, 10 + 140 + 10);	// 画像の高さの分、行が高くなる
}

TEST(MarkdownLayout, SmallImagesKeepTheirSize)
{
	Block block;
	block.runs.push_back(ImageRun(L"alt", L"s.png"));
	const auto result = LayoutWithImages(DocOf({ block }), 300, { { L"s.png", Size(50, 30) } });
	ASSERT_EQ(result.images.size(), 1U);
	EXPECT_EQ(result.images[0].width, 50);
	EXPECT_EQ(result.images[0].height, 30);
	EXPECT_EQ(result.images[0].key, L"s.png");	// keyが空のときは、パスをそのまま使う
}

TEST(MarkdownLayout, UnavailableImagesShowTheAlternativeText)
{
	Block block;
	block.runs.push_back(ImageRun(L"代替", L"missing.png"));
	const auto result = LayoutWithImages(DocOf({ block }), 300, {});
	EXPECT_TRUE(result.images.empty());
	ASSERT_EQ(result.texts.size(), 1U);
	EXPECT_EQ(result.texts[0].text, L"[代替]");
}

TEST(MarkdownLayout, ImagesSitInTheLineWithText)
{
	Block block;
	block.runs.push_back(Text(L"前"));
	block.runs.push_back(ImageRun(L"a", L"i.png"));
	block.runs.push_back(Text(L"後"));
	const auto result = LayoutWithImages(DocOf({ block }), 400, { { L"i.png", Size(40, 60) } });
	ASSERT_EQ(result.images.size(), 1U);
	ASSERT_EQ(result.texts.size(), 2U);
	// 文字は左右にあり、下端がそろう
	const auto& image = result.images[0];
	EXPECT_EQ(result.texts[0].text, L"前");
	EXPECT_EQ(result.texts[1].text, L"後");
	EXPECT_TRUE(result.texts[0].x + result.texts[0].width <= image.x);
	EXPECT_TRUE(image.x + image.width <= result.texts[1].x);
	EXPECT_EQ(image.y + image.height, result.texts[0].y + result.texts[0].height);
	EXPECT_EQ(image.y + image.height, result.texts[1].y + result.texts[1].height);
}

TEST(MarkdownLayout, ImagesWrapToTheNextLineAndNeverSplit)
{
	Block block;
	block.runs.push_back(ImageRun(L"a", L"i.png"));
	block.runs.push_back(ImageRun(L"b", L"i.png"));
	block.runs.push_back(ImageRun(L"c", L"i.png"));
	// 中身が100で、画像は60幅。1行に1枚ずつになる
	const auto result = LayoutWithImages(DocOf({ block }), 120, { { L"i.png", Size(60, 20) } });
	ASSERT_EQ(result.images.size(), 3U);
	EXPECT_EQ(result.images[0].y, 10);
	EXPECT_EQ(result.images[1].y, 30);
	EXPECT_EQ(result.images[2].y, 50);
	for (const auto& image : result.images) {
		EXPECT_EQ(image.width, 60);
		EXPECT_TRUE(image.x + image.width <= 120 - 10);
	}
}

TEST(MarkdownLayout, ImagesInTableCellsFitTheColumn)
{
	Block table;
	table.kind = BlockKind::Table;
	TableRow row;
	TableCell cell;
	cell.runs.push_back(ImageRun(L"a", L"wide.png"));
	row.cells = { cell, Cell(L"右") };
	table.rows = { row };
	const auto result = LayoutWithImages(DocOf({ table }), 300, { { L"wide.png", Size(1000, 500) } });
	ASSERT_EQ(result.images.size(), 1U);
	EXPECT_TRUE(result.images[0].width < 1000);
	EXPECT_TRUE(result.images[0].x + result.images[0].width <= 300);
	EXPECT_NEAR(static_cast<double>(result.images[0].height) / result.images[0].width, 0.5, 0.05);	// 縦横比は保つ
}

TEST(MarkdownLayout, ImageItemsAreSortedByY)
{
	std::vector<Block> blocks;
	for (int i = 0; i < 5; ++i) {
		Block block;
		block.runs.push_back(ImageRun(L"a", L"i.png"));
		blocks.push_back(block);
		blocks.push_back(Paragraph(L"段落"));
	}
	const auto result = LayoutWithImages(DocOf(blocks), 300, { { L"i.png", Size(30, 30) } });
	ASSERT_EQ(result.images.size(), 5U);
	for (std::size_t i = 1; i < result.images.size(); ++i) {
		EXPECT_TRUE(result.images[i - 1].y < result.images[i].y);
	}
}
