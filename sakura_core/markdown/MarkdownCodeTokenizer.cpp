/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/MarkdownCodeTokenizer.h"

#include <algorithm>
#include <span>

namespace markdown {

namespace {

using Words = std::span<const std::wstring_view>;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                          語の一覧                           //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

constexpr std::wstring_view kShellKeywords[] = {
	L"if", L"then", L"else", L"elif", L"fi", L"for", L"while", L"until", L"do", L"done", L"case", L"esac",
	L"function", L"in", L"select", L"time", L"return", L"exit", L"export", L"local", L"readonly", L"declare",
	L"typeset", L"unset", L"source", L"alias", L"break", L"continue", L"shift", L"trap", L"eval", L"exec",
};
//! この後ろはコマンドの最初の語になるキーワード
constexpr std::wstring_view kShellCommandStarters[] = {
	L"if", L"then", L"else", L"elif", L"while", L"until", L"do", L"time", L"exec", L"eval",
};
//! この後ろの NAME=値 の NAME を変数にするキーワード
constexpr std::wstring_view kShellAssignKeywords[] = {
	L"export", L"local", L"readonly", L"declare", L"typeset",
};

constexpr std::wstring_view kPowerShellKeywords[] = {
	L"begin", L"break", L"catch", L"class", L"continue", L"data", L"default", L"do", L"dynamicparam", L"else",
	L"elseif", L"end", L"enum", L"exit", L"filter", L"finally", L"for", L"foreach", L"function", L"if", L"in",
	L"param", L"process", L"return", L"switch", L"throw", L"trap", L"try", L"until", L"using", L"while",
};
constexpr std::wstring_view kPowerShellOperators[] = {
	L"eq", L"ne", L"gt", L"ge", L"lt", L"le", L"like", L"notlike", L"match", L"notmatch", L"contains",
	L"notcontains", L"in", L"notin", L"replace", L"split", L"join", L"and", L"or", L"not", L"xor", L"is",
	L"isnot", L"as", L"f", L"band", L"bor",
};
constexpr std::wstring_view kPowerShellConstants[] = { L"true", L"false", L"null" };

constexpr std::wstring_view kPythonKeywords[] = {
	L"and", L"as", L"assert", L"async", L"await", L"break", L"class", L"continue", L"def", L"del", L"elif",
	L"else", L"except", L"finally", L"for", L"from", L"global", L"if", L"import", L"in", L"is", L"lambda",
	L"match", L"case", L"nonlocal", L"not", L"or", L"pass", L"raise", L"return", L"try", L"while", L"with",
	L"yield",
};
constexpr std::wstring_view kPythonConstants[] = { L"True", L"False", L"None" };

constexpr std::wstring_view kJavaScriptKeywords[] = {
	L"abstract", L"as", L"async", L"await", L"break", L"case", L"catch", L"class", L"const", L"continue",
	L"debugger", L"declare", L"default", L"delete", L"do", L"else", L"enum", L"export", L"extends", L"finally",
	L"for", L"from", L"function", L"get", L"if", L"implements", L"import", L"in", L"instanceof", L"interface",
	L"let", L"new", L"of", L"private", L"protected", L"public", L"readonly", L"return", L"set", L"static",
	L"super", L"switch", L"this", L"throw", L"try", L"type", L"typeof", L"var", L"void", L"while", L"with",
	L"yield",
};
constexpr std::wstring_view kJavaScriptConstants[] = { L"true", L"false", L"null", L"undefined", L"NaN", L"Infinity" };

constexpr std::wstring_view kCLikeKeywords[] = {
	L"abstract", L"async", L"auto", L"await", L"bool", L"boolean", L"break", L"byte", L"case", L"catch", L"char",
	L"class", L"const", L"constexpr", L"continue", L"default", L"defer", L"delete", L"do", L"double", L"else",
	L"enum", L"explicit", L"extends", L"extern", L"final", L"float", L"fn", L"for", L"friend", L"func", L"go",
	L"goto", L"if", L"impl", L"implements", L"import", L"inline", L"instanceof", L"int", L"interface", L"let",
	L"long", L"match", L"mod", L"mut", L"namespace", L"new", L"noexcept", L"operator", L"override", L"package",
	L"private", L"protected", L"pub", L"public", L"register", L"return", L"short", L"signed", L"sizeof",
	L"static", L"struct", L"super", L"switch", L"synchronized", L"template", L"this", L"throw", L"throws",
	L"try", L"typedef", L"typename", L"union", L"unsigned", L"use", L"using", L"var", L"virtual", L"void",
	L"volatile", L"while", L"yield",
};
constexpr std::wstring_view kCLikeConstants[] = { L"true", L"false", L"null", L"nullptr", L"NULL", L"nil" };

constexpr std::wstring_view kSqlKeywords[] = {
	L"add", L"all", L"alter", L"and", L"any", L"as", L"asc", L"begin", L"between", L"by", L"case", L"check",
	L"column", L"commit", L"constraint", L"create", L"cross", L"database", L"default", L"delete", L"desc",
	L"distinct", L"drop", L"else", L"end", L"exists", L"foreign", L"from", L"full", L"grant", L"group",
	L"having", L"if", L"in", L"index", L"inner", L"insert", L"into", L"is", L"join", L"key", L"left", L"like",
	L"limit", L"not", L"offset", L"on", L"or", L"order", L"outer", L"primary", L"procedure", L"references",
	L"replace", L"returning", L"revoke", L"right", L"rollback", L"select", L"set", L"table", L"then", L"top",
	L"truncate", L"union", L"unique", L"update", L"values", L"view", L"when", L"where", L"with",
};
constexpr std::wstring_view kSqlConstants[] = { L"null", L"true", L"false" };

constexpr std::wstring_view kJsonConstants[] = { L"true", L"false", L"null" };
constexpr std::wstring_view kYamlConstants[] = { L"true", L"false", L"yes", L"no", L"on", L"off", L"null" };
constexpr std::wstring_view kIniConstants[] = { L"true", L"false", L"yes", L"no", L"on", L"off" };

constexpr std::wstring_view kDockerInstructions[] = {
	L"from", L"run", L"cmd", L"label", L"maintainer", L"expose", L"env", L"add", L"copy", L"entrypoint",
	L"volume", L"user", L"workdir", L"arg", L"onbuild", L"stopsignal", L"healthcheck", L"shell",
};

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                          言語の規則                         //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

//! 言語ごとの字句の規則
struct SLanguageRule {
	std::wstring_view	lineComment1;				//!< 行コメントの始まり
	std::wstring_view	lineComment2;				//!< 行コメントの始まり2
	std::wstring_view	blockCommentBegin;			//!< ブロックコメントの始まり(1行の中で閉じる分だけを扱う)
	std::wstring_view	blockCommentEnd;			//!< ブロックコメントの終わり
	bool				bCommentAtWordStart = false;	//!< 行コメントは、行頭か空白の直後だけ(シェルの#など)
	std::wstring_view	quotes;						//!< 文字列の引用符
	wchar_t				chEscape = L'\\';			//!< 文字列の中のエスケープ文字。L'\0'ならなし
	bool				bEscapeInSingleQuote = true;	//!< 'の文字列の中でもエスケープするか
	bool				bDoubledQuote = false;		//!< 引用符を2つ重ねると、引用符そのものになる(SQLなど)
	bool				bCaseInsensitive = false;	//!< 語の大文字と小文字を区別しない
	Words				keywords;					//!< キーワード
	Words				constants;					//!< 定数
	Words				dashOperators;				//!< -eq のような演算子(PowerShell)
	bool				bDollarVariables = false;	//!< $NAME を変数にする
	bool				bCommandPosition = false;	//!< コマンドの最初の語をコマンド名にする(シェル)
	bool				bFunctionCall = false;		//!< 直後が(の語を関数名にする
	bool				bDefinitionNames = false;	//!< def、class、function の次の語を関数名にする
	bool				bDecorators = false;		//!< @name を関数名にする
	bool				bPreprocessor = false;		//!< 行頭の#includeなどをキーワードにする
	bool				bCmdlets = false;			//!< Verb-Nounの形の語をコマンド名にする(PowerShell)
	bool				bJsonKeys = false;			//!< 直後が:の文字列をキーにする
	bool				bYamlKeys = false;			//!< 行頭のkey:をキーにする
	bool				bIniKeys = false;			//!< 行頭の[section]とkey=を扱う
	bool				bInstructions = false;		//!< キーワードは行の最初の語だけ(Dockerfile)
	bool				bStringPrefixes = false;	//!< r"..."、f"..." などの接頭辞(Python)
	bool				bClosedStringsOnly = false;	//!< 行の中で閉じた文字列だけを文字列にする(英文の ' を文字列にしない)
};

constexpr SLanguageRule kGenericRule{
	.lineComment1 = L"#", .lineComment2 = L"//", .bCommentAtWordStart = true, .quotes = L"\"'",
	.bClosedStringsOnly = true,
};
constexpr SLanguageRule kShellRule{
	.lineComment1 = L"#", .bCommentAtWordStart = true, .quotes = L"\"'`", .bEscapeInSingleQuote = false,
	.keywords = kShellKeywords, .bDollarVariables = true, .bCommandPosition = true,
};
constexpr SLanguageRule kPowerShellRule{
	.lineComment1 = L"#", .blockCommentBegin = L"<#", .blockCommentEnd = L"#>", .bCommentAtWordStart = true,
	.quotes = L"\"'", .chEscape = L'`', .bEscapeInSingleQuote = false, .bDoubledQuote = true,
	.bCaseInsensitive = true, .keywords = kPowerShellKeywords, .constants = kPowerShellConstants,
	.dashOperators = kPowerShellOperators, .bDollarVariables = true, .bCommandPosition = true, .bCmdlets = true,
};
constexpr SLanguageRule kPythonRule{
	.lineComment1 = L"#", .quotes = L"\"'", .keywords = kPythonKeywords, .constants = kPythonConstants,
	.bFunctionCall = true, .bDefinitionNames = true, .bDecorators = true, .bStringPrefixes = true,
};
constexpr SLanguageRule kJavaScriptRule{
	.lineComment1 = L"//", .blockCommentBegin = L"/*", .blockCommentEnd = L"*/", .quotes = L"\"'`",
	.keywords = kJavaScriptKeywords, .constants = kJavaScriptConstants, .bFunctionCall = true,
	.bDefinitionNames = true, .bDecorators = true,
};
constexpr SLanguageRule kJsonRule{
	.lineComment1 = L"//", .blockCommentBegin = L"/*", .blockCommentEnd = L"*/", .quotes = L"\"",
	.constants = kJsonConstants, .bJsonKeys = true,
};
constexpr SLanguageRule kYamlRule{
	.lineComment1 = L"#", .bCommentAtWordStart = true, .quotes = L"\"'", .bEscapeInSingleQuote = false,
	.bDoubledQuote = true, .bCaseInsensitive = true, .constants = kYamlConstants, .bYamlKeys = true,
};
constexpr SLanguageRule kSqlRule{
	.lineComment1 = L"--", .blockCommentBegin = L"/*", .blockCommentEnd = L"*/", .quotes = L"'\"",
	.chEscape = L'\0', .bDoubledQuote = true, .bCaseInsensitive = true, .keywords = kSqlKeywords,
	.constants = kSqlConstants, .bFunctionCall = true,
};
constexpr SLanguageRule kCLikeRule{
	.lineComment1 = L"//", .blockCommentBegin = L"/*", .blockCommentEnd = L"*/", .quotes = L"\"'`",
	.keywords = kCLikeKeywords, .constants = kCLikeConstants, .bFunctionCall = true, .bDecorators = true,
	.bPreprocessor = true,
};
constexpr SLanguageRule kIniRule{
	.lineComment1 = L"#", .lineComment2 = L";", .bCommentAtWordStart = true, .quotes = L"\"'",
	.bCaseInsensitive = true, .constants = kIniConstants, .bIniKeys = true,
};
constexpr SLanguageRule kDockerfileRule{
	.lineComment1 = L"#", .bCommentAtWordStart = true, .quotes = L"\"'", .bEscapeInSingleQuote = false,
	.bCaseInsensitive = true, .keywords = kDockerInstructions, .bDollarVariables = true, .bInstructions = true,
};

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                          文字の判定                         //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

bool IsSpace(wchar_t ch) noexcept { return ch == L' ' || ch == L'\t'; }
bool IsDigit(wchar_t ch) noexcept { return L'0' <= ch && ch <= L'9'; }
bool IsHexDigit(wchar_t ch) noexcept { return IsDigit(ch) || (L'a' <= ch && ch <= L'f') || (L'A' <= ch && ch <= L'F'); }
//! 語の始まりの文字。日本語などの文字は語にしない(文章に色が付かないように)
bool IsWordStart(wchar_t ch) noexcept { return (L'a' <= ch && ch <= L'z') || (L'A' <= ch && ch <= L'Z') || ch == L'_'; }
bool IsWordChar(wchar_t ch) noexcept { return IsWordStart(ch) || IsDigit(ch); }

wchar_t ToLowerAscii(wchar_t ch) noexcept { return (L'A' <= ch && ch <= L'Z') ? wchar_t(ch - L'A' + L'a') : ch; }

bool EqualsWord(std::wstring_view a, std::wstring_view b, bool bCaseInsensitive) noexcept
{
	if (!bCaseInsensitive) return a == b;
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return ToLowerAscii(x) == ToLowerAscii(y); });
}

bool ContainsWord(Words words, std::wstring_view word, bool bCaseInsensitive) noexcept
{
	return std::ranges::any_of(words, [&](std::wstring_view w) { return EqualsWord(w, word, bCaseInsensitive); });
}

std::wstring_view TrimEol(std::wstring_view s) noexcept
{
	while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.remove_suffix(1);
	return s;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                           字句解析                          //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

class CCodeLineLexer {
public:
	CCodeLineLexer(const SLanguageRule& rule, std::wstring_view line) noexcept
		: m_rule(rule), m_line(line), m_n(line.size()) {}

	std::vector<CodeToken> Lex();

private:
	void Add(size_t nBegin, size_t nEnd, ECodeTokenKind eKind) { if (nBegin < nEnd) m_tokens.push_back({ nBegin, nEnd, eKind }); }
	bool StartsWith(size_t i, std::wstring_view s) const noexcept { return !s.empty() && m_line.substr(i).starts_with(s); }
	wchar_t At(size_t i) const noexcept { return i < m_n ? m_line[i] : L'\0'; }
	size_t SkipSpaces(size_t i) const noexcept { while (i < m_n && IsSpace(m_line[i])) ++i; return i; }
	size_t ScanWord(size_t i) const noexcept { while (i < m_n && IsWordChar(m_line[i])) ++i; return i; }
	size_t ScanString(size_t i, wchar_t chQuote, bool* pbClosed = nullptr) const noexcept;
	size_t ScanNumber(size_t i) const noexcept;
	size_t ScanVariable(size_t i) const noexcept;
	size_t LexYamlKey(size_t i);
	size_t LexIniPrefix(size_t i);
	bool StartsLineComment(size_t i) const noexcept;
	void LexWord(size_t& i);

	const SLanguageRule&	m_rule;
	std::wstring_view		m_line;
	size_t					m_n;
	std::vector<CodeToken>	m_tokens;
	bool					m_bCommandPos = true;		//!< 次の語がコマンドの最初の語か(シェル)
	bool					m_bAssignPos = false;		//!< 次の NAME= を変数にするか(シェルの export の後ろ)
	bool					m_bDefinitionPos = false;	//!< 次の語が定義の名前か(def の後ろ)
	bool					m_bFirstWord = true;		//!< まだ行の最初の語を読んでいないか
};

//! 引用符で始まる文字列の終わり(閉じの引用符の次)。閉じていなければ行末
size_t CCodeLineLexer::ScanString(size_t i, wchar_t chQuote, bool* pbClosed) const noexcept
{
	if (pbClosed) *pbClosed = true;
	const bool bEscape = m_rule.chEscape != L'\0' && (chQuote != L'\'' || m_rule.bEscapeInSingleQuote);
	for (size_t j = i + 1; j < m_n; ++j) {
		const wchar_t ch = m_line[j];
		if (bEscape && ch == m_rule.chEscape) {
			++j;
		} else if (ch == chQuote) {
			if (m_rule.bDoubledQuote && At(j + 1) == chQuote) {
				++j;
			} else {
				return j + 1;
			}
		}
	}
	if (pbClosed) *pbClosed = false;
	return m_n;
}

size_t CCodeLineLexer::ScanNumber(size_t i) const noexcept
{
	if (m_line[i] == L'0' && (At(i + 1) == L'x' || At(i + 1) == L'X') && IsHexDigit(At(i + 2))) {
		size_t j = i + 2;
		while (j < m_n && IsHexDigit(m_line[j])) ++j;
		return j;
	}
	size_t j = i;
	while (j < m_n && (IsDigit(m_line[j]) || m_line[j] == L'_')) ++j;
	if (At(j) == L'.' && IsDigit(At(j + 1))) {
		j += 1;
		while (j < m_n && IsDigit(m_line[j])) ++j;
	}
	if ((At(j) == L'e' || At(j) == L'E') && (IsDigit(At(j + 1)) || ((At(j + 1) == L'+' || At(j + 1) == L'-') && IsDigit(At(j + 2))))) {
		j += 2;
		while (j < m_n && IsDigit(m_line[j])) ++j;
	}
	return j;
}

//! $で始まる変数の終わり。変数でなければi
size_t CCodeLineLexer::ScanVariable(size_t i) const noexcept
{
	const wchar_t ch = At(i + 1);
	if (ch == L'{') {
		const size_t e = m_line.find(L'}', i + 2);
		return e == std::wstring_view::npos ? m_n : e + 1;
	}
	if (IsWordStart(ch)) {
		size_t j = ScanWord(i + 1);
		// PowerShell の $env:PATH のようなスコープ付きの変数
		if (m_rule.bCmdlets && At(j) == L':' && IsWordStart(At(j + 1))) {
			j = ScanWord(j + 1);
		}
		return j;
	}
	if (IsDigit(ch) || (ch != L'\0' && std::wstring_view(L"@#?$!*-").find(ch) != std::wstring_view::npos)) {
		return i + 2;
	}
	return i;
}

bool CCodeLineLexer::StartsLineComment(size_t i) const noexcept
{
	if (m_rule.bCommentAtWordStart && i != 0 && !IsSpace(m_line[i - 1])) {
		return false;
	}
	return StartsWith(i, m_rule.lineComment1) || StartsWith(i, m_rule.lineComment2);
}

//! YAMLの行頭の key: (リストの - の後ろを含む)。キーの後ろの位置を返す
size_t CCodeLineLexer::LexYamlKey(size_t i)
{
	i = SkipSpaces(i);
	if (At(i) == L'-' && (i + 1 == m_n || IsSpace(At(i + 1)))) {
		i = SkipSpaces(i + 1);
	}
	const size_t nKey = i;
	size_t j = i;
	if (At(j) == L'"' || At(j) == L'\'') {
		j = ScanString(j, At(j));
	} else {
		while (j < m_n && m_line[j] != L':' && m_line[j] != L'#' && m_line[j] != L'{' && m_line[j] != L'[') ++j;
	}
	if (nKey < j && At(j) == L':' && (j + 1 == m_n || IsSpace(At(j + 1)))) {
		size_t nKeyEnd = j;
		while (nKey < nKeyEnd && IsSpace(m_line[nKeyEnd - 1])) --nKeyEnd;
		Add(nKey, nKeyEnd, ECodeTokenKind::Variable);
		return j + 1;
	}
	return nKey;
}

//! iniの行頭の [section] と key= 。後ろの位置を返す。行全体がコメントならm_n
size_t CCodeLineLexer::LexIniPrefix(size_t i)
{
	i = SkipSpaces(i);
	if (At(i) == L';' || At(i) == L'#') {
		Add(i, m_n, ECodeTokenKind::Comment);
		return m_n;
	}
	if (At(i) == L'[') {
		const size_t e = m_line.find(L']', i + 1);
		const size_t nEnd = e == std::wstring_view::npos ? m_n : e + 1;
		Add(i, nEnd, ECodeTokenKind::Keyword);
		return nEnd;
	}
	const size_t nSep = m_line.find_first_of(L"=:", i);
	if (nSep != std::wstring_view::npos && i < nSep) {
		size_t nKeyEnd = nSep;
		while (i < nKeyEnd && IsSpace(m_line[nKeyEnd - 1])) --nKeyEnd;
		Add(i, nKeyEnd, ECodeTokenKind::Variable);
		return nSep + 1;
	}
	return i;
}

//! 語を読み、種類を決める
void CCodeLineLexer::LexWord(size_t& i)
{
	const bool bCommandWord = m_rule.bCommandPosition && m_bCommandPos;
	size_t j = i;
	if (bCommandWord || m_rule.bCmdlets) {
		// コマンド名は、パス(/usr/bin/env、./run.sh)や - を含むことがある
		while (j < m_n && (IsWordChar(m_line[j]) || std::wstring_view(L"./~+-").find(m_line[j]) != std::wstring_view::npos)) ++j;
	} else {
		j = ScanWord(i);
	}
	const std::wstring_view word = m_line.substr(i, j - i);
	const bool bFirstWord = m_bFirstWord;
	m_bFirstWord = false;
	const bool bCommandPos = m_bCommandPos;
	const bool bAssignPos = m_bAssignPos;
	const bool bDefinitionPos = m_bDefinitionPos;
	m_bCommandPos = false;
	m_bAssignPos = false;
	m_bDefinitionPos = false;

	// r"..." などの接頭辞の付いた文字列
	if (m_rule.bStringPrefixes && word.size() <= 2 && (At(j) == L'"' || At(j) == L'\'')
		&& std::ranges::all_of(word, [](wchar_t ch) { return std::wstring_view(L"rRbBfFuU").find(ch) != std::wstring_view::npos; })) {
		const size_t e = ScanString(j, At(j));
		Add(i, e, ECodeTokenKind::String);
		i = e;
		return;
	}

	const bool bKeyword = (!m_rule.bInstructions || bFirstWord) && ContainsWord(m_rule.keywords, word, m_rule.bCaseInsensitive);
	if (bKeyword) {
		Add(i, j, ECodeTokenKind::Keyword);
		if (m_rule.bCommandPosition) {
			m_bCommandPos = ContainsWord(kShellCommandStarters, word, m_rule.bCaseInsensitive);
			m_bAssignPos = ContainsWord(kShellAssignKeywords, word, m_rule.bCaseInsensitive);
		}
		if (m_rule.bDefinitionNames || m_rule.bDecorators) {
			m_bDefinitionPos = word == L"def" || word == L"class" || word == L"function";
		}
		if (m_rule.bInstructions) {
			m_bCommandPos = true;
		}
	} else if (ContainsWord(m_rule.constants, word, m_rule.bCaseInsensitive)) {
		Add(i, j, ECodeTokenKind::Number);
	} else if (bDefinitionPos) {
		Add(i, j, ECodeTokenKind::Function);
	} else if (m_rule.bCommandPosition && At(j) == L'=' && (bCommandPos || bAssignPos)
		&& std::ranges::all_of(word, IsWordChar)) {
		// NAME=値 の代入。値はコマンド名にしない
		Add(i, j, ECodeTokenKind::Variable);
	} else if (bCommandWord) {
		Add(i, j, ECodeTokenKind::Function);
	} else if (m_rule.bCmdlets && word.find(L'-') != std::wstring_view::npos && IsWordStart(word.front()) && IsWordChar(word.back())) {
		Add(i, j, ECodeTokenKind::Function);
	} else if (m_rule.bFunctionCall && At(SkipSpaces(j)) == L'(') {
		Add(i, j, ECodeTokenKind::Function);
	}
	i = j;
}

std::vector<CodeToken> CCodeLineLexer::Lex()
{
	size_t i = 0;
	if (m_rule.bIniKeys) {
		i = LexIniPrefix(i);
	} else if (m_rule.bYamlKeys) {
		i = LexYamlKey(i);
	}
	bool bLineStart = true;		// ここまで空白だけか
	while (i < m_n) {
		const wchar_t ch = m_line[i];
		if (IsSpace(ch)) {
			++i;
			continue;
		}
		if (StartsLineComment(i)) {
			Add(i, m_n, ECodeTokenKind::Comment);
			break;
		}
		if (StartsWith(i, m_rule.blockCommentBegin)) {
			const size_t e = m_line.find(m_rule.blockCommentEnd, i + m_rule.blockCommentBegin.size());
			const size_t nEnd = e == std::wstring_view::npos ? m_n : e + m_rule.blockCommentEnd.size();
			Add(i, nEnd, ECodeTokenKind::Comment);
			i = nEnd;
			continue;
		}
		if (m_rule.bPreprocessor && bLineStart && ch == L'#') {
			const size_t j = ScanWord(SkipSpaces(i + 1));
			Add(i, j, ECodeTokenKind::Keyword);
			i = j;
			bLineStart = false;
			continue;
		}
		bLineStart = false;

		if (m_rule.quotes.find(ch) != std::wstring_view::npos) {
			bool bClosed = true;
			const size_t j = ScanString(i, ch, &bClosed);
			if (m_rule.bClosedStringsOnly && !bClosed) {
				++i;
				continue;
			}
			const bool bKey = m_rule.bJsonKeys && At(SkipSpaces(j)) == L':';
			Add(i, j, bKey ? ECodeTokenKind::Variable : ECodeTokenKind::String);
			i = j;
			m_bCommandPos = false;
			m_bFirstWord = false;
			continue;
		}
		if (m_rule.bDollarVariables && ch == L'$') {
			const size_t j = ScanVariable(i);
			if (i < j) {
				const bool bConstant = ContainsWord(m_rule.constants, m_line.substr(i + 1, j - i - 1), m_rule.bCaseInsensitive);
				Add(i, j, bConstant ? ECodeTokenKind::Number : ECodeTokenKind::Variable);
				i = j;
				m_bCommandPos = false;
				continue;
			}
			// $( ... ) の中はコマンド
			if (At(i + 1) == L'(') {
				m_bCommandPos = true;
				i += 2;
				continue;
			}
		}
		if (m_rule.bDecorators && ch == L'@' && IsWordStart(At(i + 1))) {
			const size_t j = ScanWord(i + 1);
			Add(i, j, ECodeTokenKind::Function);
			i = j;
			continue;
		}
		if (!m_rule.dashOperators.empty() && ch == L'-' && IsWordStart(At(i + 1)) && (i == 0 || !IsWordChar(m_line[i - 1]))) {
			const size_t j = ScanWord(i + 1);
			if (ContainsWord(m_rule.dashOperators, m_line.substr(i + 1, j - i - 1), m_rule.bCaseInsensitive)) {
				Add(i, j, ECodeTokenKind::Keyword);
			}
			i = j;
			continue;
		}
		if (IsDigit(ch) && (i == 0 || !IsWordChar(m_line[i - 1]))) {
			const size_t j = ScanNumber(i);
			if (IsWordChar(At(j))) {
				// 2nd、x86_64 の 86 のような語の一部
				i = ScanWord(j);
			} else {
				Add(i, j, ECodeTokenKind::Number);
				i = j;
			}
			m_bCommandPos = false;
			m_bFirstWord = false;
			continue;
		}
		if (IsWordStart(ch) || (m_rule.bCommandPosition && m_bCommandPos && (ch == L'.' || ch == L'/' || ch == L'~'))) {
			LexWord(i);
			continue;
		}
		if (m_rule.bCommandPosition) {
			// | ; & ( ` { ! の後ろは、次のコマンドの始まり
			m_bCommandPos = std::wstring_view(L"|;&(`{!").find(ch) != std::wstring_view::npos;
		}
		++i;
	}
	return std::move(m_tokens);
}

//! HTMLとXML
std::vector<CodeToken> LexHtml(std::wstring_view line)
{
	std::vector<CodeToken> tokens;
	const auto add = [&](size_t b, size_t e, ECodeTokenKind k) { if (b < e) tokens.push_back({ b, e, k }); };
	const size_t n = line.size();
	const auto at = [&](size_t i) { return i < n ? line[i] : L'\0'; };
	size_t i = 0;
	while (i < n) {
		if (line.substr(i).starts_with(L"<!--")) {
			const size_t e = line.find(L"-->", i + 4);
			const size_t nEnd = e == std::wstring_view::npos ? n : e + 3;
			add(i, nEnd, ECodeTokenKind::Comment);
			i = nEnd;
			continue;
		}
		if (line[i] == L'<' && (IsWordStart(at(i + 1)) || at(i + 1) == L'/' || at(i + 1) == L'!' || at(i + 1) == L'?')) {
			size_t j = i + 1;
			if (!IsWordStart(line[j])) ++j;
			while (j < n && (IsWordChar(line[j]) || line[j] == L'-' || line[j] == L':' || line[j] == L'.')) ++j;
			add(i, j, ECodeTokenKind::Tag);
			i = j;
			bool bValue = false;	// = の後ろ
			while (i < n) {
				const wchar_t ch = line[i];
				if (IsSpace(ch)) {
					++i;
				} else if (ch == L'>') {
					add(i, i + 1, ECodeTokenKind::Tag);
					++i;
					break;
				} else if ((ch == L'/' || ch == L'?') && at(i + 1) == L'>') {
					add(i, i + 2, ECodeTokenKind::Tag);
					i += 2;
					break;
				} else if (ch == L'"' || ch == L'\'') {
					const size_t e = line.find(ch, i + 1);
					const size_t nEnd = e == std::wstring_view::npos ? n : e + 1;
					add(i, nEnd, ECodeTokenKind::String);
					i = nEnd;
					bValue = false;
				} else if (ch == L'=') {
					++i;
					bValue = true;
				} else {
					size_t e = i;
					while (e < n && !IsSpace(line[e]) && std::wstring_view(L"=>\"'").find(line[e]) == std::wstring_view::npos
						&& !(line[e] == L'/' && at(e + 1) == L'>')) ++e;
					if (e == i) ++e;
					add(i, e, bValue ? ECodeTokenKind::String : ECodeTokenKind::Attribute);
					i = e;
					bValue = false;
				}
			}
			continue;
		}
		if (line[i] == L'&') {
			const size_t e = line.find(L';', i + 1);
			if (e != std::wstring_view::npos && e - i <= 10
				&& std::all_of(line.begin() + i + 1, line.begin() + e, [](wchar_t ch) { return IsWordChar(ch) || ch == L'#'; })) {
				add(i, e + 1, ECodeTokenKind::Number);
				i = e + 1;
				continue;
			}
		}
		++i;
	}
	return tokens;
}

//! 差分(diff、patch)
std::vector<CodeToken> LexDiff(std::wstring_view line)
{
	const size_t n = line.size();
	if (line.starts_with(L"+++") || line.starts_with(L"---") || line.starts_with(L"diff ") || line.starts_with(L"index ")) {
		return { { 0, n, ECodeTokenKind::Keyword } };
	}
	if (line.starts_with(L"@@")) {
		return { { 0, n, ECodeTokenKind::Function } };
	}
	if (line.starts_with(L"+")) {
		return { { 0, n, ECodeTokenKind::Inserted } };
	}
	if (line.starts_with(L"-")) {
		return { { 0, n, ECodeTokenKind::Deleted } };
	}
	return {};
}

} // namespace

ECodeLanguage GetCodeLanguage(std::wstring_view infoString)
{
	// 最初の語。{.python} や python{...} のような書き方も受け付ける
	size_t i = 0;
	while (i < infoString.size() && (IsSpace(infoString[i]) || infoString[i] == L'{' || infoString[i] == L'.')) ++i;
	size_t j = i;
	while (j < infoString.size() && !IsSpace(infoString[j]) && infoString[j] != L'{' && infoString[j] != L'}' && infoString[j] != L',') ++j;
	const std::wstring_view name = infoString.substr(i, j - i);
	if (name.empty()) {
		return ECodeLanguage::Generic;
	}

	struct SAlias {
		std::wstring_view	name;
		ECodeLanguage		eLanguage;
	};
	static constexpr SAlias kAliases[] = {
		{ L"text", ECodeLanguage::None }, { L"txt", ECodeLanguage::None }, { L"plain", ECodeLanguage::None },
		{ L"plaintext", ECodeLanguage::None }, { L"log", ECodeLanguage::None },
		{ L"sh", ECodeLanguage::Shell }, { L"bash", ECodeLanguage::Shell }, { L"zsh", ECodeLanguage::Shell },
		{ L"ksh", ECodeLanguage::Shell }, { L"shell", ECodeLanguage::Shell }, { L"shellscript", ECodeLanguage::Shell },
		{ L"console", ECodeLanguage::Shell }, { L"terminal", ECodeLanguage::Shell },
		{ L"powershell", ECodeLanguage::PowerShell }, { L"ps", ECodeLanguage::PowerShell },
		{ L"ps1", ECodeLanguage::PowerShell }, { L"pwsh", ECodeLanguage::PowerShell },
		{ L"python", ECodeLanguage::Python }, { L"py", ECodeLanguage::Python }, { L"python3", ECodeLanguage::Python },
		{ L"javascript", ECodeLanguage::JavaScript }, { L"js", ECodeLanguage::JavaScript },
		{ L"jsx", ECodeLanguage::JavaScript }, { L"mjs", ECodeLanguage::JavaScript },
		{ L"typescript", ECodeLanguage::JavaScript }, { L"ts", ECodeLanguage::JavaScript },
		{ L"tsx", ECodeLanguage::JavaScript },
		{ L"json", ECodeLanguage::Json }, { L"jsonc", ECodeLanguage::Json }, { L"json5", ECodeLanguage::Json },
		{ L"yaml", ECodeLanguage::Yaml }, { L"yml", ECodeLanguage::Yaml },
		{ L"sql", ECodeLanguage::Sql }, { L"mysql", ECodeLanguage::Sql }, { L"postgresql", ECodeLanguage::Sql },
		{ L"psql", ECodeLanguage::Sql }, { L"plsql", ECodeLanguage::Sql }, { L"tsql", ECodeLanguage::Sql },
		{ L"sqlite", ECodeLanguage::Sql },
		{ L"c", ECodeLanguage::CLike }, { L"h", ECodeLanguage::CLike }, { L"cpp", ECodeLanguage::CLike },
		{ L"c++", ECodeLanguage::CLike }, { L"cc", ECodeLanguage::CLike }, { L"cxx", ECodeLanguage::CLike },
		{ L"hpp", ECodeLanguage::CLike }, { L"cs", ECodeLanguage::CLike }, { L"csharp", ECodeLanguage::CLike },
		{ L"c#", ECodeLanguage::CLike }, { L"java", ECodeLanguage::CLike }, { L"go", ECodeLanguage::CLike },
		{ L"golang", ECodeLanguage::CLike }, { L"rust", ECodeLanguage::CLike }, { L"rs", ECodeLanguage::CLike },
		{ L"kotlin", ECodeLanguage::CLike }, { L"kt", ECodeLanguage::CLike }, { L"swift", ECodeLanguage::CLike },
		{ L"scala", ECodeLanguage::CLike }, { L"dart", ECodeLanguage::CLike }, { L"php", ECodeLanguage::CLike },
		{ L"groovy", ECodeLanguage::CLike }, { L"objc", ECodeLanguage::CLike },
		{ L"html", ECodeLanguage::Html }, { L"htm", ECodeLanguage::Html }, { L"xml", ECodeLanguage::Html },
		{ L"xhtml", ECodeLanguage::Html }, { L"svg", ECodeLanguage::Html }, { L"xaml", ECodeLanguage::Html },
		{ L"vue", ECodeLanguage::Html },
		{ L"ini", ECodeLanguage::Ini }, { L"toml", ECodeLanguage::Ini }, { L"conf", ECodeLanguage::Ini },
		{ L"cfg", ECodeLanguage::Ini }, { L"properties", ECodeLanguage::Ini }, { L"env", ECodeLanguage::Ini },
		{ L"dotenv", ECodeLanguage::Ini }, { L"editorconfig", ECodeLanguage::Ini },
		{ L"dockerfile", ECodeLanguage::Dockerfile }, { L"docker", ECodeLanguage::Dockerfile },
		{ L"containerfile", ECodeLanguage::Dockerfile },
		{ L"diff", ECodeLanguage::Diff }, { L"patch", ECodeLanguage::Diff },
	};
	for (const auto& alias : kAliases) {
		if (EqualsWord(alias.name, name, true)) {
			return alias.eLanguage;
		}
	}
	return ECodeLanguage::Generic;
}

std::vector<CodeToken> TokenizeCodeLine(ECodeLanguage eLanguage, std::wstring_view line)
{
	line = TrimEol(line);
	const SLanguageRule* pRule = nullptr;
	switch (eLanguage) {
	case ECodeLanguage::None:		return {};
	case ECodeLanguage::Html:		return LexHtml(line);
	case ECodeLanguage::Diff:		return LexDiff(line);
	case ECodeLanguage::Shell:		pRule = &kShellRule;		break;
	case ECodeLanguage::PowerShell:	pRule = &kPowerShellRule;	break;
	case ECodeLanguage::Python:		pRule = &kPythonRule;		break;
	case ECodeLanguage::JavaScript:	pRule = &kJavaScriptRule;	break;
	case ECodeLanguage::Json:		pRule = &kJsonRule;			break;
	case ECodeLanguage::Yaml:		pRule = &kYamlRule;			break;
	case ECodeLanguage::Sql:		pRule = &kSqlRule;			break;
	case ECodeLanguage::CLike:		pRule = &kCLikeRule;		break;
	case ECodeLanguage::Ini:		pRule = &kIniRule;			break;
	case ECodeLanguage::Dockerfile:	pRule = &kDockerfileRule;	break;
	default:						pRule = &kGenericRule;		break;
	}
	return CCodeLineLexer(*pRule, line).Lex();
}

} // namespace markdown
