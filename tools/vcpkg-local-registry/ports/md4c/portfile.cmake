# Based on the md4c port of microsoft/vcpkg (MIT License, Copyright (c) Microsoft Corporation).
#
# Changes for Sakura Editor:
#   - Build the parser for UTF-16 (MD4C_USE_UTF16), so that the UTF-16 text of the editor
#     can be passed to md_parse() without converting it to UTF-8.
#     This macro changes the type MD_CHAR in md4c.h, so every file that includes md4c.h
#     must define it as well (sakura_core/markdown/Md4cParser.cpp does).
#   - Do not build the HTML renderer. md4c does not support UTF-16 in it.
#   - strikethrough-flanking.patch: make the strikethrough (~~) use the same left/right-flanking
#     rules as the asterisk emphasis, as cmark-gfm (GitHub) does. Unpatched md4c 0.6.0 does not
#     strike "a~~b~~c" (it treats ~~ like an intra-word underscore), which breaks Japanese text
#     where words are not separated by spaces. See sakura_core/markdown/README.md.

if(NOT VCPKG_TARGET_IS_WINDOWS)
  message(FATAL_ERROR "The md4c port of Sakura Editor supports Windows targets only (MD4C_USE_UTF16 requires Windows).")
endif()

vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO mity/md4c
  REF "v${VERSION}"
  SHA512 a8b3deba8b6e25b11cfc8f796ff8299d47f73c0e668badeaadd38609d72d1d301c662e17f0fab6ccefe382d83bad6f47a3aedf68a16021521822cc7a285efcad
  HEAD_REF master
  PATCHES
    cmake.patch
    strikethrough-flanking.patch
)

# vcpkg_cmake_configure requires VCPKG_C_FLAGS and VCPKG_CXX_FLAGS to be set together.
string(APPEND VCPKG_C_FLAGS " -DMD4C_USE_UTF16")
string(APPEND VCPKG_CXX_FLAGS "")

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DBUILD_MD2HTML_EXECUTABLE=OFF
    -DBUILD_HTML_LIBRARY=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/md4c")
vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
