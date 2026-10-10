/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CMARKDOWNPREVIEWWORKER_2E8A41D7_B6C3_4F95_A027_9D1E73C5B864_H_
#define SAKURA_CMARKDOWNPREVIEWWORKER_2E8A41D7_B6C3_4F95_A027_9D1E73C5B864_H_
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "markdown/IMarkdownParser.h"

namespace markdown {

/*!
	@brief Markdownの解析をバックグラウンドで行う

	- 解析を行うスレッドは1本。同時に持つのは「実行中の1件」と「最新の待機1件」だけ。
	  次の依頼が来たら、待機中のものは捨てて置き換える。
	- 依頼のたびに世代番号を振る。最新でない世代の結果は、渡さずに捨てる。
	- 実行中の解析も、より新しい依頼が来た、または終了を求められたら、解析の途中で中止する。
	- ワーカーはソース文字列のコピーだけを読む。文書やウィンドウには触れない。

	依頼、結果の取得、終了はUIスレッド(同じスレッド)から呼ぶこと。
*/
class CMarkdownPreviewWorker {
public:
	struct Result {
		std::uint64_t	generation = 0;
		Document		document;
	};

	//! 結果ができたときに、ワーカーのスレッドから呼ばれる。ここでUIの処理はせず、メッセージを投げるだけにする
	using Notify = std::function<void()>;

	CMarkdownPreviewWorker(std::shared_ptr<const IMarkdownParser> parser, Notify notify);
	~CMarkdownPreviewWorker();

	CMarkdownPreviewWorker(const CMarkdownPreviewWorker&) = delete;
	CMarkdownPreviewWorker& operator=(const CMarkdownPreviewWorker&) = delete;

	/*!
		@brief 解析を依頼する
		@param[in] source	解析する文字列(呼び出し側がコピーしたもの)
		@param[in] options	解析の上限。shouldCancelは上書きされる
		@return 依頼の世代番号。1から始まり、依頼のたびに増える
	*/
	std::uint64_t Queue(std::wstring source, ParseOptions options);

	//! 最新の依頼の結果が出ていれば取り出す。古い依頼の結果は、ここで捨てる
	std::optional<Result> TakeResult();

	//! 最後に依頼した世代番号。まだ依頼がなければ0
	std::uint64_t LatestGeneration() const noexcept;

	/*!
		@brief 終了する
		待機中の依頼と結果を捨て、実行中の解析に中止を求める。実行中の解析の終了は待たない
		(UIを止めないため)。戻った後は、Notifyが呼ばれることはない。何度呼んでもよい。
	*/
	void Shutdown() noexcept;

private:
	struct State;
	std::shared_ptr<State>	m_state;
	std::thread				m_thread;
};

} // namespace markdown

#endif /* SAKURA_CMARKDOWNPREVIEWWORKER_2E8A41D7_B6C3_4F95_A027_9D1E73C5B864_H_ */
