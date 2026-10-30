/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CCOLOR_MARKDOWNFENCE_5A1C8E39_7B2D_4E64_9F0A_3C6D2B8E1F47_H_
#define SAKURA_CCOLOR_MARKDOWNFENCE_5A1C8E39_7B2D_4E64_9F0A_3C6D2B8E1F47_H_
#pragma once

#include <vector>

#include "view/colors/CColorStrategy.h"
#include "markdown/MarkdownCodeTokenizer.h"

/*!
	@brief Markdownのフェンスで囲んだコードブロック(```や~~~)の色分け

	コードブロック全体を1つの範囲として扱い(色は「コメント」)、その中の字句には、
	フェンスの情報文字列(```python の python)の言語に合わせて、強調キーワード1～10の色を付ける。
	- 強調キーワード1: キーワード		2: 文字列		3: コメント		4: 数値と定数
	- 強調キーワード5: 変数とキー		6: 関数名とコマンド名
	- 強調キーワード7: タグ			8: 属性名		9: 差分の追加行	10: 差分の削除行

	フェンスは行頭(前の空白は何桁でもよい)に、同じ文字(`か~)を3個以上並べる。
	閉じのフェンスは、開いたときと同じ文字を同じ数以上並べ、後ろに空白しかない行。
	範囲を識別する色はCOLORIDX_MARKDOWN_FENCEで、描画の色(GetDrawColor)だけを字句ごとに変える。
*/
class CColor_MarkdownFence final : public CColorStrategy{
public:
	EColorIndexType GetStrategyColor() const override{ return COLORIDX_MARKDOWN_FENCE; }
	EColorIndexType GetDrawColor() const override{ return m_eDrawColor; }
	CLayoutColorInfo* GetStrategyColorInfo() const override;
	void InitStrategyStatus() override;
	void SetStrategyColorInfo(const CLayoutColorInfo* colorInfo) override;
	bool BeginColor(const CStringRef& cStr, int nPos) override;
	bool EndColor(const CStringRef& cStr, int nPos) override;
	bool UpdateDrawColor(const CStringRef& cStr, int nPos) override;
	bool Disp() const override;

	//! 字句の種類を色に変える
	static EColorIndexType ToColorIndex(markdown::ECodeTokenKind eKind) noexcept;

private:
	bool IsClosingFence(const CStringRef& cStr) const;

	wchar_t						m_chFence = L'\0';			//!< 開いたフェンスの文字
	int							m_nFenceLen = 0;			//!< 開いたフェンスの長さ
	markdown::ECodeLanguage		m_eLanguage = markdown::ECodeLanguage::Generic;

	int							m_nCOMMENTEND = 0;			//!< 範囲の終わり。0ならこの行をまだ調べていない
	bool						m_bFenceLine = false;		//!< この行はフェンスの行(字句に分けない)
	bool						m_bTokenized = false;		//!< この行を字句に分けたか
	std::vector<markdown::CodeToken>	m_tokens;			//!< この行の字句
	size_t						m_nTokenIndex = 0;			//!< 今の位置より後ろにある最初の字句
	int							m_nLastPos = -1;			//!< 前回調べた位置
	EColorIndexType				m_eDrawColor = COLORIDX_MARKDOWN_FENCE;
};

#endif /* SAKURA_CCOLOR_MARKDOWNFENCE_5A1C8E39_7B2D_4E64_9F0A_3C6D2B8E1F47_H_ */
