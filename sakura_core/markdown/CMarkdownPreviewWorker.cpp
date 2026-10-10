/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "markdown/CMarkdownPreviewWorker.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace markdown {

//! ワーカーのスレッドとUIスレッドで共有する状態。スレッドが残っていても安全なように、shared_ptrで持つ
struct CMarkdownPreviewWorker::State {
	struct Job {
		std::uint64_t	generation = 0;
		std::wstring	source;
		ParseOptions	options;
	};

	std::shared_ptr<const IMarkdownParser>	parser;

	std::mutex					mutex;
	std::condition_variable		condition;
	std::optional<Job>			pending;		//!< 最新の待機1件
	std::optional<Result>		done;			//!< 取り出されていない結果
	bool						stop = false;	//!< mutexで守る
	std::uint64_t				nextGeneration = 0;

	std::atomic<std::uint64_t>	latest{ 0 };	//!< 最後に依頼した世代。実行中の解析が、自分が古いかを調べるのに使う
	std::atomic<bool>			stopped{ false };

	std::mutex					notifyMutex;	//!< notifyの呼び出しと、Shutdown()での取り外しを直列にする
	Notify						notify;

	void Run();
};

void CMarkdownPreviewWorker::State::Run()
{
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this] { return stop || pending.has_value(); });
			if (stop) {
				return;
			}
			job = std::move(*pending);
			pending.reset();
		}

		const std::uint64_t generation = job.generation;
		job.options.shouldCancel = [this, generation] {
			return stopped.load(std::memory_order_relaxed) || latest.load(std::memory_order_relaxed) != generation;
		};

		Document document;
		try {
			document = parser->Parse(job.source, job.options);
		}
		catch (...) {
			document = Document{};
			document.cancelled = true;	// 失敗した結果は渡さない
		}
		job.source = std::wstring();	// 大きな文字列を早く手放す

		bool hasResult = false;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (stop) {
				return;
			}
			if (!document.cancelled && latest.load() == generation) {
				done = Result{ generation, std::move(document) };
				hasResult = true;
			}
		}
		if (hasResult) {
			std::lock_guard<std::mutex> lock(notifyMutex);
			if (notify) {
				notify();
			}
		}
	}
}

CMarkdownPreviewWorker::CMarkdownPreviewWorker(std::shared_ptr<const IMarkdownParser> parser, Notify notify)
	: m_state(std::make_shared<State>())
{
	m_state->parser = std::move(parser);
	m_state->notify = std::move(notify);
}

CMarkdownPreviewWorker::~CMarkdownPreviewWorker()
{
	Shutdown();
}

std::uint64_t CMarkdownPreviewWorker::Queue(std::wstring source, ParseOptions options)
{
	State& state = *m_state;
	std::uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		if (state.stop) {
			return state.nextGeneration;
		}
		generation = ++state.nextGeneration;
		state.latest.store(generation);
		state.done.reset();	// 古い結果は不要になった
		state.pending = State::Job{ generation, std::move(source), std::move(options) };
	}
	if (!m_thread.joinable()) {
		// 使われるまでスレッドは作らない。ワーカーが残っても状態が消えないよう、shared_ptrを渡す
		m_thread = std::thread([shared = m_state] { shared->Run(); });
	}
	state.condition.notify_one();
	return generation;
}

std::optional<CMarkdownPreviewWorker::Result> CMarkdownPreviewWorker::TakeResult()
{
	State& state = *m_state;
	std::lock_guard<std::mutex> lock(state.mutex);
	if (!state.done.has_value()) {
		return std::nullopt;
	}
	std::optional<Result> result = std::move(state.done);
	state.done.reset();
	if (result->generation != state.latest.load()) {
		return std::nullopt;	// 古い依頼の結果は渡さない
	}
	return result;
}

std::uint64_t CMarkdownPreviewWorker::LatestGeneration() const noexcept
{
	return m_state->latest.load();
}

void CMarkdownPreviewWorker::Shutdown() noexcept
{
	State& state = *m_state;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.stop = true;
		state.stopped.store(true);
		state.pending.reset();
		state.done.reset();
	}
	state.condition.notify_all();
	{
		// 通知の呼び出し中なら終わるまで待つ(メッセージを投げるだけなので短い)。以後は呼ばれない
		std::lock_guard<std::mutex> lock(state.notifyMutex);
		state.notify = nullptr;
	}
	if (m_thread.joinable()) {
		// 実行中の解析はshouldCancelで途中で止まる。止まるのは待たない
		m_thread.detach();
	}
}

} // namespace markdown
