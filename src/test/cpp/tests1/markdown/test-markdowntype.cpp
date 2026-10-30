/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "types/CType.h"

#include <string>
#include <utility>
#include <vector>

#include "env/CRegexKeyword.h"
#include "env/ShareDataTestSuite.hpp"
#include "mem/CNativeW.h"
#include "view/colors/EColorIndexType.h"

using ::testing::ElementsAre;
using ::testing::Pair;

namespace {

using Span = std::pair<int, std::wstring>;	// 色指定番号と、色が付く文字列

struct CTypeMarkdownTest : public ::testing::Test, public env::ShareDataTestSuite {
	static void SetUpTestSuite()
	{
		SetUpShareData();
	}

	static void TearDownTestSuite()
	{
		TearDownShareData();
	}

	void SetUp() override
	{
		CType_Markdown().InitTypeConfig(17, type);
		pcRegexKeyword = std::make_unique<CRegexKeyword>(nullptr);
		ASSERT_THAT(pcRegexKeyword->IsAvailable(), IsTrue());
		ASSERT_THAT(pcRegexKeyword->RegexKeySetTypes(&type), IsTrue());
	}

	/*!
		1行(改行は付け足す)を、エディタと同じように先頭から調べ、正規表現キーワードで色が付く範囲を返す。
		一致したら、その終わりから次を調べる。
	 */
	std::vector<Span> Highlight(std::wstring_view line)
	{
		const std::wstring text = std::wstring(line) + L"\r\n";
		const CStringRef cStr(text.c_str(), text.length());
		std::vector<Span> spans;
		pcRegexKeyword->RegexKeyLineStart();
		for (int nPos = 0; nPos < int(text.length());) {
			int nMatchLen = 0;
			int nMatchColor = 0;
			if (pcRegexKeyword->RegexIsKeyword(cStr, nPos, &nMatchLen, &nMatchColor)) {
				spans.emplace_back(nMatchColor, text.substr(nPos, nMatchLen));
				nPos += nMatchLen;
			} else {
				++nPos;
			}
		}
		return spans;
	}

	STypeConfig type;
	std::unique_ptr<CRegexKeyword> pcRegexKeyword;
};

} // namespace

TEST_F(CTypeMarkdownTest, TypeConfig)
{
	EXPECT_THAT(type.m_szTypeName, StrEq(L"Markdown"));
	EXPECT_THAT(type.m_szTypeExts, StrEq(L"md,markdown,mdown,mkd"));
	EXPECT_THAT(type.m_eDefaultOutline, Eq(OUTLINE_MARKDOWN));
	EXPECT_THAT(type.m_nTextWrapMethod, Eq(WRAP_WINDOW_WIDTH));
	// コードブロックはブロックコメントでなく、CColor_MarkdownFenceで色を付ける
	EXPECT_THAT(type.m_bMarkdownCodeBlock, IsTrue());
	EXPECT_THAT(type.m_cBlockComments[0].getBlockCommentFrom(), StrEq(L""));
	EXPECT_THAT(type.m_cBlockComments[1].getBlockCommentFrom(), StrEq(L""));
	for (int i = COLORIDX_KEYWORD1; i <= COLORIDX_KEYWORD10; ++i) {
		EXPECT_THAT(type.m_ColorInfoArr[i].m_bDisp, IsTrue()) << i;
	}
	for (int i = 0; i < MAX_KEYWORDSET_PER_TYPE; ++i) {
		EXPECT_THAT(type.m_nKeyWordSetIdx[i], Eq(-1)) << i;	// 強調キーワードはコードブロックの色にだけ使う
	}
	EXPECT_THAT(type.m_ColorInfoArr[COLORIDX_SSTRING].m_bDisp, IsFalse());
	EXPECT_THAT(type.m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp, IsFalse());
	EXPECT_THAT(type.m_ColorInfoArr[COLORIDX_COMMENT].m_bDisp, IsTrue());
	EXPECT_THAT(type.m_bUseRegexKeyword, IsTrue());
	for (int i = COLORIDX_REGEX1; i <= COLORIDX_REGEX10; ++i) {
		EXPECT_THAT(type.m_ColorInfoArr[i].m_bDisp, IsTrue()) << i;
	}
}

TEST_F(CTypeMarkdownTest, Headings)
{
	EXPECT_THAT(Highlight(L"# 見出し **太字**"), ElementsAre(Pair(COLORIDX_REGEX1, L"# 見出し **太字**")));
	EXPECT_THAT(Highlight(L"   ###### 6段目"), ElementsAre(Pair(COLORIDX_REGEX1, L"   ###### 6段目")));
	EXPECT_THAT(Highlight(L"#"), ElementsAre(Pair(COLORIDX_REGEX1, L"#")));
	EXPECT_THAT(Highlight(L"==="), ElementsAre(Pair(COLORIDX_REGEX1, L"===")));
	EXPECT_THAT(Highlight(L"#ハッシュタグ"), IsEmpty());
	EXPECT_THAT(Highlight(L"####### 7個"), IsEmpty());
	EXPECT_THAT(Highlight(L"    # 字下げ4桁"), IsEmpty());
}

TEST_F(CTypeMarkdownTest, BlockMarkers)
{
	EXPECT_THAT(Highlight(L"- 項目 **太字**"), ElementsAre(Pair(COLORIDX_REGEX8, L"-"), Pair(COLORIDX_REGEX2, L"**太字**")));
	EXPECT_THAT(Highlight(L"  1. 番号"), ElementsAre(Pair(COLORIDX_REGEX8, L"  1.")));
	EXPECT_THAT(Highlight(L"- [x] 完了"), ElementsAre(Pair(COLORIDX_REGEX8, L"- [x]")));
	EXPECT_THAT(Highlight(L"> 引用 *斜体*"), ElementsAre(Pair(COLORIDX_REGEX7, L"> 引用 *斜体*")));
	EXPECT_THAT(Highlight(L"---"), ElementsAre(Pair(COLORIDX_REGEX9, L"---")));
	EXPECT_THAT(Highlight(L"* * *"), ElementsAre(Pair(COLORIDX_REGEX9, L"* * *")));
	EXPECT_THAT(Highlight(L"[ref]: https://example.com/ \"題\""), ElementsAre(Pair(COLORIDX_REGEX6, L"[ref]: https://example.com/ \"題\"")));
}

TEST_F(CTypeMarkdownTest, Tables)
{
	EXPECT_THAT(Highlight(L"| a | b |"), ElementsAre(Pair(COLORIDX_REGEX9, L"|"), Pair(COLORIDX_REGEX9, L"|"), Pair(COLORIDX_REGEX9, L"|")));
	EXPECT_THAT(Highlight(L"|---|:---:|"), ElementsAre(Pair(COLORIDX_REGEX9, L"|---|:---:|")));
	EXPECT_THAT(Highlight(L"--- | ---"), ElementsAre(Pair(COLORIDX_REGEX9, L"--- | ---")));
}

TEST_F(CTypeMarkdownTest, InlineCode)
{
	EXPECT_THAT(Highlight(L"`a*b*c` と *斜体*"), ElementsAre(Pair(COLORIDX_REGEX5, L"`a*b*c`"), Pair(COLORIDX_REGEX3, L"*斜体*")));
	EXPECT_THAT(Highlight(L"``a`b``"), ElementsAre(Pair(COLORIDX_REGEX5, L"``a`b``")));
	EXPECT_THAT(Highlight(L"```python"), IsEmpty());
}

TEST_F(CTypeMarkdownTest, Emphasis)
{
	EXPECT_THAT(Highlight(L"**太字** と __太字__ と *斜体* と _斜体_"), ElementsAre(
		Pair(COLORIDX_REGEX2, L"**太字**"),
		Pair(COLORIDX_REGEX2, L"__太字__"),
		Pair(COLORIDX_REGEX3, L"*斜体*"),
		Pair(COLORIDX_REGEX3, L"_斜体_")
	));
	EXPECT_THAT(Highlight(L"***強調***"), ElementsAre(Pair(COLORIDX_REGEX2, L"***強調***")));
	EXPECT_THAT(Highlight(L"~~消す~~"), ElementsAre(Pair(COLORIDX_REGEX4, L"~~消す~~")));
	EXPECT_THAT(Highlight(L"日本語*強調*です"), ElementsAre(Pair(COLORIDX_REGEX3, L"*強調*")));
}

TEST_F(CTypeMarkdownTest, NotEmphasis)
{
	EXPECT_THAT(Highlight(L"snake_case_name と 2 * 3 * 4"), IsEmpty());
	EXPECT_THAT(Highlight(L"\\*斜体じゃない\\*"), ElementsAre(Pair(COLORIDX_TEXT, L"\\*"), Pair(COLORIDX_TEXT, L"\\*")));
}

TEST_F(CTypeMarkdownTest, Links)
{
	EXPECT_THAT(Highlight(L"[サクラ](https://sakura-editor.github.io/) と ![画像](a.png)"), ElementsAre(
		Pair(COLORIDX_REGEX6, L"[サクラ](https://sakura-editor.github.io/)"),
		Pair(COLORIDX_REGEX6, L"![画像](a.png)")
	));
	EXPECT_THAT(Highlight(L"[![CI](b.svg)](https://example.com/)"), ElementsAre(Pair(COLORIDX_REGEX6, L"[![CI](b.svg)](https://example.com/)")));
	EXPECT_THAT(Highlight(L"[参照][ref]"), ElementsAre(Pair(COLORIDX_REGEX6, L"[参照][ref]")));
}

TEST_F(CTypeMarkdownTest, Html)
{
	EXPECT_THAT(Highlight(L"<!-- メモ --> <br> <span class=\"x\">赤</span>"), ElementsAre(
		Pair(COLORIDX_REGEX10, L"<!-- メモ -->"),
		Pair(COLORIDX_REGEX10, L"<br>"),
		Pair(COLORIDX_REGEX10, L"<span class=\"x\">"),
		Pair(COLORIDX_REGEX10, L"</span>")
	));
	EXPECT_THAT(Highlight(L"<https://example.com/>"), IsEmpty());
}
