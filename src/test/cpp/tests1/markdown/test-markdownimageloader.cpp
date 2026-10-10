/*! @file */
/*
	Copyright (C) 2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "markdown/CMarkdownImageLoader.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace markdown;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

//! 3x2画素のPNG。1行目: 赤、緑、青(不透明)。2行目: 半透明の白(alpha=128)、黒、黄
const unsigned char kPng[] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9D, 0x74, 0x66,
	0x1A, 0x00, 0x00, 0x00, 0x1A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
	0x1F, 0x0C, 0x19, 0xFE, 0x03, 0xC9, 0xFF, 0xFF, 0x1B, 0x18, 0x40, 0x2C, 0x20, 0x13, 0x00, 0xA3,
	0xFA, 0x0D, 0x74, 0x85, 0x0E, 0x0D, 0x47, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
	0x42, 0x60, 0x82,
};

//! テストごとの作業フォルダ。終わると消す(ジャンクションは、先を消さないように先に外す)
class TempFolder {
public:
	TempFolder()
	{
		wchar_t base[MAX_PATH + 1] = {};
		::GetTempPathW(MAX_PATH, base);
		m_path = fs::path(base) / (L"sakura-md-test-" + std::to_wstring(::GetCurrentProcessId()) + L"-"
			+ std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++s_counter));
		fs::create_directories(m_path);
	}

	~TempFolder()
	{
		for (const auto& junction : m_junctions) {
			::RemoveDirectoryW(junction.c_str());
		}
		std::error_code ec;
		fs::remove_all(m_path, ec);
	}

	TempFolder(const TempFolder&) = delete;
	TempFolder& operator=(const TempFolder&) = delete;

	fs::path Path() const { return m_path; }

	fs::path MakeDirectory(const std::wstring& name) const
	{
		const fs::path dir = m_path / name;
		fs::create_directories(dir);
		return dir;
	}

	static void WriteFile(const fs::path& path, const void* data, std::size_t size)
	{
		std::ofstream file(path, std::ios::binary);
		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	}

	static void WritePng(const fs::path& path)
	{
		WriteFile(path, kPng, sizeof(kPng));
	}

	//! ジャンクション(mklink /J)を作る。作れなければfalse
	bool MakeJunction(const fs::path& link, const fs::path& target)
	{
		std::wstring command = L"cmd.exe /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
			return false;
		}
		::WaitForSingleObject(process.hProcess, 10000);
		DWORD exitCode = 1;
		::GetExitCodeProcess(process.hProcess, &exitCode);
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		if (exitCode != 0) return false;
		m_junctions.push_back(link.wstring());
		return true;
	}

private:
	static inline std::atomic<int> s_counter{ 0 };
	fs::path m_path;
	std::vector<std::wstring> m_junctions;
};

ImageLocation Location(const fs::path& root, const fs::path& file)
{
	return ImageLocation{ root.wstring(), file.wstring() };
}

} // namespace

TEST(MarkdownImageLoader, LoadsAnImageInsideTheRoot)
{
	TempFolder folder;
	const auto image = folder.MakeDirectory(L"images") / L"a.png";
	TempFolder::WritePng(image);

	const auto result = CMarkdownImageLoader::Load(Location(folder.Path(), image));
	ASSERT_TRUE(result.ok);
	EXPECT_EQ(result.width, 3);
	EXPECT_EQ(result.height, 2);
	ASSERT_EQ(result.pixels.size(), 3U * 2U * 4U);
	// BGRA、乗算済みアルファ。1画素目は赤(不透明)
	EXPECT_EQ(result.pixels[0], 0);
	EXPECT_EQ(result.pixels[1], 0);
	EXPECT_EQ(result.pixels[2], 255);
	EXPECT_EQ(result.pixels[3], 255);
	// 2行目の1画素目は、半透明の白。乗算済みなので、色もalphaと同じ値になる
	const std::size_t secondRow = 3U * 4U;
	EXPECT_EQ(result.pixels[secondRow + 3], 128);
	EXPECT_EQ(result.pixels[secondRow + 0], 128);
	EXPECT_EQ(result.pixels[secondRow + 1], 128);
	EXPECT_EQ(result.pixels[secondRow + 2], 128);
}

TEST(MarkdownImageLoader, FindsFilesWithJapaneseNames)
{
	TempFolder folder;
	const auto image = folder.MakeDirectory(L"画像") / L"日本語の名前.png";
	TempFolder::WritePng(image);
	EXPECT_TRUE(CMarkdownImageLoader::Load(Location(folder.Path(), image)).ok);
}

TEST(MarkdownImageLoader, RejectsMissingFilesDirectoriesAndGarbage)
{
	TempFolder folder;
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), folder.Path() / L"none.png")).ok);

	const auto dir = folder.MakeDirectory(L"dir.png");
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), dir)).ok);

	const auto empty = folder.Path() / L"empty.png";
	TempFolder::WriteFile(empty, "", 0);
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), empty)).ok);

	const auto garbage = folder.Path() / L"garbage.png";
	const std::string text(1000, 'x');
	TempFolder::WriteFile(garbage, text.data(), text.size());
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), garbage)).ok);

	// PNGの途中で切れたもの
	const auto cut = folder.Path() / L"cut.png";
	TempFolder::WriteFile(cut, kPng, 40);
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), cut)).ok);
}

TEST(MarkdownImageLoader, RejectsFilesOutsideTheRoot)
{
	TempFolder folder;
	const auto root = folder.MakeDirectory(L"root");
	const auto outside = folder.MakeDirectory(L"outside") / L"secret.png";
	TempFolder::WritePng(outside);

	// 字句的な検査をすり抜けたと仮定して、パスだけが外を指している場合
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, outside)).ok);
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, root / L".." / L"outside" / L"secret.png")).ok);

	// フォルダ名が前方一致するだけの別のフォルダ(root と root2)
	const auto sibling = folder.MakeDirectory(L"root2") / L"a.png";
	TempFolder::WritePng(sibling);
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, sibling)).ok);

	// 同じ構成で、中にあれば読める(上の失敗が、フォルダの指定ミスによるものではないことの確認)
	const auto inside = root / L"a.png";
	TempFolder::WritePng(inside);
	EXPECT_TRUE(CMarkdownImageLoader::Load(Location(root, inside)).ok);
}

TEST(MarkdownImageLoader, RejectsJunctionsThatLeadOutsideTheRoot)
{
	TempFolder folder;
	const auto root = folder.MakeDirectory(L"root");
	const auto outsideDir = folder.MakeDirectory(L"outside");
	TempFolder::WritePng(outsideDir / L"secret.png");

	const auto link = root / L"link";
	if (!folder.MakeJunction(link, outsideDir)) {
		GTEST_SKIP() << "ジャンクションを作れなかった";
	}
	ASSERT_TRUE(fs::exists(link / L"secret.png"));	// パスの上では、rootの中にある

	// 字句的にはrootの中だが、実際のファイルは外にある
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, link / L"secret.png")).ok);

	// 同じ中身でも、rootの中にある本物のフォルダなら読める
	const auto real = folder.MakeDirectory(L"root") / L"real";
	fs::create_directories(real);
	TempFolder::WritePng(real / L"secret.png");
	EXPECT_TRUE(CMarkdownImageLoader::Load(Location(root, real / L"secret.png")).ok);
}

TEST(MarkdownImageLoader, AcceptsJunctionsThatStayInsideTheRoot)
{
	TempFolder folder;
	const auto root = folder.MakeDirectory(L"root");
	const auto target = folder.MakeDirectory(L"root\\real");
	TempFolder::WritePng(target / L"a.png");
	const auto link = root / L"alias";
	if (!folder.MakeJunction(link, target)) {
		GTEST_SKIP() << "ジャンクションを作れなかった";
	}
	EXPECT_TRUE(CMarkdownImageLoader::Load(Location(root, link / L"a.png")).ok);
}

TEST(MarkdownImageLoader, EnforcesTheLimits)
{
	TempFolder folder;
	const auto image = folder.Path() / L"a.png";
	TempFolder::WritePng(image);

	CMarkdownImageLoader::Limits tiny;
	tiny.maxFileBytes = 10;	// ファイルが大きすぎる
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), image), tiny).ok);

	CMarkdownImageLoader::Limits fewPixels;
	fewPixels.maxSourcePixels = 5;	// 3x2=6画素は多すぎる
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(folder.Path(), image), fewPixels).ok);

	// 長い辺がmaxEdgeを超えるときは、縮小して読む
	CMarkdownImageLoader::Limits shrink;
	shrink.maxEdge = 2;
	const auto shrunk = CMarkdownImageLoader::Load(Location(folder.Path(), image), shrink);
	ASSERT_TRUE(shrunk.ok);
	EXPECT_EQ(shrunk.width, 2);
	EXPECT_EQ(shrunk.height, 1);
	EXPECT_EQ(shrunk.pixels.size(), 2U * 1U * 4U);
}

TEST(MarkdownImageLoader, LoadsInTheBackgroundAndNotifies)
{
	TempFolder folder;
	const auto image = folder.Path() / L"a.png";
	TempFolder::WritePng(image);
	const auto missing = folder.Path() / L"missing.png";

	std::atomic<int> notified{ 0 };
	CMarkdownImageLoader loader([&] { ++notified; });
	EXPECT_TRUE(loader.Request(Location(folder.Path(), image)));
	EXPECT_TRUE(loader.Request(Location(folder.Path(), missing)));

	std::vector<CMarkdownImageLoader::Image> all;
	const auto limit = std::chrono::steady_clock::now() + 10s;
	while (all.size() < 2 && std::chrono::steady_clock::now() < limit) {
		for (auto& r : loader.TakeReady()) all.push_back(std::move(r));
		std::this_thread::sleep_for(5ms);
	}
	ASSERT_EQ(all.size(), 2U);
	EXPECT_GE(notified.load(), 1);
	int ok = 0;
	for (const auto& r : all) {
		if (r.ok) {
			++ok;
			EXPECT_EQ(r.path, image.wstring());
		} else {
			EXPECT_EQ(r.path, missing.wstring());
		}
	}
	EXPECT_EQ(ok, 1);
}

TEST(MarkdownImageLoader, ShutdownStopsNotifyingAndRefusesRequests)
{
	TempFolder folder;
	const auto image = folder.Path() / L"a.png";
	TempFolder::WritePng(image);

	std::atomic<int> notified{ 0 };
	{
		CMarkdownImageLoader loader([&] { ++notified; });
		for (int i = 0; i < 50; ++i) {
			loader.Request(Location(folder.Path(), image));
		}
		loader.Shutdown();
		const int atShutdown = notified.load();
		EXPECT_FALSE(loader.Request(Location(folder.Path(), image)));
		EXPECT_TRUE(loader.TakeReady().empty());
		std::this_thread::sleep_for(200ms);
		EXPECT_EQ(notified.load(), atShutdown);	// 終了後は通知されない
		loader.Shutdown();
	}
	SUCCEED();
}

TEST(MarkdownImageLoader, RejectsSymbolicLinksThatLeadOutsideTheRoot)
{
	TempFolder folder;
	const auto root = folder.MakeDirectory(L"root");
	const auto outsideDir = folder.MakeDirectory(L"outside");
	const auto secret = outsideDir / L"secret.png";
	TempFolder::WritePng(secret);

	// ファイルのシンボリックリンク(rootの中にあるように見えるが、実体は外にある)
	const auto fileLink = root / L"alias.png";
	if (!::CreateSymbolicLinkW(fileLink.c_str(), secret.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
		GTEST_SKIP() << "シンボリックリンクを作れなかった(開発者モードか、管理者権限が必要)。error=" << ::GetLastError();
	}
	ASSERT_TRUE(fs::exists(fileLink));
	EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, fileLink)).ok);

	// フォルダのシンボリックリンク
	const auto dirLink = root / L"dirlink";
	if (::CreateSymbolicLinkW(dirLink.c_str(), outsideDir.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
		EXPECT_FALSE(CMarkdownImageLoader::Load(Location(root, dirLink / L"secret.png")).ok);
	}

	// rootの中を指すシンボリックリンクは、読める
	const auto inside = root / L"real.png";
	TempFolder::WritePng(inside);
	const auto insideLink = root / L"alias2.png";
	if (::CreateSymbolicLinkW(insideLink.c_str(), inside.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
		EXPECT_TRUE(CMarkdownImageLoader::Load(Location(root, insideLink)).ok);
	}
}
