// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "GrpcProtos/symbol.pb.h"
#include "ObjectUtils/CoffFile.h"
#include "Test/Path.h"
#include "TestUtils/TestUtils.h"
#include "absl/strings/ascii.h"

using orbit_grpc_protos::SymbolInfo;
using orbit_test_utils::HasError;
using orbit_test_utils::HasNoError;

namespace orbit_object_utils {

TEST(CoffFile, LoadDebugSymbols) {
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";

  auto coff_file_result = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_result, HasNoError());
  std::unique_ptr<CoffFile> coff_file = std::move(coff_file_result.value());

  const auto symbols_result = coff_file->LoadDebugSymbols();
  ASSERT_THAT(symbols_result, HasNoError());

  std::vector<SymbolInfo> symbol_infos(symbols_result.value().symbol_infos().begin(),
                                       symbols_result.value().symbol_infos().end());
  EXPECT_EQ(symbol_infos.size(), 52);

  // From the DWARF debug info (but also in the COFF symbol table).
  SymbolInfo& symbol_info = symbol_infos[0];
  EXPECT_EQ(symbol_info.demangled_name(), "pre_c_init");
  uint64_t expected_address =
      0x0 + coff_file->GetExecutableSegmentOffset() + coff_file->GetLoadBias();
  EXPECT_EQ(symbol_info.address(), expected_address);
  EXPECT_EQ(symbol_info.size(), 0xc);

  symbol_info = symbol_infos[7];
  EXPECT_EQ(symbol_info.demangled_name(), "PrintHelloWorld");
  expected_address = 0x03a0 + coff_file->GetExecutableSegmentOffset() + coff_file->GetLoadBias();
  EXPECT_EQ(symbol_info.address(), expected_address);
  EXPECT_EQ(symbol_info.size(), 0x1b);

  // Only from the COFF symbol table.
  symbol_info = symbol_infos[36];
  EXPECT_EQ(symbol_info.demangled_name(), "puts");
  expected_address = 0x10a8 + coff_file->GetExecutableSegmentOffset() + coff_file->GetLoadBias();
  EXPECT_EQ(symbol_info.address(), expected_address);
  EXPECT_EQ(symbol_info.size(), 0x8);  // One six-byte jump plus two bytes of padding.

  symbol_info = symbol_infos.back();
  EXPECT_EQ(symbol_info.demangled_name(), "register_frame_ctor");
  expected_address = 0x1300 + coff_file->GetExecutableSegmentOffset() + coff_file->GetLoadBias();
  EXPECT_EQ(symbol_info.address(), expected_address);
  EXPECT_EQ(symbol_info.size(), 0);  // Size of the last function cannot be deduced.
}

TEST(CoffFile, HasDebugSymbols) {
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";

  auto coff_file_result = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_result, HasNoError());

  EXPECT_TRUE(coff_file_result.value()->HasDebugSymbols());
}

TEST(CoffFile, GetFilePath) {
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";

  auto coff_file_result = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_result, HasNoError());

  EXPECT_EQ(coff_file_result.value()->GetFilePath(), file_path);
}

TEST(CoffFile, FileDoesNotExist) {
  const std::filesystem::path file_path = orbit_test::GetTestdataDir() / "does_not_exist";

  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_TRUE(coff_file_or_error.has_error());
  EXPECT_THAT(absl::AsciiStrToLower(coff_file_or_error.error().message()),
              testing::HasSubstr("no such file or directory"));
}

TEST(CoffFile, LoadsPdbPathSuccessfully) {
  // Note that our test library libtest.dll does not have a PDB file path.
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "dllmain.dll";

  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());

  auto pdb_debug_info_or_error = coff_file_or_error.value()->GetDebugPdbInfo();
  ASSERT_THAT(pdb_debug_info_or_error, HasNoError());
  EXPECT_EQ("C:\\tmp\\dllmain.pdb", pdb_debug_info_or_error.value().pdb_file_path.string());

  // The correct loading of age and guid is tested in PdbFileTest, where we compare the
  // DLL and PDB data directly.
}

TEST(CoffFile, FailsWithErrorIfPdbDataNotPresent) {
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";
  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());

  auto pdb_debug_info_or_error = coff_file_or_error.value()->GetDebugPdbInfo();
  ASSERT_THAT(pdb_debug_info_or_error, HasError("Object file does not have debug PDB info."));
}

TEST(CoffFile, GetsCorrectBuildIdIfPdbInfoIsPresent) {
  // Note that our test library libtest.dll does not have a PDB file path.
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "dllmain.dll";

  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());

  EXPECT_EQ("4f9ad6af397f504e88fc34477bd0bae3-1", coff_file_or_error.value()->GetBuildId());
}

TEST(CoffFile, GetsEmptyBuildIdIfPdbInfoIsNotPresent) {
  // Note that our test library libtest.dll does not have a PDB file path.
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";

  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());

  EXPECT_EQ("", coff_file_or_error.value()->GetBuildId());
}

TEST(CoffFile, GetLoadBiasAndExecutableSegmentOffsetAndImageSize) {
  {
    std::filesystem::path file_path = orbit_test::GetTestdataDir() / "dllmain.dll";
    auto coff_file_or_error = CreateCoffFile(file_path);
    ASSERT_THAT(coff_file_or_error, HasNoError());
    const CoffFile& coff_file = *coff_file_or_error.value();
    EXPECT_EQ(coff_file.GetLoadBias(), 0x180000000);
    EXPECT_EQ(coff_file.GetExecutableSegmentOffset(), 0x1000);
    EXPECT_EQ(coff_file.GetImageSize(), 0x10d000);
  }
  {
    std::filesystem::path file_path = orbit_test::GetTestdataDir() / "libtest.dll";
    auto coff_file_or_error = CreateCoffFile(file_path);
    ASSERT_THAT(coff_file_or_error, HasNoError());
    const CoffFile& coff_file = *coff_file_or_error.value();
    EXPECT_EQ(coff_file.GetLoadBias(), 0x62640000);
    EXPECT_EQ(coff_file.GetExecutableSegmentOffset(), 0x1000);
    EXPECT_EQ(coff_file.GetImageSize(), 0x20000);
  }
}

TEST(CoffFile, ObjectSegments) {
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "dllmain.dll";
  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());
  const std::vector<orbit_grpc_protos::ModuleInfo::ObjectSegment>& segments =
      coff_file_or_error.value()->GetObjectSegments();
  ASSERT_EQ(segments.size(), 8);

  EXPECT_EQ(segments[0].offset_in_file(), 0x400);
  EXPECT_EQ(segments[0].size_in_file(), 0xCEA00);
  EXPECT_EQ(segments[0].address(), 0x180001000);
  EXPECT_EQ(segments[0].size_in_memory(), 0xCE9E4);

  EXPECT_EQ(segments[1].offset_in_file(), 0xCEE00);
  EXPECT_EQ(segments[1].size_in_file(), 0x27A00);
  EXPECT_EQ(segments[1].address(), 0x1800D0000);
  EXPECT_EQ(segments[1].size_in_memory(), 0x2797D);

  EXPECT_EQ(segments[2].offset_in_file(), 0xF6800);
  EXPECT_EQ(segments[2].size_in_file(), 0x2800);
  EXPECT_EQ(segments[2].address(), 0x1800F8000);
  EXPECT_EQ(segments[2].size_in_memory(), 0x5269);

  EXPECT_EQ(segments[3].offset_in_file(), 0xF9000);
  EXPECT_EQ(segments[3].size_in_file(), 0x9000);
  EXPECT_EQ(segments[3].address(), 0x1800FE000);
  EXPECT_EQ(segments[3].size_in_memory(), 0x8F4C);

  EXPECT_EQ(segments[4].offset_in_file(), 0x102000);
  EXPECT_EQ(segments[4].size_in_file(), 0x1200);
  EXPECT_EQ(segments[4].address(), 0x180107000);
  EXPECT_EQ(segments[4].size_in_memory(), 0x1041);

  EXPECT_EQ(segments[5].offset_in_file(), 0x103200);
  EXPECT_EQ(segments[5].size_in_file(), 0x200);
  EXPECT_EQ(segments[5].address(), 0x180109000);
  EXPECT_EQ(segments[5].size_in_memory(), 0x151);

  EXPECT_EQ(segments[6].offset_in_file(), 0x103400);
  EXPECT_EQ(segments[6].size_in_file(), 0x400);
  EXPECT_EQ(segments[6].address(), 0x18010A000);
  EXPECT_EQ(segments[6].size_in_memory(), 0x222);

  EXPECT_EQ(segments[7].offset_in_file(), 0x103800);
  EXPECT_EQ(segments[7].size_in_file(), 0x1C00);
  EXPECT_EQ(segments[7].address(), 0x18010B000);
  EXPECT_EQ(segments[7].size_in_memory(), 0x1A78);
}

}  // namespace orbit_object_utils
