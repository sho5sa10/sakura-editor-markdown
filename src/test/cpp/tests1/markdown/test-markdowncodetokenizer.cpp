/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/MarkdownCodeTokenizer.h"

#include <string>
#include <vector>

using namespace markdown;
using ::testing::ElementsAre;

namespace {

const wchar_t* KindName(ECodeTokenKind eKind)
{
	switch (eKind) {
	case ECodeTokenKind::Keyword:	return L"K";
	case ECodeTokenKind::String:	return L"S";
	case ECodeTokenKind::Comment:	return L"C";
	case ECodeTokenKind::Number:	return L"N";
	case ECodeTokenKind::Variable:	return L"V";
	case ECodeTokenKind::Function:	return L"F";
	case ECodeTokenKind::Tag:		return L"T";
	case ECodeTokenKind::Attribute:	return L"A";
	case ECodeTokenKind::Inserted:	return L"+";
	case ECodeTokenKind::Deleted:	return L"-";
	}
	return L"?";
}

//! 字句を「種類:文字列」の形で並べる(K=キーワード S=文字列 C=コメント N=数値 V=変数 F=関数 T=タグ A=属性)
std::vector<std::wstring> Tokens(ECodeLanguage eLanguage, std::wstring_view line)
{
	std::vector<std::wstring> result;
	size_t nLastEnd = 0;
	for (const auto& token : TokenizeCodeLine(eLanguage, line)) {
		EXPECT_LE(nLastEnd, token.nBegin);
		EXPECT_LT(token.nBegin, token.nEnd);
		nLastEnd = token.nEnd;
		result.push_back(std::wstring(KindName(token.eKind)) + L":" + std::wstring(line.substr(token.nBegin, token.nEnd - token.nBegin)));
	}
	return result;
}

} // namespace

TEST(MarkdownCodeTokenizer, GetCodeLanguage)
{
	EXPECT_EQ(GetCodeLanguage(L""), ECodeLanguage::Generic);
	EXPECT_EQ(GetCodeLanguage(L"  "), ECodeLanguage::Generic);
	EXPECT_EQ(GetCodeLanguage(L"bash"), ECodeLanguage::Shell);
	EXPECT_EQ(GetCodeLanguage(L"Shell"), ECodeLanguage::Shell);
	EXPECT_EQ(GetCodeLanguage(L" python title=\"a.py\""), ECodeLanguage::Python);
	EXPECT_EQ(GetCodeLanguage(L"{.json}"), ECodeLanguage::Json);
	EXPECT_EQ(GetCodeLanguage(L"js{1,3}"), ECodeLanguage::JavaScript);
	EXPECT_EQ(GetCodeLanguage(L"TypeScript"), ECodeLanguage::JavaScript);
	EXPECT_EQ(GetCodeLanguage(L"ps1"), ECodeLanguage::PowerShell);
	EXPECT_EQ(GetCodeLanguage(L"yml"), ECodeLanguage::Yaml);
	EXPECT_EQ(GetCodeLanguage(L"SQL"), ECodeLanguage::Sql);
	EXPECT_EQ(GetCodeLanguage(L"c++"), ECodeLanguage::CLike);
	EXPECT_EQ(GetCodeLanguage(L"csharp"), ECodeLanguage::CLike);
	EXPECT_EQ(GetCodeLanguage(L"xml"), ECodeLanguage::Html);
	EXPECT_EQ(GetCodeLanguage(L"toml"), ECodeLanguage::Ini);
	EXPECT_EQ(GetCodeLanguage(L"Dockerfile"), ECodeLanguage::Dockerfile);
	EXPECT_EQ(GetCodeLanguage(L"diff"), ECodeLanguage::Diff);
	EXPECT_EQ(GetCodeLanguage(L"text"), ECodeLanguage::None);
	EXPECT_EQ(GetCodeLanguage(L"mermaid"), ECodeLanguage::Generic);
}

TEST(MarkdownCodeTokenizer, NoneHasNoTokens)
{
	EXPECT_THAT(Tokens(ECodeLanguage::None, L"echo \"a\" # b 1"), IsEmpty());
}

TEST(MarkdownCodeTokenizer, Generic)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Generic, L"run \"a b\" 'c' 42 # note"),
		ElementsAre(L"S:\"a b\"", L"S:'c'", L"N:42", L"C:# note"));
	EXPECT_THAT(Tokens(ECodeLanguage::Generic, L"url http://example.com/a#b // note"),
		ElementsAre(L"C:// note"));
	EXPECT_THAT(Tokens(ECodeLanguage::Generic, L"手順3 x86_64 v2"), ElementsAre(L"N:3"));
	// 言語の指定がないときは、行の中で閉じていない引用符を文字列にしない(英文の ' など)
	EXPECT_THAT(Tokens(ECodeLanguage::Generic, L"don't stop\r\n"), IsEmpty());
}

TEST(MarkdownCodeTokenizer, ShellCommandsAndKeywords)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"echo \"##### [01] $HOME\" | grep -i brotli"),
		ElementsAre(L"F:echo", L"S:\"##### [01] $HOME\"", L"F:grep"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"if [ -d \"$LOGDIR\" ]; then cd /tmp; fi"),
		ElementsAre(L"K:if", L"S:\"$LOGDIR\"", L"K:then", L"F:cd", L"K:fi"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"for f in *.log; do rm \"$f\"; done"),
		ElementsAre(L"K:for", L"K:in", L"K:do", L"F:rm", L"S:\"$f\"", L"K:done"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"  /usr/bin/env python3 ./run.sh"),
		ElementsAre(L"F:/usr/bin/env"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"nice -n 19 ionice -c3 grep -rI 'x' /opt 2>/dev/null"),
		ElementsAre(L"F:nice", L"N:19", L"S:'x'", L"N:2"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"} 2>&1 | tee \"$LOG\""),
		ElementsAre(L"N:2", L"N:1", L"F:tee", L"S:\"$LOG\""));
}

TEST(MarkdownCodeTokenizer, ShellVariablesAndComments)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"LOG=$LOGDIR/check_$(hostname -s).log"),
		ElementsAre(L"V:LOG", L"V:$LOGDIR", L"F:hostname"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"export PATH=${HOME}/bin:$PATH"),
		ElementsAre(L"K:export", L"V:PATH", L"V:${HOME}", L"V:$PATH"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"echo $1 $? $# # comment"),
		ElementsAre(L"F:echo", L"V:$1", L"V:$?", L"V:$#", L"C:# comment"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"# rm <file>"), ElementsAre(L"C:# rm <file>"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"echo 'it''s \\' x"), ElementsAre(L"F:echo", L"S:'it'", L"S:'s \\'"));
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"echo \"a \\\" b\""), ElementsAre(L"F:echo", L"S:\"a \\\" b\""));
}

TEST(MarkdownCodeTokenizer, PowerShell)
{
	EXPECT_THAT(Tokens(ECodeLanguage::PowerShell, L"Get-ChildItem -Path $env:TEMP | Where-Object { $_.Length -gt 100 }"),
		ElementsAre(L"F:Get-ChildItem", L"V:$env:TEMP", L"F:Where-Object", L"V:$_", L"K:-gt", L"N:100"));
	EXPECT_THAT(Tokens(ECodeLanguage::PowerShell, L"if ($x -eq $true) { 'it''s' } # c"),
		ElementsAre(L"K:if", L"V:$x", L"K:-eq", L"N:$true", L"S:'it''s'", L"C:# c"));
	EXPECT_THAT(Tokens(ECodeLanguage::PowerShell, L"<# block #> ls"), ElementsAre(L"C:<# block #>", L"F:ls"));
}

TEST(MarkdownCodeTokenizer, Python)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Python, L"def main(args=None):  # entry"),
		ElementsAre(L"K:def", L"F:main", L"N:None", L"C:# entry"));
	EXPECT_THAT(Tokens(ECodeLanguage::Python, L"@dataclass"), ElementsAre(L"F:@dataclass"));
	EXPECT_THAT(Tokens(ECodeLanguage::Python, L"print(f\"{x}\", r'\\d', 0x1F, 1.5e-3)"),
		ElementsAre(L"F:print", L"S:f\"{x}\"", L"S:r'\\d'", L"N:0x1F", L"N:1.5e-3"));
	EXPECT_THAT(Tokens(ECodeLanguage::Python, L"class Foo(Base): pass"),
		ElementsAre(L"K:class", L"F:Foo", L"K:pass"));
}

TEST(MarkdownCodeTokenizer, JavaScript)
{
	EXPECT_THAT(Tokens(ECodeLanguage::JavaScript, L"const x = await fetch(`/api/${id}`); // get"),
		ElementsAre(L"K:const", L"K:await", L"F:fetch", L"S:`/api/${id}`", L"C:// get"));
	EXPECT_THAT(Tokens(ECodeLanguage::JavaScript, L"function add(a, b) { return a + b /* sum */; }"),
		ElementsAre(L"K:function", L"F:add", L"K:return", L"C:/* sum */"));
	EXPECT_THAT(Tokens(ECodeLanguage::JavaScript, L"if (v === null || v === undefined) {}"),
		ElementsAre(L"K:if", L"N:null", L"N:undefined"));
}

TEST(MarkdownCodeTokenizer, Json)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Json, L"  \"name\": \"sakura\", \"size\": -12.5, \"ok\" : true,"),
		ElementsAre(L"V:\"name\"", L"S:\"sakura\"", L"V:\"size\"", L"N:12.5", L"V:\"ok\"", L"N:true"));
}

TEST(MarkdownCodeTokenizer, Yaml)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Yaml, L"name: build # job"), ElementsAre(L"V:name", L"C:# job"));
	EXPECT_THAT(Tokens(ECodeLanguage::Yaml, L"  - uses: actions/checkout@v4"), ElementsAre(L"V:uses"));
	EXPECT_THAT(Tokens(ECodeLanguage::Yaml, L"    enabled: true"), ElementsAre(L"V:enabled", L"N:true"));
	EXPECT_THAT(Tokens(ECodeLanguage::Yaml, L"\"quoted key\": 'v''s'"), ElementsAre(L"V:\"quoted key\"", L"S:'v''s'"));
	EXPECT_THAT(Tokens(ECodeLanguage::Yaml, L"url: http://a#b"), ElementsAre(L"V:url"));
}

TEST(MarkdownCodeTokenizer, Sql)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Sql, L"SELECT name, COUNT(*) FROM users WHERE id = 'it''s' -- c"),
		ElementsAre(L"K:SELECT", L"F:COUNT", L"K:FROM", L"K:WHERE", L"S:'it''s'", L"C:-- c"));
	EXPECT_THAT(Tokens(ECodeLanguage::Sql, L"update t set v = null /* x */ where 1 = 1"),
		ElementsAre(L"K:update", L"K:set", L"N:null", L"C:/* x */", L"K:where", L"N:1", L"N:1"));
}

TEST(MarkdownCodeTokenizer, CLike)
{
	EXPECT_THAT(Tokens(ECodeLanguage::CLike, L"#include <stdio.h>"), ElementsAre(L"K:#include"));
	EXPECT_THAT(Tokens(ECodeLanguage::CLike, L"  # define MAX 10"), ElementsAre(L"K:# define", L"N:10"));
	EXPECT_THAT(Tokens(ECodeLanguage::CLike, L"int main(void) { return printf(\"%d\\n\", 'a'); } // c"),
		ElementsAre(L"K:int", L"F:main", L"K:void", L"K:return", L"F:printf", L"S:\"%d\\n\"", L"S:'a'", L"C:// c"));
	EXPECT_THAT(Tokens(ECodeLanguage::CLike, L"if (p == nullptr) throw new Error();"),
		ElementsAre(L"K:if", L"N:nullptr", L"K:throw", L"K:new", L"F:Error"));
}

TEST(MarkdownCodeTokenizer, Html)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Html, L"<a href=\"x.html\" class=note>link</a> &amp; <!-- c -->"),
		ElementsAre(L"T:<a", L"A:href", L"S:\"x.html\"", L"A:class", L"S:note", L"T:>", L"T:</a", L"T:>",
			L"N:&amp;", L"C:<!-- c -->"));
	EXPECT_THAT(Tokens(ECodeLanguage::Html, L"<?xml version='1.0'?><br/>"),
		ElementsAre(L"T:<?xml", L"A:version", L"S:'1.0'", L"T:?>", L"T:<br", L"T:/>"));
	EXPECT_THAT(Tokens(ECodeLanguage::Html, L"a < b"), IsEmpty());
}

TEST(MarkdownCodeTokenizer, Ini)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Ini, L"[core]"), ElementsAre(L"K:[core]"));
	EXPECT_THAT(Tokens(ECodeLanguage::Ini, L"  editor = \"vim\" # c"), ElementsAre(L"V:editor", L"S:\"vim\"", L"C:# c"));
	EXPECT_THAT(Tokens(ECodeLanguage::Ini, L"enabled=true"), ElementsAre(L"V:enabled", L"N:true"));
	EXPECT_THAT(Tokens(ECodeLanguage::Ini, L"; comment = x"), ElementsAre(L"C:; comment = x"));
}

TEST(MarkdownCodeTokenizer, Dockerfile)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Dockerfile, L"FROM ubuntu:24.04 AS build"), ElementsAre(L"K:FROM", L"N:24.04"));
	EXPECT_THAT(Tokens(ECodeLanguage::Dockerfile, L"run apt-get install -y \"$PKG\" # c"),
		ElementsAre(L"K:run", L"S:\"$PKG\"", L"C:# c"));
	EXPECT_THAT(Tokens(ECodeLanguage::Dockerfile, L"ENV HOME=/root PATH=$PATH"), ElementsAre(L"K:ENV", L"V:$PATH"));
}

TEST(MarkdownCodeTokenizer, Diff)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Diff, L"--- a/x.c"), ElementsAre(L"K:--- a/x.c"));
	EXPECT_THAT(Tokens(ECodeLanguage::Diff, L"@@ -1,2 +1,3 @@"), ElementsAre(L"F:@@ -1,2 +1,3 @@"));
	EXPECT_THAT(Tokens(ECodeLanguage::Diff, L"+added\r\n"), ElementsAre(L"+:+added"));
	EXPECT_THAT(Tokens(ECodeLanguage::Diff, L"-removed"), ElementsAre(L"-:-removed"));
	EXPECT_THAT(Tokens(ECodeLanguage::Diff, L" context"), IsEmpty());
}

TEST(MarkdownCodeTokenizer, UnterminatedStringRunsToEndOfLine)
{
	EXPECT_THAT(Tokens(ECodeLanguage::Shell, L"echo \"open\r\n"), ElementsAre(L"F:echo", L"S:\"open"));
	EXPECT_THAT(Tokens(ECodeLanguage::CLike, L"x = /* open"), ElementsAre(L"C:/* open"));
}
