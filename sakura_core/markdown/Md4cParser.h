/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MD4CPARSER_7E5B2A94_C1D8_4F36_B07A_2D9E64F1A358_H_
#define SAKURA_MD4CPARSER_7E5B2A94_C1D8_4F36_B07A_2D9E64F1A358_H_
#pragma once

#include <memory>

#include "markdown/IMarkdownParser.h"

namespace markdown {

/*!
	@brief md4c(https://github.com/mity/md4c, MIT License)を使うパーサーを作る

	md4cを直接使うのはMd4cParser.cppだけ。
	md4cは MD4C_USE_UTF16 を付けてビルドしたものが必要(vcpkgのポートで設定する)。
*/
std::unique_ptr<IMarkdownParser> CreateMd4cParser();

} // namespace markdown

#endif /* SAKURA_MD4CPARSER_7E5B2A94_C1D8_4F36_B07A_2D9E64F1A358_H_ */
