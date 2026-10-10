/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/CMarkdownPreviewWorker.h"
#include "markdown/Md4cParser.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

using namespace markdown;
using namespace std::chrono_literals;

namespace {

Document MakeDocument(const std::wstring& text)
{
	Document document;
	Block block;
	InlineRun run;
	run.text = text;
	block.runs.push_back(run);
	document.blocks.push_back(block);
	return document;
}

std::wstring TextOf(const Document& document)
{
	return document.blocks.empty() || document.blocks[0].runs.empty() ? std::wstring() : document.blocks[0].runs[0].text;
}

//! テスト用のパーサー。解析の中身を関数で差し替える
class FakeParser final : public IMarkdownParser {
public:
	using Func = std::function<Document(std::wstring_view, const ParseOptions&)>;
	explicit FakeParser(Func func) : m_func(std::move(func)) {}

	Document Parse(std::wstring_view source, const ParseOptions& options) const override
	{
		++calls;
		return m_func(source, options);
	}

	mutable std::atomic<int> calls{ 0 };

private:
	Func m_func;
};

template <typename Pred>
bool WaitUntil(Pred pred, std::chrono::milliseconds timeout = 5000ms)
{
	const auto limit = std::chrono::steady_clock::now() + timeout;
	while (!pred()) {
		if (std::chrono::steady_clock::now() > limit) return false;
		std::this_thread::sleep_for(2ms);
	}
	return true;
}

//! 中止を要求されるまで、または指定の時間が経つまで待つ(中止を無視する場合は無視して待つ)
Document SlowParse(std::wstring_view source, const ParseOptions& options, std::chrono::milliseconds duration, bool pollCancel)
{
	const auto end = std::chrono::steady_clock::now() + duration;
	while (std::chrono::steady_clock::now() < end) {
		if (pollCancel && options.shouldCancel && options.shouldCancel()) {
			Document cancelled;
			cancelled.cancelled = true;
			return cancelled;
		}
		std::this_thread::sleep_for(1ms);
	}
	return MakeDocument(std::wstring(source));
}

} // namespace

TEST(MarkdownPreviewWorker, DeliversTheResultAndNotifies)
{
	std::atomic<int> notified{ 0 };
	const auto parser = std::make_shared<FakeParser>([](std::wstring_view source, const ParseOptions&) {
		return MakeDocument(std::wstring(source));
	});
	CMarkdownPreviewWorker worker(parser, [&] { ++notified; });

	EXPECT_EQ(worker.LatestGeneration(), 0U);
	EXPECT_FALSE(worker.TakeResult().has_value());

	const auto generation = worker.Queue(L"一つ目", ParseOptions{});
	EXPECT_EQ(generation, 1U);
	ASSERT_TRUE(WaitUntil([&] { return notified.load() > 0; }));

	const auto result = worker.TakeResult();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->generation, 1U);
	EXPECT_EQ(TextOf(result->document), L"一つ目");
	EXPECT_FALSE(worker.TakeResult().has_value());	// 同じ結果は二度渡さない
}

TEST(MarkdownPreviewWorker, OnlyTheNewestPendingRequestIsParsed)
{
	// 1件目の解析中に、2件目と3件目を続けて依頼する。2件目は待機中に3件目に置き換わり、解析されない
	std::promise<void> release;
	std::shared_future<void> gate = release.get_future().share();
	std::promise<void> firstStarted;
	std::atomic<bool> first{ true };
	std::atomic<int> notified{ 0 };
	std::atomic<int> parsed2{ 0 };

	const auto parser = std::make_shared<FakeParser>([&](std::wstring_view source, const ParseOptions&) {
		if (first.exchange(false)) {
			firstStarted.set_value();
			gate.wait();	// 解放されるまで、中止も見ずに待つ
		}
		if (source == L"2") ++parsed2;
		return MakeDocument(std::wstring(source));
	});
	CMarkdownPreviewWorker worker(parser, [&] { ++notified; });

	worker.Queue(L"1", ParseOptions{});
	firstStarted.get_future().wait();
	worker.Queue(L"2", ParseOptions{});
	const auto third = worker.Queue(L"3", ParseOptions{});
	release.set_value();

	ASSERT_TRUE(WaitUntil([&] { return notified.load() > 0; }));
	const auto result = worker.TakeResult();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->generation, third);
	EXPECT_EQ(TextOf(result->document), L"3");
	EXPECT_EQ(parsed2.load(), 0);
	EXPECT_EQ(parser->calls.load(), 2);	// 1件目と3件目だけ
	EXPECT_EQ(notified.load(), 1);		// 1件目の結果は古いので通知しない
}

TEST(MarkdownPreviewWorker, StaleResultIsNeverDelivered)
{
	// 中止を無視する解析が、新しい依頼の後に終わっても、その結果は渡さない
	std::promise<void> firstStarted;
	std::atomic<bool> first{ true };
	std::atomic<int> notified{ 0 };
	const auto parser = std::make_shared<FakeParser>([&](std::wstring_view source, const ParseOptions& options) {
		if (first.exchange(false)) {
			firstStarted.set_value();
			return SlowParse(source, options, 300ms, false);
		}
		return MakeDocument(std::wstring(source));
	});
	CMarkdownPreviewWorker worker(parser, [&] { ++notified; });

	worker.Queue(L"古い", ParseOptions{});
	firstStarted.get_future().wait();
	worker.Queue(L"新しい", ParseOptions{});

	ASSERT_TRUE(WaitUntil([&] { return notified.load() > 0; }));
	const auto result = worker.TakeResult();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(TextOf(result->document), L"新しい");
	std::this_thread::sleep_for(50ms);
	EXPECT_FALSE(worker.TakeResult().has_value());
	EXPECT_EQ(notified.load(), 1);
}

TEST(MarkdownPreviewWorker, RunningParseIsCancelledWhenANewerRequestArrives)
{
	std::promise<void> firstStarted;
	std::atomic<bool> first{ true };
	std::atomic<bool> firstWasCancelled{ false };
	std::atomic<int> notified{ 0 };
	const auto parser = std::make_shared<FakeParser>([&](std::wstring_view source, const ParseOptions& options) {
		if (first.exchange(false)) {
			firstStarted.set_value();
			const auto document = SlowParse(source, options, 10000ms, true);	// 中止されなければ10秒かかる
			firstWasCancelled = document.cancelled;
			return document;
		}
		return MakeDocument(std::wstring(source));
	});
	CMarkdownPreviewWorker worker(parser, [&] { ++notified; });

	worker.Queue(L"長い解析", ParseOptions{});
	firstStarted.get_future().wait();
	const auto start = std::chrono::steady_clock::now();
	worker.Queue(L"次", ParseOptions{});

	ASSERT_TRUE(WaitUntil([&] { return notified.load() > 0; }));
	EXPECT_TRUE(firstWasCancelled.load());
	EXPECT_LT(std::chrono::steady_clock::now() - start, 2000ms);	// 10秒待たずに次の解析に移る
	const auto result = worker.TakeResult();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(TextOf(result->document), L"次");
}

TEST(MarkdownPreviewWorker, ShutdownDoesNotWaitForTheRunningParseAndNeverNotifiesAfterwards)
{
	std::promise<void> started;
	std::atomic<bool> first{ true };
	std::atomic<int> notified{ 0 };
	// 中止を無視して600msかかる解析
	const auto parser = std::make_shared<FakeParser>([&](std::wstring_view source, const ParseOptions& options) {
		if (first.exchange(false)) started.set_value();
		return SlowParse(source, options, 600ms, false);
	});
	auto worker = std::make_unique<CMarkdownPreviewWorker>(parser, [&] { ++notified; });

	worker->Queue(L"x", ParseOptions{});
	started.get_future().wait();

	const auto start = std::chrono::steady_clock::now();
	worker->Shutdown();
	EXPECT_LT(std::chrono::steady_clock::now() - start, 300ms);	// 解析の終了を待たない
	EXPECT_FALSE(worker->TakeResult().has_value());

	std::this_thread::sleep_for(900ms);	// 解析が終わるまで待つ
	EXPECT_EQ(notified.load(), 0);			// 終了後は通知されない
	EXPECT_FALSE(worker->TakeResult().has_value());

	worker->Shutdown();	// 何度呼んでもよい
	const auto after = worker->Queue(L"y", ParseOptions{});	// 終了後の依頼は無視される
	(void)after;
	std::this_thread::sleep_for(50ms);
	EXPECT_EQ(notified.load(), 0);
	worker.reset();
}

TEST(MarkdownPreviewWorker, DestroyingWithWorkInFlightIsSafe)
{
	// ワーカーを、解析の依頼の直後に破棄することを繰り返す。ハングやクラッシュをしないこと
	for (int i = 0; i < 100; ++i) {
		std::atomic<int> notified{ 0 };
		const auto parser = std::make_shared<FakeParser>([](std::wstring_view source, const ParseOptions& options) {
			return SlowParse(source, options, 5ms, true);
		});
		{
			CMarkdownPreviewWorker worker(parser, [&] { ++notified; });
			worker.Queue(L"a", ParseOptions{});
			worker.Queue(L"b", ParseOptions{});
		}
	}
	SUCCEED();
}

TEST(MarkdownPreviewWorker, ManyRapidRequestsEndWithTheLatest)
{
	std::atomic<int> notified{ 0 };
	const auto parser = std::shared_ptr<const IMarkdownParser>(CreateMd4cParser());
	CMarkdownPreviewWorker worker(parser, [&] { ++notified; });

	std::uint64_t last = 0;
	for (int i = 1; i <= 300; ++i) {
		last = worker.Queue(L"# 見出し" + std::to_wstring(i) + L"\n\n本文\n", ParseOptions{});
	}
	EXPECT_EQ(last, 300U);
	EXPECT_EQ(worker.LatestGeneration(), 300U);

	std::optional<CMarkdownPreviewWorker::Result> result;
	ASSERT_TRUE(WaitUntil([&] {
		if (auto r = worker.TakeResult()) result = std::move(r);
		return result.has_value();
	}));
	EXPECT_EQ(result->generation, 300U);	// 古い世代の結果は渡されない
	ASSERT_FALSE(result->document.blocks.empty());
	EXPECT_EQ(result->document.blocks[0].runs[0].text, L"見出し300");
}

TEST(MarkdownPreviewWorker, Md4cParserStopsWhenCancelled)
{
	// 中止の要求は、解析の途中(ブロックや文字列の通知のとき)に確認される
	std::wstring source;
	for (int i = 0; i < 20000; ++i) {
		source += L"段落" + std::to_wstring(i) + L" **強調** と `コード` です。\n\n";
	}
	const auto parser = CreateMd4cParser();

	std::atomic<int> asked{ 0 };
	ParseOptions options;
	options.shouldCancel = [&] { return ++asked >= 5; };	// 5回目の問い合わせで中止する
	const auto document = parser->Parse(source, options);
	EXPECT_TRUE(document.cancelled);
	EXPECT_LT(document.blocks.size(), 20000U);

	// 要求がなければ、最後まで解析する
	ParseOptions never;
	never.shouldCancel = [] { return false; };
	const auto complete = parser->Parse(source, never);
	EXPECT_FALSE(complete.cancelled);
	EXPECT_EQ(complete.blocks.size(), 20000U);

	// 解析の前から中止が要求されているとき
	ParseOptions already;
	already.shouldCancel = [] { return true; };
	EXPECT_TRUE(parser->Parse(L"x", already).cancelled);
}
