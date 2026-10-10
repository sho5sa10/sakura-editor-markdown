/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CMARKDOWNIMAGELOADER_C71F5A38_09DE_4B26_9E64_3A8B2D50F1C7_H_
#define SAKURA_CMARKDOWNIMAGELOADER_C71F5A38_09DE_4B26_9E64_3A8B2D50F1C7_H_
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "markdown/MarkdownImagePath.h"

namespace markdown {

//! 読み込みの上限(GCCは、クラスの中の入れ子の型の既定メンバー初期化子を、そのクラスの中では使えないので外に出している)
struct ImageLoadLimits {
	std::uint64_t	maxFileBytes = 32ULL * 1024 * 1024;		//!< ファイルの大きさ
	std::uint64_t	maxSourcePixels = 64ULL * 1024 * 1024;	//!< 画像の画素数(デコード前)
	std::uint32_t	maxEdge = 2048;							//!< これを超える辺は、縮小してデコードする
};

/*!
	@brief Markdownのローカル画像を、バックグラウンドで読み込んでデコードする

	安全のため、ファイルを開いてから、実際に開いたファイルが文書のあるフォルダの中か
	(シンボリックリンクやジャンクションの先を含めて)を確認し、確認したそのハンドルから
	デコードする。パスを開き直すことはしない。大きすぎるファイルや画像は拒否する。
	GDIやウィンドウには触れず、デコードしたピクセルだけを返す。
*/
class CMarkdownImageLoader {
public:
	using Limits = ImageLoadLimits;

	//! 読み込みの結果
	struct Image {
		std::wstring				path;			//!< 依頼したパス
		bool						ok = false;
		int							width = 0;
		int							height = 0;
		std::vector<std::uint8_t>	pixels;			//!< 上から下へ、BGRA(乗算済みアルファ)、1行はwidth*4バイト
	};

	//! 結果ができたときに、ワーカーのスレッドから呼ばれる。メッセージを投げるだけにすること
	using Notify = std::function<void()>;

	explicit CMarkdownImageLoader(Notify notify, Limits limits = Limits{});
	~CMarkdownImageLoader();

	CMarkdownImageLoader(const CMarkdownImageLoader&) = delete;
	CMarkdownImageLoader& operator=(const CMarkdownImageLoader&) = delete;

	/*!
		@brief 画像の読み込みを依頼する
		@retval false	待ち行列がいっぱい、または終了している
	*/
	bool Request(const ImageLocation& location);

	//! 読み込みを終えたものを取り出す(失敗したものも、ok=falseで返る)
	std::vector<Image> TakeReady();

	//! 終了する。実行中の読み込みの終了は待たない。戻った後はNotifyが呼ばれない。何度呼んでもよい
	void Shutdown() noexcept;

	/*!
		@brief 同期で1枚を読み込む(安全の確認から、デコードまで)

		単体テストなどで使う。デコードは専用のスレッドで行い、完了まで待つ。
		呼び出したスレッドのCOMの状態(初期化の有無や方式)には影響しない。
	*/
	static Image Load(const ImageLocation& location, const Limits& limits = Limits{});

private:
	//! Loadの本体。呼び出したスレッドでCOMを使う(初期化されていなければ、中で初期化して終わりに戻す)。ワーカーが使う
	static Image LoadOnThisThread(const ImageLocation& location, const Limits& limits);

	struct State;
	std::shared_ptr<State>	m_state;
	std::thread				m_thread;
};

} // namespace markdown

#endif /* SAKURA_CMARKDOWNIMAGELOADER_C71F5A38_09DE_4B26_9E64_3A8B2D50F1C7_H_ */
