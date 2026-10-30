/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "StdAfx.h"
#include <span>
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
	フェンスで囲んだコードブロックの中は、CColor_MarkdownFence が色を付ける(その中では正規表現キーワードを調べない)。
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

//! Markdownの既定の色(既定値から変えるもの)
struct SMarkdownColor {
	EColorIndexType	eColorIndex;
	COLORREF		crText;
	bool			bBold;
	bool			bUnderLine;
	COLORREF		crBack;		//!< CLR_INVALIDなら既定値のまま
};

constexpr COLORREF kCodeBack = RGB( 234, 236, 239 );	//!< コードの背景

/*!
	今の版の色。配色はGitHubの画面を参考にした。
	コードブロックの中の字句には、強調キーワード1～10を使う(CColor_MarkdownFence)。
	変えたときは MARKDOWN_TYPE_VERSION を上げ、前の版の色を g_aMarkdownColorsV* に残して、
	UpgradeMarkdownTypeConfig で置き換えられるようにする。
*/
constexpr SMarkdownColor g_aMarkdownColors[] = {
	{ COLORIDX_COMMENT,		RGB(  36,  41,  46 ), false, false, kCodeBack },			// コードブロック
	{ COLORIDX_REGEX1,		RGB(   0,  92, 197 ), true,  false, CLR_INVALID },			// 見出し
	{ COLORIDX_REGEX2,		RGB( 215,  58,  73 ), true,  false, CLR_INVALID },			// 太字
	{ COLORIDX_REGEX3,		RGB( 111,  66, 193 ), false, false, CLR_INVALID },			// 斜体
	{ COLORIDX_REGEX4,		RGB( 106, 115, 125 ), false, false, CLR_INVALID },			// 取り消し線
	{ COLORIDX_REGEX5,		RGB( 199,  37,  78 ), false, false, RGB( 249, 242, 244 ) },	// インラインコード
	{ COLORIDX_REGEX6,		RGB(   3, 102, 214 ), false, true,  CLR_INVALID },			// リンク、画像
	{ COLORIDX_REGEX7,		RGB(  34, 134,  58 ), false, false, CLR_INVALID },			// 引用
	{ COLORIDX_REGEX8,		RGB( 227,  98,   9 ), true,  false, CLR_INVALID },			// リストの記号
	{ COLORIDX_REGEX9,		RGB( 106, 115, 125 ), true,  false, CLR_INVALID },			// 区切り線、表の区切り
	{ COLORIDX_REGEX10,		RGB(   0, 134, 179 ), false, false, CLR_INVALID },			// HTMLのタグとコメント
	{ COLORIDX_KEYWORD1,	RGB( 215,  58,  73 ), false, false, kCodeBack },			// コード: キーワード
	{ COLORIDX_KEYWORD2,	RGB(   3,  47,  98 ), false, false, kCodeBack },			// コード: 文字列
	{ COLORIDX_KEYWORD3,	RGB( 106, 115, 125 ), false, false, kCodeBack },			// コード: コメント
	{ COLORIDX_KEYWORD4,	RGB(   0,  92, 197 ), false, false, kCodeBack },			// コード: 数値と定数
	{ COLORIDX_KEYWORD5,	RGB( 227,  98,   9 ), false, false, kCodeBack },			// コード: 変数とキー
	{ COLORIDX_KEYWORD6,	RGB( 111,  66, 193 ), false, false, kCodeBack },			// コード: 関数名とコマンド名
	{ COLORIDX_KEYWORD7,	RGB(  34, 134,  58 ), false, false, kCodeBack },			// コード: タグ
	{ COLORIDX_KEYWORD8,	RGB(   0, 134, 179 ), false, false, kCodeBack },			// コード: 属性名
	{ COLORIDX_KEYWORD9,	RGB(  34, 134,  58 ), false, false, RGB( 230, 255, 237 ) },	// コード: 差分の追加行
	{ COLORIDX_KEYWORD10,	RGB( 179,  29,  40 ), false, false, RGB( 255, 235, 233 ) },	// コード: 差分の削除行
};

//! 版1(v1.0.0)の色。VS CodeのLight+テーマを参考にしたもの
constexpr SMarkdownColor g_aMarkdownColorsV1[] = {
	{ COLORIDX_COMMENT,		RGB( 163,  21,  21 ), false, false, RGB( 238, 238, 238 ) },
	{ COLORIDX_REGEX1,		RGB( 128,   0,   0 ), true,  false, CLR_INVALID },
	{ COLORIDX_REGEX2,		RGB(   0,   0, 128 ), true,  false, CLR_INVALID },
	{ COLORIDX_REGEX3,		RGB(  38, 127, 153 ), false, false, CLR_INVALID },
	{ COLORIDX_REGEX4,		RGB( 128, 128, 128 ), false, false, CLR_INVALID },
	{ COLORIDX_REGEX5,		RGB( 163,  21,  21 ), false, false, RGB( 238, 238, 238 ) },
	{ COLORIDX_REGEX6,		RGB(   0,   0, 255 ), false, false, CLR_INVALID },
	{ COLORIDX_REGEX7,		RGB(   0, 128,   0 ), false, false, CLR_INVALID },
	{ COLORIDX_REGEX8,		RGB(   4,  81, 165 ), true,  false, CLR_INVALID },
	{ COLORIDX_REGEX9,		RGB( 128, 128, 128 ), true,  false, CLR_INVALID },
	{ COLORIDX_REGEX10,		RGB( 175,   0, 219 ), false, false, CLR_INVALID },
};

void ApplyMarkdownColor(ColorInfo& info, const SMarkdownColor& color)
{
	info.m_bDisp = true;
	info.m_sFontAttr.m_bBoldFont = color.bBold;
	info.m_sFontAttr.m_bUnderLine = color.bUnderLine;
	info.m_sColorAttr.m_cTEXT = color.crText;
	if( color.crBack != CLR_INVALID ){
		info.m_sColorAttr.m_cBACK = color.crBack;
	}
}

//! 指定の色の一覧で、その色番号の色を作る(一覧にない色番号は既定値)
ColorInfo MakeMarkdownColor(std::span<const SMarkdownColor> colors, int nColorIndex)
{
	ColorInfo info;
	GetDefaultColorInfo( &info, nColorIndex );
	for( const auto& color : colors ){
		if( color.eColorIndex == nColorIndex ){
			ApplyMarkdownColor( info, color );
		}
	}
	return info;
}

bool IsSameColor(const ColorInfo& a, const ColorInfo& b) noexcept
{
	return a.m_bDisp == b.m_bDisp
		&& a.m_sFontAttr.m_bBoldFont == b.m_sFontAttr.m_bBoldFont
		&& a.m_sFontAttr.m_bUnderLine == b.m_sFontAttr.m_bUnderLine
		&& a.m_sColorAttr.m_cTEXT == b.m_sColorAttr.m_cTEXT
		&& a.m_sColorAttr.m_cBACK == b.m_sColorAttr.m_cBACK;
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
	pType->m_bMarkdownCodeBlock = true;							// コードブロック(```)を言語ごとに色分けする
	pType->m_ColorInfoArr[COLORIDX_SSTRING].m_bDisp = false;	//シングルクォーテーション文字列を色分け表示しない
	pType->m_ColorInfoArr[COLORIDX_WSTRING].m_bDisp = false;	//ダブルクォーテーション文字列を色分け表示しない

	//色
	for( const auto& color : g_aMarkdownColors ){
		ApplyMarkdownColor( pType->m_ColorInfoArr[color.eColorIndex], color );
	}

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

void UpgradeMarkdownTypeConfig( STypeConfig& type, int nFromVersion )
{
	if( nFromVersion < 2 ){
		// 版1は、コードブロックをブロックコメントで色分けしていた
		for( auto& blockComment : type.m_cBlockComments ){
			const std::wstring_view from = blockComment.getBlockCommentFrom();
			const std::wstring_view to = blockComment.getBlockCommentTo();
			if( (from == L"```" && to == L"```") || (from == L"~~~" && to == L"~~~") ){
				blockComment.SetBlockCommentRule( L"", L"" );
			}
		}
		type.m_bMarkdownCodeBlock = true;

		// 版1の既定のままの色だけを、今の版の色にする
		for( const auto& color : g_aMarkdownColors ){
			auto& info = type.m_ColorInfoArr[color.eColorIndex];
			if( IsSameColor( info, MakeMarkdownColor( g_aMarkdownColorsV1, color.eColorIndex ) ) ){
				static_cast<ColorInfoBase&>( info ) = MakeMarkdownColor( g_aMarkdownColors, color.eColorIndex );
			}
		}
	}
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
