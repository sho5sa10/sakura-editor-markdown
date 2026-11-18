/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MARKDOWNHTML_4E8B2C17_93D6_4A05_B7F1_2C6D9E30A584_H_
#define SAKURA_MARKDOWNHTML_4E8B2C17_93D6_4A05_B7F1_2C6D9E30A584_H_
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "markdown/MarkdownModel.h"

/*!
	@file MarkdownHtml.h
	@brief プレビューの内容を、クリップボードに入れる HTML とテキストにする

	Teams、Outlook、Word などに貼り付けたとき、表や見出しがそのまま使えるようにする。
	貼り付け先は <style> を捨てることが多いので、書式はすべて要素の style 属性に書く。
	Win32 に依存しない(単体テストできる)。
*/
namespace markdown {

struct HtmlExportOptions {
	/*!
		画像のパス(Markdown に書かれた未検証の文字列)から、<img> の src に入れる値
		(data: URI など)を返す。空を返したとき、または未指定のときは、[代替テキスト] にする。
	*/
	std::function<std::string(const std::wstring& target)> imageSource;
};

//! 文書を HTML の断片(UTF-8)にする。<html> や <body> は含まない
std::string BuildHtmlFragment(const Document& document, const HtmlExportOptions& options = {});

//! 文書をプレーンテキストにする。表の列はタブで区切る(Excel に貼れる)。改行は CRLF
std::wstring BuildPlainText(const Document& document);

//! HTML の断片を、クリップボードの "HTML Format" (CF_HTML) の形式にする
std::string BuildClipboardHtml(std::string_view fragmentUtf8);

//! UTF-16 を UTF-8 にする。対にならないサロゲートは U+FFFD にする
std::string ToUtf8(std::wstring_view text);

//! Base64 にする(改行なし)
std::string Base64Encode(const void* data, std::size_t size);

//! 画像ファイルの拡張子から MIME タイプを返す。対応しないものは空
std::string ImageMimeTypeFromPath(std::wstring_view path);

} // namespace markdown

#endif /* SAKURA_MARKDOWNHTML_4E8B2C17_93D6_4A05_B7F1_2C6D9E30A584_H_ */
