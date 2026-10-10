/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/Md4cParser.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "markdown/MarkdownText.h"

// md4cのUTF-16版を使う。md4c.hを取り込むのはこのファイルだけにすること。
// (ライブラリ側もMD4C_USE_UTF16を付けてビルドされている必要がある。両者の定義が
//  食い違うとMD_CHARの型が合わず、下のstatic_assertかコンパイルエラーで検出される)
#ifndef MD4C_USE_UTF16
#define MD4C_USE_UTF16
#endif
#include <md4c.h>

static_assert(sizeof(MD_CHAR) == sizeof(wchar_t), "md4c must be built with MD4C_USE_UTF16");

namespace markdown {

namespace {

//! 名前付き文字参照。md4cは一覧を持たないので、よく使うものだけ展開する
struct NamedEntity {
	const wchar_t*	name;
	std::uint32_t	codePoint;
};

constexpr NamedEntity kNamedEntities[] = {
	{ L"amp", 0x26U },    { L"lt", 0x3CU },      { L"gt", 0x3EU },      { L"quot", 0x22U },
	{ L"apos", 0x27U },   { L"nbsp", 0xA0U },    { L"copy", 0xA9U },    { L"reg", 0xAEU },
	{ L"trade", 0x2122U },{ L"hellip", 0x2026U },{ L"mdash", 0x2014U }, { L"ndash", 0x2013U },
	{ L"laquo", 0xABU },  { L"raquo", 0xBBU },   { L"yen", 0xA5U },     { L"euro", 0x20ACU },
	{ L"pound", 0xA3U },  { L"cent", 0xA2U },    { L"sect", 0xA7U },    { L"deg", 0xB0U },
	{ L"plusmn", 0xB1U }, { L"times", 0xD7U },   { L"divide", 0xF7U },  { L"middot", 0xB7U },
	{ L"bull", 0x2022U }, { L"larr", 0x2190U },  { L"rarr", 0x2192U },  { L"uarr", 0x2191U },
	{ L"darr", 0x2193U }, { L"hearts", 0x2665U },
};

/*!
	文字参照("&amp;"、"&#35;"、"&#x23;")を展開する。
	@retval true	展開した(不正な数値参照はU+FFFDにした)
	@retval false	知らない名前。呼び出し側で元の文字列のまま扱うこと
*/
bool DecodeEntity(std::wstring_view entity, std::wstring& out)
{
	if (entity.size() < 3 || entity.front() != L'&' || entity.back() != L';') {
		return false;
	}
	const std::wstring_view body = entity.substr(1, entity.size() - 2);
	if (body.front() == L'#') {
		std::wstring_view digits = body.substr(1);
		unsigned base = 10;
		if (!digits.empty() && (digits.front() == L'x' || digits.front() == L'X')) {
			base = 16;
			digits.remove_prefix(1);
		}
		std::uint32_t value = 0;
		bool valid = !digits.empty() && digits.size() <= 8;
		for (const wchar_t ch : digits) {
			unsigned digit = 0;
			if (L'0' <= ch && ch <= L'9') {
				digit = static_cast<unsigned>(ch - L'0');
			} else if (base == 16 && L'a' <= ch && ch <= L'f') {
				digit = static_cast<unsigned>(ch - L'a') + 10U;
			} else if (base == 16 && L'A' <= ch && ch <= L'F') {
				digit = static_cast<unsigned>(ch - L'A') + 10U;
			} else {
				valid = false;
				break;
			}
			value = value * base + digit;
		}
		if (!valid) {
			return false;
		}
		text::AppendCodePoint(out, value);	// 不正な値はU+FFFDになる
		return true;
	}
	for (const auto& named : kNamedEntities) {
		if (body == named.name) {
			text::AppendCodePoint(out, named.codePoint);
			return true;
		}
	}
	return false;
}

//! md4cの属性(リンク先など)を文字列にする。文字参照は展開する
std::wstring DecodeAttribute(const MD_ATTRIBUTE& attribute)
{
	std::wstring result;
	if (attribute.text == nullptr || attribute.size == 0) {
		return result;
	}
	for (std::size_t i = 0; attribute.substr_offsets[i] < attribute.size; ++i) {
		const std::wstring_view part(
			attribute.text + attribute.substr_offsets[i],
			attribute.substr_offsets[i + 1] - attribute.substr_offsets[i]);
		switch (attribute.substr_types[i]) {
		case MD_TEXT_ENTITY:
			if (!DecodeEntity(part, result)) {
				text::AppendSanitized(result, part);
			}
			break;
		case MD_TEXT_NULLCHAR:
			text::AppendCodePoint(result, text::kReplacementChar);
			break;
		default:
			text::AppendSanitized(result, part);
			break;
		}
	}
	return result;
}

//! 文字列の最後のコードポイントを返す。空ならfalse
bool LastCodePoint(std::wstring_view s, std::uint32_t* cp)
{
	if (s.empty()) {
		return false;
	}
	std::size_t start = s.size() - 1;
	if (start > 0 && text::IsLowSurrogate(static_cast<std::uint16_t>(s[start]))
		&& text::IsHighSurrogate(static_cast<std::uint16_t>(s[start - 1]))) {
		--start;
	}
	std::size_t len = 1;
	*cp = text::CodePointAt(s, start, &len);
	return true;
}

//! MD_PARSERの状態を中間モデルへ変換する
class Builder {
public:
	Builder(const ParseOptions& options, Document& document)
		: m_options(options)
		, m_document(document)
	{
	}

	//! 呼び出し側から中止が要求されているか。毎回は問い合わせず、一定数のイベントごとに確認する
	bool PollCancel()
	{
		if (!m_options.shouldCancel) return false;
		if ((++m_events & (kCancelPollInterval - 1U)) != 0U) return false;
		if (m_options.shouldCancel()) {
			m_document.cancelled = true;
			m_stop = true;
			return true;
		}
		return false;
	}

	bool IsStopped() const noexcept { return m_stop; }
	void Fail() noexcept { m_stop = true; m_failed = true; }
	bool HasFailed() const noexcept { return m_failed; }

	int EnterBlock(MD_BLOCKTYPE type, void* detail)
	{
		if (m_stop || PollCancel()) return 1;
		FlushImplicitLeaf();
		switch (type) {
		case MD_BLOCK_QUOTE:
			++m_quoteDepth;
			break;
		case MD_BLOCK_UL:
			FlushPendingMarker();
			m_lists.push_back({ false, 0, L'\0' });
			break;
		case MD_BLOCK_OL: {
			FlushPendingMarker();
			const auto* ol = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
			m_lists.push_back({ true, ol->start, static_cast<wchar_t>(ol->mark_delimiter) });
			break;
		}
		case MD_BLOCK_LI:
			BeginListItem(static_cast<const MD_BLOCK_LI_DETAIL*>(detail));
			break;
		case MD_BLOCK_HR: {
			Block block = NewBlock(BlockKind::ThematicBreak);
			Emit(std::move(block));
			break;
		}
		case MD_BLOCK_H: {
			const auto* h = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
			BeginLeaf(BlockKind::Heading);
			m_block.level = static_cast<int>(std::clamp(h->level, 1U, 6U));
			break;
		}
		case MD_BLOCK_P:
			BeginLeaf(BlockKind::Paragraph);
			break;
		case MD_BLOCK_CODE: {
			const auto* code = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
			BeginLeaf(BlockKind::CodeBlock);
			m_block.language = DecodeAttribute(code->lang);
			break;
		}
		case MD_BLOCK_HTML:
			// NOHTMLBLOCKSを指定しているので通常は来ない。来た場合は文字列のまま見せる
			BeginLeaf(BlockKind::CodeBlock);
			break;
		case MD_BLOCK_TABLE:
			m_table = NewBlock(BlockKind::Table);
			m_inTable = true;
			break;
		case MD_BLOCK_THEAD:
			m_headSection = true;
			break;
		case MD_BLOCK_TBODY:
			m_headSection = false;
			break;
		case MD_BLOCK_TR:
			m_row = TableRow{};
			m_row.header = m_headSection;
			break;
		case MD_BLOCK_TH:
		case MD_BLOCK_TD: {
			const auto* td = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
			m_cell = TableCell{};
			m_cell.align = ToAlign(td->align);
			m_runs = &m_cell.runs;
			break;
		}
		default:
			break;
		}
		return m_stop ? 1 : 0;
	}

	int LeaveBlock(MD_BLOCKTYPE type)
	{
		if (m_stop || PollCancel()) return 1;
		FlushImplicitLeaf();
		switch (type) {
		case MD_BLOCK_QUOTE:
			m_quoteDepth = std::max(0, m_quoteDepth - 1);
			break;
		case MD_BLOCK_UL:
		case MD_BLOCK_OL:
			if (!m_lists.empty()) m_lists.pop_back();
			break;
		case MD_BLOCK_LI:
			FlushPendingMarker();	// 本文のない項目
			break;
		case MD_BLOCK_H:
		case MD_BLOCK_P:
		case MD_BLOCK_CODE:
		case MD_BLOCK_HTML:
			EmitLeaf();
			break;
		case MD_BLOCK_TABLE:
			m_inTable = false;
			Emit(std::move(m_table));
			m_table = Block{};
			break;
		case MD_BLOCK_TR:
			m_table.rows.push_back(std::move(m_row));
			m_row = TableRow{};
			break;
		case MD_BLOCK_TH:
		case MD_BLOCK_TD:
			m_row.cells.push_back(std::move(m_cell));
			m_cell = TableCell{};
			m_runs = nullptr;
			break;
		default:
			break;
		}
		return m_stop ? 1 : 0;
	}

	int EnterSpan(MD_SPANTYPE type, void* detail)
	{
		if (m_stop || PollCancel()) return 1;
		EnsureLeaf();
		switch (type) {
		case MD_SPAN_EM:
			++m_italic;
			break;
		case MD_SPAN_STRONG:
			++m_bold;
			break;
		case MD_SPAN_DEL:
			++m_strike;
			break;
		case MD_SPAN_CODE:
			++m_code;
			break;
		case MD_SPAN_A:
			if (m_link++ == 0) {
				m_linkTarget = DecodeAttribute(static_cast<const MD_SPAN_A_DETAIL*>(detail)->href);
			}
			break;
		case MD_SPAN_IMG:
			if (m_image++ == 0) {
				m_imageSource = DecodeAttribute(static_cast<const MD_SPAN_IMG_DETAIL*>(detail)->src);
				m_imageAlt.clear();
			}
			break;
		default:
			break;
		}
		return 0;
	}

	int LeaveSpan(MD_SPANTYPE type)
	{
		if (m_stop || PollCancel()) return 1;
		switch (type) {
		case MD_SPAN_EM:
			m_italic = std::max(0, m_italic - 1);
			break;
		case MD_SPAN_STRONG:
			m_bold = std::max(0, m_bold - 1);
			break;
		case MD_SPAN_DEL:
			m_strike = std::max(0, m_strike - 1);
			break;
		case MD_SPAN_CODE:
			m_code = std::max(0, m_code - 1);
			break;
		case MD_SPAN_A:
			if (m_link > 0 && --m_link == 0) {
				m_linkTarget.clear();
			}
			break;
		case MD_SPAN_IMG:
			if (m_image > 0 && --m_image == 0 && m_runs != nullptr) {
				InlineRun run;
				run.kind = InlineKind::Image;
				run.style = static_cast<std::uint8_t>(CurrentStyle() & InlineStyle::Link);
				run.text = std::move(m_imageAlt);
				run.target = std::move(m_imageSource);
				m_runs->push_back(std::move(run));
				m_imageAlt.clear();
				m_imageSource.clear();
			}
			break;
		default:
			break;
		}
		return 0;
	}

	int Text(MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size)
	{
		if (m_stop || PollCancel()) return 1;
		const std::wstring_view view(data != nullptr ? data : L"", data != nullptr ? size : 0U);
		if (m_image > 0) {
			// 画像の代替テキスト
			AppendToAlt(type, view);
			return 0;
		}
		EnsureLeaf();
		if (m_block.kind == BlockKind::CodeBlock && m_runs == &m_block.runs) {
			AppendToCode(type, view);
			return 0;
		}
		switch (type) {
		case MD_TEXT_BR:
			m_pendingSoftBreak = false;
			if (m_runs != nullptr) {
				InlineRun run;
				run.kind = InlineKind::LineBreak;
				m_runs->push_back(std::move(run));
			}
			break;
		case MD_TEXT_SOFTBR:
			m_pendingSoftBreak = true;
			break;
		case MD_TEXT_NULLCHAR:
			AppendText(std::wstring(1, static_cast<wchar_t>(text::kReplacementChar)));
			break;
		case MD_TEXT_ENTITY: {
			std::wstring decoded;
			if (DecodeEntity(view, decoded)) {
				AppendText(decoded);
			} else {
				AppendText(std::wstring(view));
			}
			break;
		}
		default:
			{
				std::wstring clean;
				clean.reserve(view.size());
				text::AppendSanitized(clean, view);
				AppendText(clean);
			}
			break;
		}
		return 0;
	}

	//! 解析の終了時に呼ぶ。閉じられていない暗黙のブロックを出力する
	void Finish()
	{
		FlushImplicitLeaf();
	}

private:
	struct ListState {
		bool			ordered;
		unsigned		next;		//!< 次の項目の番号(番号付きリストのみ)
		wchar_t			delimiter;	//!< '.' または ')'
	};

	static TableAlign ToAlign(MD_ALIGN align) noexcept
	{
		switch (align) {
		case MD_ALIGN_LEFT:		return TableAlign::Left;
		case MD_ALIGN_CENTER:	return TableAlign::Center;
		case MD_ALIGN_RIGHT:	return TableAlign::Right;
		default:				return TableAlign::Default;
		}
	}

	std::uint8_t CurrentStyle() const noexcept
	{
		std::uint8_t style = InlineStyle::None;
		if (m_bold > 0)		style |= InlineStyle::Bold;
		if (m_italic > 0)	style |= InlineStyle::Italic;
		if (m_strike > 0)	style |= InlineStyle::Strike;
		if (m_code > 0)		style |= InlineStyle::Code;
		if (m_link > 0)		style |= InlineStyle::Link;
		return style;
	}

	Block NewBlock(BlockKind kind)
	{
		Block block;
		block.kind = kind;
		block.quoteDepth = m_quoteDepth;
		block.listDepth = static_cast<int>(m_lists.size());
		// リスト項目の最初のブロックにマーカーを持たせる
		block.marker = std::move(m_pendingMarker);
		m_pendingMarker.clear();
		return block;
	}

	void BeginListItem(const MD_BLOCK_LI_DETAIL* detail)
	{
		FlushPendingMarker();
		std::wstring marker;
		if (detail != nullptr && detail->is_task) {
			marker = (detail->task_mark == L'x' || detail->task_mark == L'X') ? L"[x]" : L"[ ]";
		} else if (!m_lists.empty() && m_lists.back().ordered) {
			ListState& list = m_lists.back();
			marker = std::to_wstring(list.next++);
			marker.push_back(list.delimiter == L')' ? L')' : L'.');
		} else {
			marker = L"•";
		}
		m_pendingMarker = std::move(marker);
	}

	//! 本文のない項目や、直接ネストされたリストの親項目のマーカーだけを出力する
	void FlushPendingMarker()
	{
		if (m_pendingMarker.empty()) return;
		Block block = NewBlock(BlockKind::Paragraph);
		Emit(std::move(block));
	}

	void BeginLeaf(BlockKind kind)
	{
		m_block = NewBlock(kind);
		m_runs = &m_block.runs;
		m_implicitLeaf = false;
		m_pendingSoftBreak = false;
	}

	//! タイトなリストなど、段落のブロックが来ずに文字列が来た場合の対応
	void EnsureLeaf()
	{
		if (m_runs == nullptr) {
			BeginLeaf(BlockKind::Paragraph);
			m_implicitLeaf = true;
		}
	}

	void FlushImplicitLeaf()
	{
		if (m_implicitLeaf) {
			EmitLeaf();
		}
	}

	void EmitLeaf()
	{
		m_implicitLeaf = false;
		if (m_runs != &m_block.runs) {
			return;
		}
		m_runs = nullptr;
		Block block = std::move(m_block);
		m_block = Block{};
		m_pendingSoftBreak = false;
		if (block.kind == BlockKind::CodeBlock) {
			if (!block.code.empty() && block.code.back() == L'\n') {
				block.code.pop_back();
			}
		} else if (block.runs.empty() && block.marker.empty()) {
			return;	// 空の段落・見出しは出力しない
		}
		Emit(std::move(block));
	}

	void Emit(Block&& block)
	{
		if (m_document.blocks.size() >= m_options.maxBlocks) {
			m_document.truncated = true;
			m_stop = true;
			return;
		}
		m_document.blocks.push_back(std::move(block));
	}

	void AppendToAlt(MD_TEXTTYPE type, std::wstring_view view)
	{
		switch (type) {
		case MD_TEXT_SOFTBR:
		case MD_TEXT_BR:
			m_imageAlt.push_back(L' ');
			break;
		case MD_TEXT_NULLCHAR:
			text::AppendCodePoint(m_imageAlt, text::kReplacementChar);
			break;
		case MD_TEXT_ENTITY:
			if (!DecodeEntity(view, m_imageAlt)) text::AppendSanitized(m_imageAlt, view);
			break;
		default:
			text::AppendSanitized(m_imageAlt, view);
			break;
		}
	}

	void AppendToCode(MD_TEXTTYPE type, std::wstring_view view)
	{
		switch (type) {
		case MD_TEXT_NULLCHAR:
			text::AppendCodePoint(m_block.code, text::kReplacementChar);
			break;
		case MD_TEXT_BR:
		case MD_TEXT_SOFTBR:
			m_block.code.push_back(L'\n');
			break;
		case MD_TEXT_ENTITY:
			// コードブロックの中の文字参照は展開しない(md4cもMD_TEXT_CODEで渡してくる)
			text::AppendSanitized(m_block.code, view);
			break;
		default:
			text::AppendSanitized(m_block.code, view);
			break;
		}
	}

	/*!
		インラインの文字列を追加する。
		ソース上の改行(ソフトブレーク)は、前後のどちらかが東アジアの文字なら何も入れず、
		どちらも英数字なら空白1つにする。日本語の文書を途中で改行して書いても、
		表示で余計な空白が入らないようにするため。
	*/
	void AppendText(const std::wstring& content)
	{
		if (content.empty() || m_runs == nullptr) {
			return;
		}
		InlineRuns& runs = *m_runs;
		std::wstring separator;
		if (m_pendingSoftBreak) {
			m_pendingSoftBreak = false;
			if (!runs.empty()) {
				const InlineRun& last = runs.back();
				std::uint32_t prev = 0;
				if (last.kind == InlineKind::Text && LastCodePoint(last.text, &prev)) {
					std::size_t len = 1;
					const std::uint32_t next = text::CodePointAt(content, 0, &len);
					if (!text::IsWide(prev) && !text::IsWide(next) && !text::IsBreakSpace(prev)) {
						separator = L" ";
					}
				} else if (last.kind == InlineKind::Image) {
					separator = L" ";
				}
			}
		}
		const std::uint8_t style = CurrentStyle();
		const std::wstring& target = (m_link > 0) ? m_linkTarget : kNoTarget;
		if (!runs.empty() && runs.back().kind == InlineKind::Text
			&& runs.back().style == style && runs.back().target == target) {
			runs.back().text += separator;
			runs.back().text += content;
			return;
		}
		InlineRun run;
		run.kind = InlineKind::Text;
		run.style = style;
		run.text = separator + content;
		run.target = target;
		runs.push_back(std::move(run));
	}

	const ParseOptions&	m_options;
	Document&			m_document;

	std::vector<ListState>	m_lists;
	int						m_quoteDepth = 0;
	std::wstring			m_pendingMarker;

	Block					m_block;				//!< 作成中のブロック(段落・見出し・コード)
	InlineRuns*				m_runs = nullptr;		//!< インラインの追加先(m_blockまたは表のセル)
	bool					m_implicitLeaf = false;	//!< m_blockがmd4cのブロック通知なしに作られた
	bool					m_pendingSoftBreak = false;

	int						m_bold = 0;
	int						m_italic = 0;
	int						m_strike = 0;
	int						m_code = 0;
	int						m_link = 0;
	std::wstring			m_linkTarget;
	int						m_image = 0;
	std::wstring			m_imageSource;
	std::wstring			m_imageAlt;

	bool					m_inTable = false;
	bool					m_headSection = false;
	Block					m_table;
	TableRow				m_row;
	TableCell				m_cell;

	bool					m_stop = false;
	bool					m_failed = false;
	std::uint32_t			m_events = 0;			//!< 通知されたイベントの数(中止の確認の間隔を決める)

	static constexpr std::uint32_t kCancelPollInterval = 64U;	//!< 2の累乗にすること

	inline static const std::wstring kNoTarget{};
};

// md4cのコールバック。C言語のコードを例外が通り抜けるのは未定義の動作なので、必ずここで止める
template <typename Func>
int Guard(void* userdata, Func&& func) noexcept
{
	auto* builder = static_cast<Builder*>(userdata);
	try {
		return func(*builder);
	}
	catch (...) {
		builder->Fail();
		return -1;
	}
}

int OnEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
	return Guard(userdata, [&](Builder& b) { return b.EnterBlock(type, detail); });
}

int OnLeaveBlock(MD_BLOCKTYPE type, void* /*detail*/, void* userdata)
{
	return Guard(userdata, [&](Builder& b) { return b.LeaveBlock(type); });
}

int OnEnterSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
	return Guard(userdata, [&](Builder& b) { return b.EnterSpan(type, detail); });
}

int OnLeaveSpan(MD_SPANTYPE type, void* /*detail*/, void* userdata)
{
	return Guard(userdata, [&](Builder& b) { return b.LeaveSpan(type); });
}

int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
	return Guard(userdata, [&](Builder& b) { return b.Text(type, text, size); });
}

class Md4cParser final : public IMarkdownParser {
public:
	Document Parse(std::wstring_view source, const ParseOptions& options) const override
	{
		Document document;
		try {
			// 上限を超えた分は切り捨てる。サロゲートペアの途中では切らない
			const std::size_t limit = (std::min)(options.maxInputChars,
				static_cast<std::size_t>((std::numeric_limits<MD_SIZE>::max)()));
			if (source.size() > limit) {
				std::size_t length = limit;
				if (length > 0 && text::IsHighSurrogate(static_cast<std::uint16_t>(source[length - 1]))) {
					--length;
				}
				source = source.substr(0, length);
				document.truncated = true;
			}

			if (options.shouldCancel && options.shouldCancel()) {
				document.cancelled = true;
				return document;
			}
			Builder builder(options, document);
			MD_PARSER parser{};
			parser.abi_version = 0;
			parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS
				| MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS;
			parser.enter_block = OnEnterBlock;
			parser.leave_block = OnLeaveBlock;
			parser.enter_span = OnEnterSpan;
			parser.leave_span = OnLeaveSpan;
			parser.text = OnText;

			const MD_CHAR* data = source.empty() ? L"" : source.data();
			md_parse(data, static_cast<MD_SIZE>(source.size()), &parser, &builder);
			if (!builder.IsStopped()) {
				builder.Finish();
			}
			if (builder.HasFailed()) {
				document.truncated = true;
			}
		}
		catch (...) {
			document.truncated = true;	// メモリ不足など。取得できた範囲を返す
		}
		return document;
	}
};

} // namespace

std::unique_ptr<IMarkdownParser> CreateMd4cParser()
{
	return std::make_unique<Md4cParser>();
}

} // namespace markdown
