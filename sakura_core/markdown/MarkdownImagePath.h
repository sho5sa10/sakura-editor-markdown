/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNIMAGEPATH_6B0D93E2_41A7_4C58_8E1F_D2A59C07B346_H_
#define SAKURA_MARKDOWNIMAGEPATH_6B0D93E2_41A7_4C58_8E1F_D2A59C07B346_H_
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace markdown {

//! 画像の読み込み先。rootは、画像がその中にあることを確認するための、文書のあるフォルダ
struct ImageLocation {
	std::wstring	root;	//!< 文書のあるフォルダ(末尾に\なし)
	std::wstring	path;	//!< 画像ファイルの絶対パス。rootの下にある
};

/*!
	@brief Markdownの画像のパス(![alt](パス))を、読み込むファイルの絶対パスにする

	ファイルには一切触れず、文字列だけで判断する(I/Oなし)。ここで通ったものも、
	読み込むときに、実際に開いたファイルがrootの中にあるかを、もう一度確認すること
	(シンボリックリンクやジャンクションは、文字列だけでは分からないため)。

	受け付けるのは、文書のあるフォルダからの相対パスだけ。次のものは拒否する:
	- 文書が保存されていない(基準のフォルダがない)
	- URL(http:、file:、data: など)、ドライブ付きパス、NTFSの代替データストリーム(':'を含むもの)
	- 絶対パス、UNCパス、先頭が'/'や'\\'のパス(//で始まるURL形式を含む)
	- ".."でフォルダの外に出るもの(%2e%2e のようなパーセントエンコードも、展開してから調べる)
	- 空、制御文字、ファイル名に使えない文字(< > " | *)、NULを含むもの
	- 末尾が'.'や' 'の名前(Windowsが無視して別の名前にしてしまう)
	- CON、PRN、AUX、NUL、COM1〜9、LPT1〜9(拡張子付きを含む)のデバイス名

	@param[in] documentPath	文書のファイルパス(絶対パス)
	@param[in] target		Markdownに書かれた画像のパス。?以降と#以降は無視する。%XXはUTF-8として展開する
	@return 読み込み先。拒否したときはstd::nullopt
*/
std::optional<ImageLocation> ResolveLocalImagePath(std::wstring_view documentPath, std::wstring_view target);

} // namespace markdown

#endif /* SAKURA_MARKDOWNIMAGEPATH_6B0D93E2_41A7_4C58_8E1F_D2A59C07B346_H_ */
