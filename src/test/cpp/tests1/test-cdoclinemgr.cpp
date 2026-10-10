/*
	Copyright (C) 2021-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "pch.h"
#include "doc/logic/CDocLineMgr.h"

#include <array>

TEST(CDocLineMgr, ListManipulations)
{
	CDocLineMgr m;

	// 初期状態。行数0。
	EXPECT_EQ(m.GetLineCount(), 0);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), nullptr);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), nullptr);
	EXPECT_EQ(m.GetDocLineTop(), nullptr);
	EXPECT_EQ(m.GetDocLineBottom(), nullptr);

	// 1行追加する。行数1。
	CDocLine* a = m.AddNewLine();
	ASSERT_NE(a, nullptr);
	a->SetDocLineString(L"A", 1, false);
	EXPECT_EQ(m.GetLineCount(), 1);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), a);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), nullptr);
	EXPECT_EQ(m.GetDocLineTop(), a);
	EXPECT_EQ(m.GetDocLineBottom(), a);

	// 先頭に1行入れる。行数2。
	CDocLine* b = m.InsertNewLine(m.GetDocLineTop());
	ASSERT_NE(b, nullptr);
	b->SetDocLineString(L"B", 1, false);
	EXPECT_EQ(m.GetLineCount(), 2);
	//EXPECT_EQ(m.GetLine(CLogicInt(0)), b);
	//EXPECT_EQ(m.GetLine(CLogicInt(1)), a);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), nullptr);
	EXPECT_EQ(m.GetDocLineTop(), b);
	EXPECT_EQ(m.GetDocLineBottom(), a);

	// 中間に1行入れる。行数3。
	CDocLine* c = m.InsertNewLine(m.GetDocLineBottom());
	ASSERT_NE(c, nullptr);
	c->SetDocLineString(L"C", 1, false);
	EXPECT_EQ(m.GetLineCount(), 3);
	//EXPECT_EQ(m.GetLine(CLogicInt(0)), b);
	//EXPECT_EQ(m.GetLine(CLogicInt(1)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), a);
	EXPECT_EQ(m.GetDocLineTop(), b);
	EXPECT_EQ(m.GetDocLineBottom(), a);

	// 末尾に1行追加する。行数4。
	CDocLine* d = m.InsertNewLine(nullptr);
	ASSERT_NE(d, nullptr);
	d->SetDocLineString(L"D", 1, false);
	EXPECT_EQ(m.GetLineCount(), 4);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), b);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), a);
	EXPECT_EQ(m.GetLine(CLogicInt(3)), d);
	EXPECT_EQ(m.GetDocLineTop(), b);
	EXPECT_EQ(m.GetDocLineBottom(), d);

	// 末尾に1行追加する。行数5。
	CDocLine* e = m.AddNewLine();
	ASSERT_NE(e, nullptr);
	e->SetDocLineString(L"E", 1, false);
	EXPECT_EQ(m.GetLineCount(), 5);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), b);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), a);
	EXPECT_EQ(m.GetLine(CLogicInt(4)), e);
	EXPECT_EQ(m.GetLine(CLogicInt(3)), d);
	EXPECT_EQ(m.GetDocLineTop(), b);
	EXPECT_EQ(m.GetDocLineBottom(), e);

	// 先頭の1行を削除する。行数4。
	m.DeleteLine(b);
	EXPECT_EQ(m.GetLineCount(), 4);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), a);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), d);
	EXPECT_EQ(m.GetLine(CLogicInt(3)), e);
	EXPECT_EQ(m.GetDocLineTop(), c);
	EXPECT_EQ(m.GetDocLineBottom(), e);

	// 中間の1行を削除する。行数3。
	m.DeleteLine(a);
	EXPECT_EQ(m.GetLineCount(), 3);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), d);
	EXPECT_EQ(m.GetLine(CLogicInt(2)), e);
	EXPECT_EQ(m.GetDocLineTop(), c);
	EXPECT_EQ(m.GetDocLineBottom(), e);

	// 末尾の1行を削除する。行数2。
	m.DeleteLine(e);
	EXPECT_EQ(m.GetLineCount(), 2);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), c);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), d);
	EXPECT_EQ(m.GetDocLineTop(), c);
	EXPECT_EQ(m.GetDocLineBottom(), d);

	// すべて削除する。行数0。
	m.DeleteAllLine();
	EXPECT_EQ(m.GetLineCount(), 0);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), nullptr);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), nullptr);
	EXPECT_EQ(m.GetDocLineTop(), nullptr);
	EXPECT_EQ(m.GetDocLineBottom(), nullptr);

	// 最後の行を DeleteLine で削除するケース。
	m.DeleteLine(m.AddNewLine());
	EXPECT_EQ(m.GetLineCount(), 0);
	EXPECT_EQ(m.GetLine(CLogicInt(0)), nullptr);
	EXPECT_EQ(m.GetLine(CLogicInt(1)), nullptr);
	EXPECT_EQ(m.GetDocLineTop(), nullptr);
	EXPECT_EQ(m.GetDocLineBottom(), nullptr);
}

TEST(CDocLineMgr, RandomAccess)
{
	std::array<CDocLine*, 100> lines;
	CDocLineMgr m;
	for (int i = 0; i < lines.size(); ++i)
		lines[i] = m.AddNewLine();

	EXPECT_EQ(m.GetLine(CLogicInt(10)), lines[10]);
	EXPECT_EQ(m.GetLine(CLogicInt(90)), lines[90]);
	EXPECT_EQ(m.GetLine(CLogicInt(60)), lines[60]);
	EXPECT_EQ(m.GetLine(CLogicInt(70)), lines[70]);
}

/*!
	リビジョン番号の検査。
	本文が変更された可能性があるたびに進み、戻らないこと。
 */
TEST(CDocLineMgr, RevisionStartsAtZero)
{
	CDocLineMgr m;
	EXPECT_EQ(m.GetRevision(), 0U);
}

TEST(CDocLineMgr, RevisionNeverGoesBack)
{
	CDocLineMgr m;
	const auto r0 = m.GetRevision();

	// 全消去で進む。空の状態に何度戻しても、同じ番号には戻らない(_Init()でリセットしない)
	m.DeleteAllLine();
	const auto r1 = m.GetRevision();
	EXPECT_GT(r1, r0);
	m.DeleteAllLine();
	EXPECT_GT(m.GetRevision(), r1);

	// 空の他のマネージャーを追加する場合でも進む(読み込み完了時の呼び出しを想定)
	const auto r2 = m.GetRevision();
	CDocLineMgr empty;
	m.AppendAsMove(empty);
	EXPECT_GT(m.GetRevision(), r2);

	// 行の入った他のマネージャーを追加する。追加元の番号は、追加先に影響しない
	CDocLineMgr other;
	other.SetMemoryResource(m.GetMemoryResource());	// 行の所有権が移るので、メモリリソースを共有する(CReadManagerと同じ)
	other.AddNewLine()->SetDocLineString(L"A\n", 2, false);
	const auto r3 = m.GetRevision();
	const auto otherRevision = other.GetRevision();
	m.AppendAsMove(other);
	EXPECT_GT(m.GetRevision(), r3);
	EXPECT_EQ(m.GetLineCount(), 1);
	EXPECT_EQ(other.GetRevision(), otherRevision);	// 追加元は、移動してもここでは進めない
}

TEST(CDocLineMgr, RevisionIsPerInstance)
{
	// ファイル読み込みでは、別のスレッドが一時的なマネージャーに行を追加する。
	// 表示中の文書のリビジョン番号は、それに影響されないこと。
	CDocLineMgr shown;
	const auto before = shown.GetRevision();
	CDocLineMgr temporary;
	temporary.AddNewLine();
	temporary.AddNewLine();
	temporary.DeleteAllLine();
	EXPECT_EQ(shown.GetRevision(), before);
}
