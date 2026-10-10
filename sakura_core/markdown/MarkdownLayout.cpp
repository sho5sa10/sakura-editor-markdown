/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/MarkdownLayout.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>

#include "markdown/MarkdownText.h"

namespace markdown {

namespace {

//! 同じフォント・装飾が続く文字列
struct Atom {
	std::wstring	text;
	FontSpec		font;
	std::uint8_t	style = InlineStyle::None;
	int				width = 0;
	bool			image = false;		//!< 画像(textは使わない)
	int				imageHeight = 0;
	std::wstring	imageKey;
	std::wstring	imageAlt;
};

/*!
	折り返しの単位。トークンの途中では改行しない。
	英単語は1つのトークンになり、日本語は(禁則処理に当たらない限り)1文字ずつのトークンになる。
	装飾が途中で変わっても(例: foo**bar**)、同じトークンに入る。
*/
struct Token {
	std::vector<Atom>	atoms;
	int					width = 0;
	bool				space = false;			//!< 空白だけのトークン(行末にぶら下がる)
	bool				forcedBreak = false;	//!< 強制改行
};

enum class Align : std::uint8_t { Left, Center, Right };

constexpr int kHugeWidth = 1 << 24;	//!< 折り返さずに幅を測るための値

FontSpec ApplyStyle(FontSpec font, std::uint8_t style) noexcept
{
	if (style & InlineStyle::Bold)   font.bold = true;
	if (style & InlineStyle::Italic) font.italic = true;
	if (style & InlineStyle::Code)   font.mono = true;
	if (style & InlineStyle::Link)   font.underline = true;
	if (style & InlineStyle::Strike) font.strike = true;
	return font;
}

constexpr int kHeadingPercent[6] = { 200, 150, 125, 110, 100, 90 };

class Layouter {
public:
	Layouter(ITextMeasurer& measurer, const LayoutSettings& settings)
		: m_measurer(measurer)
		, m_settings(settings)
	{
	}

	LayoutResult Run(const Document& document)
	{
		int y = m_settings.padding;
		const Block* prev = nullptr;
		for (const Block& block : document.blocks) {
			const int top = y + (prev != nullptr ? GapBetween(*prev, block) : 0);
			const int x0 = m_settings.padding
				+ block.quoteDepth * m_settings.quoteIndent
				+ block.listDepth * m_settings.listIndent;
			const int available = (std::max)(1, m_settings.width - m_settings.padding - x0);

			int bottom = top;
			switch (block.kind) {
			case BlockKind::Heading:
			case BlockKind::Paragraph:
				bottom = LayoutTextBlock(block, x0, top, available);
				break;
			case BlockKind::CodeBlock:
				bottom = LayoutCodeBlock(block, x0, top, available);
				break;
			case BlockKind::ThematicBreak:
				bottom = LayoutRule(x0, top, available);
				break;
			case BlockKind::Table:
				bottom = LayoutTable(block, x0, top, available);
				break;
			}
			AddQuoteBars(block, prev, y, top, bottom);
			y = bottom;
			prev = &block;
		}
		m_result.height = y + m_settings.padding;

		const auto byY = [](const auto& a, const auto& b) { return a.y < b.y; };
		std::stable_sort(m_result.texts.begin(), m_result.texts.end(), byY);
		std::stable_sort(m_result.fills.begin(), m_result.fills.end(), byY);
		std::stable_sort(m_result.images.begin(), m_result.images.end(), byY);
		return std::move(m_result);
	}

private:
	struct InlineResult {
		int height = 0;
		int maxLineWidth = 0;
	};

	int LineHeight(const FontSpec& font)
	{
		const int raw = (std::max)(1, m_measurer.LineHeight(font));
		return (std::max)(raw, static_cast<int>(static_cast<long long>(raw) * m_settings.lineSpacingPercent / 100));
	}

	int GapBetween(const Block& prev, const Block& current) const
	{
		if (prev.listDepth > 0 && current.listDepth > 0) {
			return m_settings.listItemGap;
		}
		if (current.kind == BlockKind::Heading) {
			return m_settings.blockGap * 3 / 2;
		}
		return m_settings.blockGap;
	}

	static FontSpec BaseFont(const Block& block) noexcept
	{
		FontSpec font;
		if (block.kind == BlockKind::Heading) {
			const int level = std::clamp(block.level, 1, 6);
			font.sizePercent = kHeadingPercent[level - 1];
			font.bold = true;
		}
		return font;
	}

	void AddQuoteBars(const Block& block, const Block* prev, int previousBottom, int top, int bottom)
	{
		for (int depth = 0; depth < block.quoteDepth; ++depth) {
			// 前のブロックも同じ深さの引用なら、間をつないで1本の線にする
			const bool connected = prev != nullptr && prev->quoteDepth > depth;
			const int barTop = connected ? previousBottom : top;
			FillItem fill;
			fill.x = m_settings.padding + depth * m_settings.quoteIndent;
			fill.y = barTop;
			fill.width = m_settings.quoteBarWidth;
			fill.height = (std::max)(1, bottom - barTop);
			fill.kind = FillKind::QuoteBar;
			m_result.fills.push_back(fill);
		}
	}

	//! リストのマーカーを、本文の左側に描く
	void AddMarker(const Block& block, const FontSpec& font, int x0, int top)
	{
		if (block.marker.empty()) {
			return;
		}
		const int width = m_measurer.Width(font, block.marker);
		const int minX = m_settings.padding + block.quoteDepth * m_settings.quoteIndent;
		TextItem item;
		item.font = font;
		item.text = block.marker;
		item.width = width;
		item.height = m_measurer.LineHeight(font);
		item.x = (std::max)(minX, x0 - m_settings.markerGap - width);
		item.y = top + (LineHeight(font) - item.height) / 2;
		m_result.texts.push_back(std::move(item));
	}

	int LayoutTextBlock(const Block& block, int x0, int top, int available)
	{
		const FontSpec font = BaseFont(block);
		AddMarker(block, font, x0, top);
		const InlineResult laidOut = LayoutInline(block.runs, font, x0, top, available, Align::Left, true);
		return top + laidOut.height;
	}

	int LayoutRule(int x0, int top, int available)
	{
		FillItem fill;
		fill.x = x0;
		fill.y = top + 4;
		fill.width = available;
		fill.height = m_settings.ruleThickness;
		fill.kind = FillKind::Rule;
		m_result.fills.push_back(fill);
		return top + 8 + m_settings.ruleThickness;
	}

	// ---- コードブロック ----

	static std::wstring ExpandTabs(std::wstring_view line, int tabSize)
	{
		std::wstring result;
		int column = 0;
		for (const wchar_t ch : line) {
			if (ch == L'\t') {
				const int spaces = tabSize - (column % tabSize);
				result.append(static_cast<std::size_t>(spaces), L' ');
				column += spaces;
			} else if (ch == L'\r') {
				continue;
			} else {
				result.push_back(ch);
				++column;
			}
		}
		return result;
	}

	int LayoutCodeBlock(const Block& block, int x0, int top, int available)
	{
		FontSpec font;
		font.mono = true;
		font.sizePercent = 95;
		const int lineHeight = LineHeight(font);
		const int rawHeight = m_measurer.LineHeight(font);
		const int pad = m_settings.codePadding;
		const int innerX = x0 + pad;
		const int innerWidth = (std::max)(1, available - 2 * pad);
		const int tabSize = (std::max)(1, m_settings.tabSize);

		AddMarker(block, BaseFont(block), x0, top + pad);

		int y = top + pad;
		std::wstring_view code = block.code;
		std::size_t pos = 0;
		bool last = false;
		while (!last) {
			std::size_t end = code.find(L'\n', pos);
			if (end == std::wstring_view::npos) {
				end = code.size();
				last = true;
			}
			const std::wstring line = ExpandTabs(code.substr(pos, end - pos), tabSize);
			pos = end + 1;

			// 折り返しは文字単位。コードの空白は詰めない
			std::size_t start = 0;
			do {
				std::size_t stop = start;
				int width = 0;
				while (stop < line.size()) {
					std::size_t len = 1;
					const std::uint32_t cp = text::CodePointAt(line, stop, &len);
					(void)cp;
					const int w = m_measurer.Width(font, std::wstring_view(line).substr(stop, len));
					if (width + w > innerWidth && stop > start) {
						break;
					}
					width += w;
					stop += len;
				}
				if (stop > start) {
					TextItem item;
					item.x = innerX;
					item.y = y + (lineHeight - rawHeight) / 2;
					item.width = width;
					item.height = rawHeight;
					item.font = font;
					item.text = line.substr(start, stop - start);
					m_result.texts.push_back(std::move(item));
				}
				y += lineHeight;
				start = stop;
			} while (start < line.size());
		}
		y += pad;

		FillItem fill;
		fill.x = x0;
		fill.y = top;
		fill.width = available;
		fill.height = y - top;
		fill.kind = FillKind::CodeBackground;
		m_result.fills.push_back(fill);
		return y;
	}

	// ---- 表 ----

	/*!
		表の列の幅を決める。
		- 全部が自然な幅(折り返さない幅)で収まるなら、そのまま使う。
		- 収まらないときは、自然な幅が「列数で等分した幅」以下の列はそのまま使い、
		  それより広い列だけが、残りの幅を自然な幅の比率で分け合う。
		  短い列(数量・日付など)が文字の途中で折れないようにするため。
	*/
	std::vector<int> ComputeColumnWidths(const std::vector<int>& natural, const std::vector<int>& minContent,
		int available) const
	{
		const std::size_t count = natural.size();
		long long total = 0;
		for (const int w : natural) total += w;
		if (total <= available) {
			return natural;
		}
		const int fairShare = available / static_cast<int>(count);
		std::vector<int> result(count, 0);
		long long narrowTotal = 0;
		long long wideNaturalTotal = 0;
		for (std::size_t i = 0; i < count; ++i) {
			if (natural[i] <= fairShare) {
				result[i] = natural[i];
				narrowTotal += natural[i];
			} else {
				wideNaturalTotal += natural[i];
			}
		}
		const long long remaining = available - narrowTotal;
		if (wideNaturalTotal == 0 || remaining <= 0) {
			// 全部が狭い列として扱える、または狭い列だけで幅を使い切る。自然な幅の比率で縮める
			for (std::size_t i = 0; i < count; ++i) {
				result[i] = (std::max)(1, static_cast<int>(static_cast<long long>(natural[i]) * available / total));
			}
			return result;
		}
		long long assigned = narrowTotal;
		for (std::size_t i = 0; i < count; ++i) {
			if (natural[i] <= fairShare) continue;
			// 広い列にも、折り返せない最長の語が入る幅は(極端でなければ)確保する
			const int minimum = (std::min)(natural[i],
				(std::max)(m_settings.tableMinColumn, (std::min)(minContent[i], available / 2)));
			result[i] = (std::max)(minimum, static_cast<int>(remaining * natural[i] / wideNaturalTotal));
			assigned += result[i];
		}
		if (assigned > available) {
			// 最小幅を確保した結果、はみ出した。広い列だけを縮めて収める
			const long long overflow = assigned - available;
			long long wideTotal = assigned - narrowTotal;
			for (std::size_t i = 0; i < count && wideTotal > 0; ++i) {
				if (natural[i] <= fairShare) continue;
				const long long cut = overflow * result[i] / wideTotal;
				result[i] = (std::max)(1, static_cast<int>(result[i] - cut));
			}
		}
		return result;
	}

	static Align ToAlign(TableAlign align) noexcept
	{
		switch (align) {
		case TableAlign::Center:	return Align::Center;
		case TableAlign::Right:		return Align::Right;
		default:					return Align::Left;
		}
	}

	int LayoutTable(const Block& block, int x0, int top, int available)
	{
		std::size_t columns = 0;
		for (const TableRow& row : block.rows) {
			columns = (std::max)(columns, row.cells.size());
		}
		if (columns == 0) {
			return top;
		}
		const int padX = m_settings.tableCellPadX;
		const int padY = m_settings.tableCellPadY;
		const int line = m_settings.ruleThickness;

		FontSpec bodyFont;
		FontSpec headerFont;
		headerFont.bold = true;

		std::vector<int> natural(columns, 2 * padX + line + 1);
		std::vector<int> minContent(columns, 2 * padX + line + 1);
		for (const TableRow& row : block.rows) {
			for (std::size_t c = 0; c < row.cells.size(); ++c) {
				const FontSpec& font = row.header ? headerFont : bodyFont;
				const InlineResult size = LayoutInline(row.cells[c].runs, font, 0, 0, kHugeWidth, Align::Left, false);
				natural[c] = (std::max)(natural[c], size.maxLineWidth + 2 * padX + line);
				minContent[c] = (std::max)(minContent[c], MinContentWidth(row.cells[c].runs, font) + 2 * padX + line);
			}
		}
		const std::vector<int> widths = ComputeColumnWidths(natural, minContent, (std::max)(1, available - line));
		int tableWidth = 0;
		for (const int w : widths) tableWidth += w;

		std::vector<int> rowTops;
		int y = top + line;
		for (const TableRow& row : block.rows) {
			rowTops.push_back(y - line);
			const FontSpec& font = row.header ? headerFont : bodyFont;
			int rowHeight = LineHeight(font);
			int cx = x0 + line;
			for (std::size_t c = 0; c < row.cells.size(); ++c) {
				const int contentWidth = (std::max)(1, widths[c] - 2 * padX - line);
				const InlineResult size = LayoutInline(row.cells[c].runs, font,
					cx + padX, y + padY, contentWidth, ToAlign(row.cells[c].align), true);
				rowHeight = (std::max)(rowHeight, size.height);
				cx += widths[c];
			}
			rowHeight += 2 * padY;
			if (row.header) {
				FillItem fill;
				fill.x = x0 + line;
				fill.y = y - line;
				fill.width = tableWidth;
				fill.height = rowHeight + line;
				fill.kind = FillKind::TableHeaderBackground;
				m_result.fills.push_back(fill);
			}
			y += rowHeight + line;
		}
		rowTops.push_back(y - line);

		const int bottom = y;
		for (const int ry : rowTops) {
			FillItem fill;
			fill.x = x0;
			fill.y = ry;
			fill.width = tableWidth + line;
			fill.height = line;
			fill.kind = FillKind::TableBorder;
			m_result.fills.push_back(fill);
		}
		int vx = x0;
		for (std::size_t c = 0; c <= widths.size(); ++c) {
			FillItem fill;
			fill.x = vx;
			fill.y = top;
			fill.width = line;
			fill.height = bottom - top;
			fill.kind = FillKind::TableBorder;
			m_result.fills.push_back(fill);
			if (c < widths.size()) vx += widths[c];
		}
		return bottom;
	}

	// ---- インライン ----

	std::vector<Token> Tokenize(const InlineRuns& runs, const FontSpec& base, int imageWidthLimit)
	{
		std::vector<Token> tokens;
		std::uint32_t prev = 0;
		bool havePrev = false;
		bool prevSpace = false;

		const auto addText = [&](std::wstring_view source, const FontSpec& font, std::uint8_t style) {
			std::size_t pos = 0;
			while (pos < source.size()) {
				std::size_t len = 1;
				std::uint32_t cp = text::CodePointAt(source, pos, &len);
				std::wstring_view piece = source.substr(pos, len);
				pos += len;
				if (cp == 0x09U || cp == 0x0AU || cp == 0x0DU) {
					cp = 0x20U;
					piece = L" ";
				} else if (cp < 0x20U) {
					continue;	// 制御文字は表示しない
				}
				const bool space = text::IsBreakSpace(cp);
				// 空白は単独のトークンにする(行末にぶら下げたり、折り返しで捨てたりするため)
				const bool startNew = !havePrev || tokens.empty() || prevSpace != space
					|| (!space && text::CanBreakBetween(prev, cp));
				if (startNew) {
					Token token;
					token.space = space;
					token.atoms.push_back(Atom{ std::wstring(piece), font, style, 0 });
					tokens.push_back(std::move(token));
				} else {
					Token& token = tokens.back();
					if (token.atoms.back().font == font && token.atoms.back().style == style) {
						token.atoms.back().text.append(piece);
					} else {
						token.atoms.push_back(Atom{ std::wstring(piece), font, style, 0 });
					}
				}
				prev = cp;
				prevSpace = space;
				havePrev = true;
			}
		};

		for (const InlineRun& run : runs) {
			switch (run.kind) {
			case InlineKind::Text:
				addText(run.text, ApplyStyle(base, run.style), run.style);
				break;
			case InlineKind::LineBreak: {
				Token token;
				token.forcedBreak = true;
				tokens.push_back(std::move(token));
				havePrev = false;
				break;
			}
			case InlineKind::Image: {
				const ImageSize size = m_measurer.MeasureImage(run.target);
				if (size.valid && size.width > 0 && size.height > 0) {
					// 表示できる画像は、1つのトークン。幅に収まらなければ、縦横比を保って縮小する
					int width = size.width;
					int height = size.height;
					const int limit = (std::max)(1, imageWidthLimit);
					if (width > limit) {
						height = (std::max)(1, static_cast<int>(static_cast<long long>(height) * limit / width));
						width = limit;
					}
					Atom atom;
					atom.font = base;
					atom.style = run.style;
					atom.width = width;
					atom.image = true;
					atom.imageHeight = height;
					atom.imageKey = size.key.empty() ? run.target : size.key;
					atom.imageAlt = run.text;
					Token token;
					token.atoms.push_back(std::move(atom));
					tokens.push_back(std::move(token));
					havePrev = false;	// 画像の前後では、改行してよい
					break;
				}
				// 表示できないときは、代替テキストを角括弧で囲んで見せる
				FontSpec font = ApplyStyle(base, run.style);
				font.italic = true;
				const std::wstring label = L"[" + (run.text.empty() ? std::wstring(L"image") : run.text) + L"]";
				addText(label, font, run.style);
				break;
			}
			}
		}
		for (Token& token : tokens) {
			for (Atom& atom : token.atoms) {
				if (!atom.image) {
					atom.width = m_measurer.Width(atom.font, atom.text);
				}
				token.width += atom.width;
			}
		}
		return tokens;
	}

	//! 折り返せない最も長い語の幅(表の列の最小幅の計算用)
	int MinContentWidth(const InlineRuns& runs, const FontSpec& base)
	{
		int width = 0;
		for (const Token& token : Tokenize(runs, base, m_settings.tableMinColumn)) {
			if (!token.space && !token.forcedBreak) {
				width = (std::max)(width, token.width);
			}
		}
		return width;
	}

	//! widthより長いトークンを、文字単位で幅に収まる断片に分ける
	void SplitToken(const Token& token, int width, std::deque<Token>& store, std::vector<const Token*>& out)
	{
		Token fragment;
		const auto finish = [&] {
			if (fragment.atoms.empty()) return;
			store.push_back(std::move(fragment));
			out.push_back(&store.back());
			fragment = Token{};
		};
		for (const Atom& atom : token.atoms) {
			if (atom.image) {
				finish();
				fragment.atoms.push_back(atom);
				fragment.width = atom.width;
				finish();
				continue;
			}
			std::size_t pos = 0;
			while (pos < atom.text.size()) {
				std::size_t len = 1;
				(void)text::CodePointAt(atom.text, pos, &len);
				const std::wstring_view piece = std::wstring_view(atom.text).substr(pos, len);
				const int w = m_measurer.Width(atom.font, piece);
				if (fragment.width + w > width && !fragment.atoms.empty()) {
					finish();
				}
				if (fragment.atoms.empty() || !(fragment.atoms.back().font == atom.font
					&& fragment.atoms.back().style == atom.style)) {
					fragment.atoms.push_back(Atom{ std::wstring(), atom.font, atom.style, 0 });
				}
				fragment.atoms.back().text.append(piece);
				fragment.atoms.back().width += w;
				fragment.width += w;
				pos += len;
			}
		}
		finish();
	}

	/*!
		インライン要素を折り返して配置する。
		@param[in] emit	falseなら高さと幅だけを計算し、出力しない
	*/
	InlineResult LayoutInline(const InlineRuns& runs, const FontSpec& base, int x, int y, int width,
		Align align, bool emit)
	{
		width = (std::max)(1, width);
		const std::vector<Token> tokens = Tokenize(runs, base, width);

		struct Line {
			std::vector<const Token*>	tokens;
		};
		std::deque<Token> fragments;
		std::vector<Line> lines;
		Line current;
		int currentWidth = 0;		// 行末の空白を含む幅
		bool hasContent = false;	// 空白以外のトークンがある

		const auto flush = [&] {
			while (!current.tokens.empty() && current.tokens.back()->space) {
				current.tokens.pop_back();	// 行末の空白は捨てる
			}
			lines.push_back(std::move(current));
			current = Line{};
			currentWidth = 0;
			hasContent = false;
		};

		for (const Token& token : tokens) {
			if (token.forcedBreak) {
				flush();
				continue;
			}
			if (token.space) {
				if (hasContent) {
					current.tokens.push_back(&token);
					currentWidth += token.width;
				}
				continue;
			}
			if (hasContent && currentWidth + token.width > width) {
				flush();
			}
			if (token.width > width) {
				std::vector<const Token*> pieces;
				SplitToken(token, width, fragments, pieces);
				for (const Token* piece : pieces) {
					if (hasContent) flush();
					current.tokens.push_back(piece);
					currentWidth = piece->width;
					hasContent = true;
				}
				continue;
			}
			current.tokens.push_back(&token);
			currentWidth += token.width;
			hasContent = true;
		}
		if (!current.tokens.empty() || lines.empty()) {
			flush();
		}

		InlineResult result;
		int lineTop = y;
		for (const Line& line : lines) {
			int lineWidth = 0;
			// どの行も基準のフォントの高さは確保する(表の同じ行のセルで、文字の高さがそろうように)
			int lineHeight = LineHeight(base);
			int rawHeight = m_measurer.LineHeight(base);
			for (const Token* token : line.tokens) {
				lineWidth += token->width;
				for (const Atom& atom : token->atoms) {
					if (atom.image) {
						lineHeight = (std::max)(lineHeight, atom.imageHeight);
						rawHeight = (std::max)(rawHeight, atom.imageHeight);
						continue;
					}
					lineHeight = (std::max)(lineHeight, LineHeight(atom.font));
					rawHeight = (std::max)(rawHeight, m_measurer.LineHeight(atom.font));
				}
			}
			result.maxLineWidth = (std::max)(result.maxLineWidth, lineWidth);

			if (emit) {
				int cx = x;
				if (align == Align::Center) {
					cx += (std::max)(0, (width - lineWidth) / 2);
				} else if (align == Align::Right) {
					cx += (std::max)(0, width - lineWidth);
				}
				// 大きさの違う文字は、下端をそろえる
				const int glyphBottom = lineTop + (lineHeight - rawHeight) / 2 + rawHeight;
				const std::size_t firstItem = m_result.texts.size();
				for (const Token* token : line.tokens) {
					for (const Atom& atom : token->atoms) {
						if (atom.image) {
							ImageItem item;
							item.x = cx;
							item.y = glyphBottom - atom.imageHeight;
							item.width = atom.width;
							item.height = atom.imageHeight;
							item.key = atom.imageKey;
							item.alt = atom.imageAlt;
							m_result.images.push_back(std::move(item));
							cx += atom.width;
							continue;
						}
						const int height = m_measurer.LineHeight(atom.font);
						const int ty = glyphBottom - height;
						if (m_result.texts.size() > firstItem) {
							TextItem& last = m_result.texts.back();
							if (last.font == atom.font && last.style == atom.style
								&& last.x + last.width == cx && last.y == ty) {
								last.text += atom.text;
								last.width += atom.width;
								cx += atom.width;
								continue;
							}
						}
						TextItem item;
						item.x = cx;
						item.y = ty;
						item.width = atom.width;
						item.height = height;
						item.font = atom.font;
						item.style = atom.style;
						item.text = atom.text;
						m_result.texts.push_back(std::move(item));
						cx += atom.width;
					}
				}
				for (std::size_t i = firstItem; i < m_result.texts.size(); ++i) {
					const TextItem& item = m_result.texts[i];
					if (item.style & InlineStyle::Code) {
						FillItem fill;
						fill.x = item.x;
						fill.y = item.y;
						fill.width = item.width;
						fill.height = item.height;
						fill.kind = FillKind::InlineCodeBackground;
						m_result.fills.push_back(fill);
					}
				}
			}
			lineTop += lineHeight;
		}
		result.height = lineTop - y;
		return result;
	}

	ITextMeasurer&			m_measurer;
	const LayoutSettings&	m_settings;
	LayoutResult			m_result;
};

} // namespace

LayoutResult BuildLayout(const Document& document, ITextMeasurer& measurer, const LayoutSettings& settings)
{
	Layouter layouter(measurer, settings);
	return layouter.Run(document);
}

} // namespace markdown
