/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "StdAfx.h"
#include <string_view>
#include <vector>
#include "types/CType.h"
#include "doc/CEditDoc.h"
#include "doc/CDocOutline.h"
#include "doc/logic/CDocLine.h"
#include "outline/CFuncInfoArr.h"
#include "markdown/MarkdownHeadingScanner.h"
#include "view/colors/EColorIndexType.h"
#include "CSelectLang.h"

namespace {

//! 正規表現キーワードの既定値
struct SMarkdownRegexKeyword {
	EColorIndexType	eColorIndex;	//!< 色指定番号
	const wchar_t*	pszKeyword;		//!< 正規表現キーワード
};

/*!
	Markdownの正規表現キーワード

	同じ位置で複数が一致するときは、先に書いたものが優先される。
	行頭(/^)のものは行全体、または行頭の記号に色を付ける。
	正規表現キーワードは1行ずつ調べるので、行をまたぐ強調には色が付かない。
	フェンスで囲んだコードブロックは、ブロックコメントとして色を付ける(その中では正規表現キーワードを調べない)。
*/
const SMarkdownRegexKeyword g_aMarkdownRegexKeywords[] = {
	// 行頭
	{ COLORIDX_REGEX1,	LR"(/^ {0,3}#{1,6}(?=[ \t\r\n]|$)[^\r\n]*/k)" },								// ATX見出し
	{ COLORIDX_REGEX1,	LR"(/^ {0,3}=+[ \t]*(?=[\r\n]|$)/k)" },										// Setext見出しの下線(=)
	{ COLORIDX_REGEX9,	LR"(/^ {0,3}(?:(?:\*[ \t]*){3,}|(?:-[ \t]*){3,}|(?:_[ \t]*){3,})(?=[\r\n]|$)/k)" },	// 区切り線
	{ COLORIDX_REGEX9,	LR"(/^[ \t]*\|?[ \t]*:?-+:?[ \t]*(?:\|[ \t]*:?-+:?[ \t]*)+\|?[ \t]*(?=[\r\n]|$)/k)" },	// 表の区切りの行
	{ COLORIDX_REGEX7,	LR"(/^ {0,3}>[^\r\n]*/k)" },													// 引用
	{ COLORIDX_REGEX8,	LR"(/^[ \t]*(?:[-*+]|\d{1,9}[.)])[ \t]+\[[ xX]\](?=[ \t\r\n]|$)/k)" },			// タスクリストの記号
	{ COLORIDX_REGEX8,	LR"(/^[ \t]*(?:[-*+]|\d{1,9}[.)])(?=[ \t\r\n]|$)/k)" },						// リストの記号
	{ COLORIDX_REGEX6,	LR"(/^ {0,3}\[[^\]\r\n]+\]:[ \t]*\S[^\r\n]*/k)" },								// リンクの参照先の定義
	// 行内
	{ COLORIDX_TEXT,	LR"(/\\[!-\/:-@\[-`{-~]/k)" },													// バックスラッシュでエスケープした記号
	{ COLORIDX_REGEX10,	LR"(/<!--.*?-->/k)" },															// HTMLのコメント(1行で閉じるもの)
	{ COLORIDX_REGEX5,	LR"(/(`+)(?!`)[^\r\n]*?(?<!`)\1(?!`)/k)" },										// インラインコード
	{ COLORIDX_REGEX6,	LR"(/!?\[(?:[^\[\]\r\n]|\[[^\[\]\r\n]*\])*\]\([^)\r\n]*\)/k)" },				// リンク、画像
	{ COLORIDX_REGEX6,	LR"(/!?\[(?:[^\[\]\r\n]|\[[^\[\]\r\n]*\])+\]\[[^\[\]\r\n]*\]/k)" },				// 参照形式のリンク
	{ COLORIDX_REGEX2,	LR"(/\*\*\*(?=[^\s*])[^\r\n]*?(?<!\s)\*\*\*/k)" },								// 太字の斜体
	{ COLORIDX_REGEX2,	LR"(/\*\*(?=[^\s*])[^\r\n]*?(?<!\s)\*\*/k)" },									// 太字(*)
	{ COLORIDX_REGEX2,	LR"(/(?<!\w)__(?=[^\s_])[^\r\n]*?(?<!\s)__(?!\w)/k)" },							// 太字(_)
	{ COLORIDX_REGEX4,	LR"(/~~(?=[^\s~])[^\r\n]*?(?<!\s)~~/k)" },										// 取り消し線
	{ COLORIDX_REGEX3,	LR"(/\*(?=[^\s*])[^\r\n*]*(?<!\s)\*/k)" },										// 斜体(*)
	{ COLORIDX_REGEX3,	LR"(/(?<!\w)_(?=[^\s_])[^\r\n_]*(?<!\s)_(?!\w)/k)" },							// 斜体(_)
	{ COLORIDX_REGEX10,	LR"(/<\/?[A-Za-z][A-Za-z0-9-]*(?:\s[^<>\r\n]*)?\/?>/k)" },						// HTMLのタグ
	{ COLORIDX_REGEX9,	LR"(/\|/k)" },																	// 表の列の区切り
};

void SetMarkdownColor(ColorInfo& info, COLORREF crText, bool bBold = false)
{
	info.m_bDisp = true;
	info.m_sFontAttr.m_bBoldFont = bBold;
	info.m_sColorAttr.m_cTEXT = crText;
}

} // namespace

/* Markdown */
void CType_Markdown::InitTypeConfigImp(STypeConfig* pType)
{
	//名前と拡張子
	wcscpy( pType->m_szTypeName, L"Markdown" );
	wcscpy( pType->m_szTypeExts, L"md,markdown,mdown,mkd" );

	//設定
	pType->m_nTextWrapMethod = WRAP_WINDOW_WIDTH;				// テキストの折り返し方法(右端で折り返す)
	pType->m_eDefaultOutline = OUTLINE_MARKDOWN;				/* アウトライン解析方法 */
	pType->m_cBlockComments[0].SetBlockCommentRule( L"```", L"```" );	/* フェンスで囲んだコードブロック */
	pType->m_cBlockComments[1].SetBlockCommentRule( L"~~~", L"~~~" );	/* フェンスで囲んだコードブロック2 */
	pType->m_ColorInfoArr[COLORIDX_SSTRING].m_bDisp = false;	//シングルクォーテーション文字列を色分け表示しない
	pType->m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp = false;	//ダブルクォーテーション文字列を色分け表示しない

	//色(VS CodeのLight+テーマのMarkdownの配色を参考にした)
	auto& colors = pType->m_ColorInfoArr;
	SetMarkdownColor( colors[COLORIDX_COMMENT], RGB( 163,  21,  21 ) );			// コードブロック
	colors[COLORIDX_COMMENT].m_sColorAttr.m_cBACK = RGB( 238, 238, 238 );
	SetMarkdownColor( colors[COLORIDX_REGEX1],  RGB( 128,   0,   0 ), true );	// 見出し
	SetMarkdownColor( colors[COLORIDX_REGEX2],  RGB(   0,   0, 128 ), true );	// 太字
	SetMarkdownColor( colors[COLORIDX_REGEX3],  RGB(  38, 127, 153 ) );			// 斜体
	SetMarkdownColor( colors[COLORIDX_REGEX4],  RGB( 128, 128, 128 ) );			// 取り消し線
	SetMarkdownColor( colors[COLORIDX_REGEX5],  RGB( 163,  21,  21 ) );			// インラインコード
	colors[COLORIDX_REGEX5].m_sColorAttr.m_cBACK = RGB( 238, 238, 238 );
	SetMarkdownColor( colors[COLORIDX_REGEX6],  RGB(   0,   0, 255 ) );			// リンク、画像
	SetMarkdownColor( colors[COLORIDX_REGEX7],  RGB(   0, 128,   0 ) );			// 引用
	SetMarkdownColor( colors[COLORIDX_REGEX8],  RGB(   4,  81, 165 ), true );	// リストの記号
	SetMarkdownColor( colors[COLORIDX_REGEX9],  RGB( 128, 128, 128 ), true );	// 区切り線、表の区切り
	SetMarkdownColor( colors[COLORIDX_REGEX10], RGB( 175,   0, 219 ) );			// HTMLのタグとコメント

	//正規表現キーワード
	static_assert( std::size(g_aMarkdownRegexKeywords) <= MAX_REGEX_KEYWORD );
	int keywordPos = 0;
	wchar_t* pKeyword = pType->m_RegexKeywordList;
	pType->m_bUseRegexKeyword = true;							// 正規表現キーワードを使うか
	for( size_t i = 0; i < std::size(g_aMarkdownRegexKeywords); ++i ){
		pType->m_RegexKeywordArr[i].m_nColorIndex = g_aMarkdownRegexKeywords[i].eColorIndex;	// 色指定番号
		::wcsncpy_s(&pKeyword[keywordPos],						// 正規表現キーワード
			std::size(pType->m_RegexKeywordList) - keywordPos,
			g_aMarkdownRegexKeywords[i].pszKeyword,
			_TRUNCATE
		);
		keywordPos += int(wcslen(&pKeyword[keywordPos]) + 1);
	}
	pKeyword[keywordPos] = L'\0';
}

/*!	Markdown アウトライン解析

	ATX見出し(# 見出し)とSetext見出し(下線が=や-)を、見出しのレベルで階層にする。
	レベルが飛ぶとき(# の次が ### など)は、無題の階層を挟まずに、すぐ下の階層にする。
*/
void CDocOutline::MakeTopicList_markdown( CFuncInfoArr* pcFuncInfoArr )
{
	markdown::CMarkdownHeadingScanner cScanner;
	std::vector<int> vLevelStack;	// 親の見出しのレベル

	const CLogicInt nLineCount = m_pcDocRef->m_cDocLineMgr.GetLineCount();
	for( CLogicInt nLine = CLogicInt(0); nLine < nLineCount; ++nLine ){
		const CDocLine* pcDocLine = m_pcDocRef->m_cDocLineMgr.GetLine( nLine );
		if( !pcDocLine ){
			break;
		}
		const auto heading = cScanner.Feed( std::wstring_view( pcDocLine->GetPtr(), pcDocLine->GetLengthWithoutEOL() ) );
		if( !heading ){
			continue;
		}

		while( !vLevelStack.empty() && heading->nLevel <= vLevelStack.back() ){
			vLevelStack.pop_back();
		}
		const int nDepth = int(vLevelStack.size());
		vLevelStack.push_back( heading->nLevel );

		CLayoutPoint ptPos;
		m_pcDocRef->m_cLayoutMgr.LogicToLayout(
			CLogicPoint(0, heading->nLine),
			&ptPos
		);
		const wchar_t* pszTitle = heading->strTitle.empty() ? LS(STR_NO_TITLE1) : heading->strTitle.c_str();
		pcFuncInfoArr->AppendData( CLogicInt(heading->nLine + 1), ptPos.GetY2() + CLayoutInt(1), pszTitle, 0, nDepth );
	}
}
