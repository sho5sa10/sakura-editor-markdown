/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/MarkdownImagePath.h"

#include <string>
#include <vector>

using namespace markdown;

namespace {

const wchar_t* const kDoc = L"C:\\docs\\notes\\a.md";

std::optional<ImageLocation> Resolve(std::wstring_view target, std::wstring_view doc = kDoc)
{
	return ResolveLocalImagePath(doc, target);
}

} // namespace

TEST(MarkdownImagePath, ResolvesRelativePathsUnderTheDocumentFolder)
{
	const auto plain = Resolve(L"images/a.png");
	ASSERT_TRUE(plain.has_value());
	EXPECT_EQ(plain->path, L"C:\\docs\\notes\\images\\a.png");
	EXPECT_EQ(plain->root, L"C:\\docs\\notes");

	EXPECT_EQ(Resolve(L"./a.png")->path, L"C:\\docs\\notes\\a.png");
	EXPECT_EQ(Resolve(L"sub/../a.png")->path, L"C:\\docs\\notes\\a.png");
	EXPECT_EQ(Resolve(L"sub\\deep\\a.png")->path, L"C:\\docs\\notes\\sub\\deep\\a.png");
	EXPECT_EQ(Resolve(L"a.png?size=large#top")->path, L"C:\\docs\\notes\\a.png");
	EXPECT_EQ(Resolve(L"  a.png  ")->path, L"C:\\docs\\notes\\a.png");
}

TEST(MarkdownImagePath, DecodesPercentEncodedUtf8AndKeepsJapaneseNames)
{
	EXPECT_EQ(Resolve(L"my%20image.png")->path, L"C:\\docs\\notes\\my image.png");
	EXPECT_EQ(Resolve(L"%E6%97%A5%E6%9C%AC%E8%AA%9E.png")->path, L"C:\\docs\\notes\\日本語.png");
	EXPECT_EQ(Resolve(L"画像/日本語.png")->path, L"C:\\docs\\notes\\画像\\日本語.png");
	EXPECT_EQ(Resolve(L"a%25b.png")->path, L"C:\\docs\\notes\\a%b.png");	// 二重には展開しない
}

TEST(MarkdownImagePath, SupportsUncAndDriveRootDocuments)
{
	const auto unc = Resolve(L"x.png", L"\\\\server\\share\\dir\\a.md");
	ASSERT_TRUE(unc.has_value());
	EXPECT_EQ(unc->path, L"\\\\server\\share\\dir\\x.png");
	EXPECT_EQ(unc->root, L"\\\\server\\share\\dir");

	const auto top = Resolve(L"x.png", L"C:\\a.md");
	ASSERT_TRUE(top.has_value());
	EXPECT_EQ(top->path, L"C:\\x.png");
	EXPECT_EQ(top->root, L"C:\\");

	EXPECT_EQ(Resolve(L"x.png", L"C:/docs/a.md")->path, L"C:\\docs\\x.png");
}

TEST(MarkdownImagePath, RejectsPathsThatLeaveTheDocumentFolder)
{
	for (const wchar_t* target : {
		L"../a.png", L"..\\a.png", L"a/../../b.png", L"sub/../../b.png", L"..",
		L"%2e%2e/a.png", L"%2E%2E%2Fa.png", L"%2e%2e%5ca.png", L"a/%2e%2e/%2e%2e/b.png",
	}) {
		EXPECT_FALSE(Resolve(target).has_value()) << target;
	}
}

TEST(MarkdownImagePath, RejectsAbsolutePathsUrlsAndStreams)
{
	for (const wchar_t* target : {
		L"/etc/passwd.png", L"\\windows\\x.png", L"\\\\host\\share\\x.png", L"//host/share/x.png", L"///x.png",
		L"C:\\x.png", L"C:/x.png", L"c:x.png",
		L"http://example.com/a.png", L"https://example.com/a.png", L"file:///C:/x.png", L"data:image/png;base64,AAAA",
		L"javascript:alert(1)", L"a.png:stream", L"a.png::$DATA", L"%3Aa.png", L"%43%3A%5Cx.png",
	}) {
		EXPECT_FALSE(Resolve(target).has_value()) << target;
	}
}

TEST(MarkdownImagePath, RejectsEmptyAndMalformedTargets)
{
	for (const wchar_t* target : {
		L"", L"   ", L"?x", L"#frag", L".", L"./", L"%", L"%2", L"%ZZ", L"a%2", L"%00a.png", L"%0Aa.png",
		L"%C0%AF.png", L"%FF%FE.png", L"%E6%97.png",
		L"a<b.png", L"a>b.png", L"a|b.png", L"a\"b.png", L"a*.png", L"a\x01.png",
	}) {
		EXPECT_FALSE(Resolve(target).has_value()) << target;
	}
}

TEST(MarkdownImagePath, RejectsNamesWindowsWouldAlias)
{
	for (const wchar_t* target : {
		L"CON", L"con.png", L"dir/NUL.txt", L"PRN.x", L"aux", L"COM1", L"com9.png", L"LPT1", L"lpt9.png", L"CONIN$", L"CON .png",
		L"a.png.", L"dir./a.png", L"dir /a.png",
	}) {
		EXPECT_FALSE(Resolve(target).has_value()) << target;
	}
	// 似ているが、デバイス名ではないものは使える
	for (const wchar_t* target : { L"COM10.png", L"CONSOLE.png", L"com.png", L"COM0.png", L"nulled.png", L"LPT.png" }) {
		EXPECT_TRUE(Resolve(target).has_value()) << target;
	}
}

TEST(MarkdownImagePath, RejectsDocumentsThatHaveNoUsableFolder)
{
	for (const wchar_t* doc : { L"", L"a.md", L"C:a.md", L"\\a.md", L"\\\\?\\C:\\a.md", L"\\\\.\\C:\\a.md", L"docs\\a.md", L"C:" }) {
		EXPECT_FALSE(Resolve(L"x.png", doc).has_value()) << doc;
	}
}

TEST(MarkdownImagePath, RejectsOversizedNames)
{
	EXPECT_FALSE(Resolve(std::wstring(300, L'a') + L".png").has_value());
	EXPECT_TRUE(Resolve(std::wstring(250, L'a') + L".png").has_value());
	EXPECT_FALSE(Resolve(std::wstring(40000, L'a')).has_value());
	std::wstring deep;
	for (int i = 0; i < 3000; ++i) deep += L"abcdefghij/";
	deep += L"x.png";
	EXPECT_FALSE(Resolve(deep).has_value());
}

TEST(MarkdownImagePath, RejectsDriveRelativePaths)
{
	// "C:a.png" は、Cドライブのカレントフォルダからの相対パス。文書のフォルダとは無関係なので拒否する
	EXPECT_FALSE(Resolve(L"C:a.png").has_value());
	EXPECT_FALSE(Resolve(L"D:images\\a.png").has_value());
	EXPECT_FALSE(Resolve(L"C:\\a.png").has_value());
	EXPECT_FALSE(Resolve(L"\\\\server\\share\\a.png").has_value());
	EXPECT_FALSE(Resolve(L"//server/share/a.png").has_value());
}
