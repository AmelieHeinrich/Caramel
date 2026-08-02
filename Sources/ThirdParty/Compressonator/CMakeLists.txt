# Thin wrapper around AMD Compressonator's own cmp_core/CMakeLists.txt (vendored as a sparse,
# blobless git submodule at upstream/ -- only cmp_core/, applications/_libs/cmp_math/, and
# license/ are checked out, not the full ~720MB monorepo). cmp_core's own CMakeLists.txt is
# self-contained (its only external reference is a cosmetic IDE-folder variable), so we reuse it
# directly instead of re-transcribing its file list and per-ISA compiler flags by hand.
#
# Produces CMP_Core (static lib) exposing cmp_core/source/cmp_core.h's CompressBlockBC7/BC5.

cmake_minimum_required(VERSION 3.10)
project(CompressonatorCore CXX)

# cmp_core/CMakeLists.txt references this for set_target_properties(... FOLDER ...) IDE grouping;
# it's normally set by the full monorepo's root CMakeLists.txt. An empty/unset value makes
# set_target_properties fail (wrong argument count), so it must be non-empty here.
set(PROJECT_FOLDER_SDK_LIBS "SDK")

add_subdirectory(upstream/cmp_core cmp_core)

# cmp_core's own CMakeLists.txt declares no install() rules (it's normally add_subdirectory'd
# into the full monorepo build, never installed standalone) and links the per-ISA SIMD libs
# PRIVATE, so their object code isn't merged into CMP_Core.lib -- all four archives need to be
# installed separately for a downstream static link to find every symbol.
install(TARGETS CMP_Core CMP_Core_SSE CMP_Core_AVX CMP_Core_AVX512 ARCHIVE DESTINATION lib)
install(FILES upstream/cmp_core/source/cmp_core.h DESTINATION include)
