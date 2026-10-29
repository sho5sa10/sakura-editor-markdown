/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/MarkdownHeadingScanner.h"

#include <string>
#include <vector>

using namespace markdown;
using ::testing::ElementsAre;

namespace {

//! 見出しを「行番号:レベル:本文」の形にして並べる
std::vector<std::wstring> Scan(const std::vector<std::wstring>& lines)
{
	CMarkdownHeadingScanner scanner;
	std::vector<std::wstring> result;
	for (const auto& line : lines) {
		if (const auto heading = scanner.Feed(line)) {
			result.push_back(std::to_wstring(heading->nLine) + L":" + std::to_wstring(heading->nLevel) + L":" + heading->strTitle);
		}
	}
	return result;
}

} // namespace

TEST(MarkdownHeadingScanner, FindsAtxHeadings)
{
	EXPECT_THAT(Scan({
		L"# 見出し1",
		L"本文",
		L"## 見出し2 ##",
		L"###### 6段目",
		L"   ### 字下げ3桁まで",
		L"#\t タブの後ろ \t",
		L"#",
	}), ElementsAre(
		L"0:1:見出し1",
		L"2:2:見出し2",
		L"3:6:6段目",
		L"4:3:字下げ3桁まで",
		L"5:1:タブの後ろ",
		L"6:1:"
	));
}

TEST(MarkdownHeadingScanner, IgnoresThingsThatAreNotAtxHeadings)
{
	EXPECT_THAT(Scan({
		L"#ハッシュタグ",
		L"####### 7個",
		L"    # 字下げ4桁はコード",
		L"\\# エスケープ",
		L"> # 引用の中",
		L"- # リストの中",
	}), IsEmpty());
}

TEST(MarkdownHeadingScanner, KeepsHashesThatAreNotAClosingSequence)
{
	EXPECT_THAT(Scan({
		L"# C#",
		L"# 番号#1 #",
		L"# ### #",
		L"## ##",
	}), ElementsAre(
		L"0:1:C#",
		L"1:1:番号#1",
		L"2:1:###",
		L"3:2:"
	));
}

TEST(MarkdownHeadingScanner, FindsSetextHeadingsOnTheParagraphLine)
{
	EXPECT_THAT(Scan({
		L"大見出し",
		L"====",
		L"",
		L"  中見出し  ",
		L"-",
		L"",
		L"段落の1行目",
		L"段落の2行目",
		L"---",
	}), ElementsAre(
		L"0:1:大見出し",
		L"3:2:中見出し",
		L"6:2:段落の1行目"
	));
}

TEST(MarkdownHeadingScanner, DoesNotTreatThematicBreaksAsSetextUnderlines)
{
	EXPECT_THAT(Scan({
		L"本文",
		L"",
		L"---",
		L"- リスト",
		L"---",
		L"> 引用",
		L"---",
		L"本文",
		L"***",
		L"本文",
		L"- - -",
		L"= = =",
	}), IsEmpty());
}

TEST(MarkdownHeadingScanner, SkipsFencedCodeBlocks)
{
	EXPECT_THAT(Scan({
		L"```python",
		L"# コメント",
		L"```",
		L"# 見出し1",
		L"~~~~",
		L"# コード",
		L"~~~",
		L"```",
		L"# まだコード",
		L"~~~~ ",
		L"# 見出し2",
		L"````",
		L"```",
		L"# コード",
		L"`````",
		L"# 見出し3",
	}), ElementsAre(
		L"3:1:見出し1",
		L"10:1:見出し2",
		L"15:1:見出し3"
	));
}

TEST(MarkdownHeadingScanner, DoesNotOpenAFenceWithBackticksInTheInfoString)
{
	EXPECT_THAT(Scan({
		L"``` a`b",
		L"# 見出し",
	}), ElementsAre(
		L"1:1:見出し"
	));
}

TEST(MarkdownHeadingScanner, SkipsFrontMatterOnlyAtTheTop)
{
	EXPECT_THAT(Scan({
		L"---",
		L"title: 題名",
		L"# YAMLのコメント",
		L"...",
		L"# 見出し1",
		L"---",
		L"# 見出し2",
		L"---",
	}), ElementsAre(
		L"4:1:見出し1",
		L"6:1:見出し2"
	));
}

TEST(MarkdownHeadingScanner, SkipsHtmlComments)
{
	EXPECT_THAT(Scan({
		L"<!-- TOC -->",
		L"# 見出し1",
		L"<!--",
		L"# コメント",
		L"-->",
		L"# 見出し2",
	}), ElementsAre(
		L"1:1:見出し1",
		L"5:1:見出し2"
	));
}

TEST(MarkdownHeadingScanner, AcceptsLinesWithEolCharacters)
{
	EXPECT_THAT(Scan({
		L"# 見出し1\r\n",
		L"見出し2\r\n",
		L"---\r\n",
		L"```\n",
		L"# コード\n",
		L"```\r\n",
	}), ElementsAre(
		L"0:1:見出し1",
		L"1:2:見出し2"
	));
}
