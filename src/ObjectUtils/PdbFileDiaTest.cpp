// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "PdbFileTest.h"

// Must be included after PdbFileTest.h
#include "PdbFileDia.h"

using orbit_object_utils::PdbFileDia;

INSTANTIATE_TYPED_TEST_SUITE_P(PdbFileDiaTest, PdbFileTest, ::testing::Types<PdbFileDia>);

// This test is specific to using the DIA SDK to load PDB files.
TEST(PdbFileDiaTest, CreatePdbDoesNotFailOnCoInitializeWhenAlreadyInitialized) {
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "dllmain.pdb";
  HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  ASSERT_TRUE(result == S_OK || result == S_FALSE);
  ErrorMessageOr<std::unique_ptr<PdbFile>> pdb_file_result1 =
      PdbFileDia::CreatePdbFile(file_path_pdb, ObjectFileInfo{0x180000000});
  ASSERT_THAT(pdb_file_result1, HasNoError());
  CoUninitialize();
}

// This test is specific to using the DIA SDK to load PDB files.
TEST(PdbFileDiaTest, PdbFileProperlyUninitializesComLibrary) {
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "dllmain.pdb";
  {
    ErrorMessageOr<std::unique_ptr<PdbFile>> pdb_file_result =
        PdbFileDia::CreatePdbFile(file_path_pdb, ObjectFileInfo{0x180000000});
    ASSERT_THAT(pdb_file_result, HasNoError());
  }

  HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  // This would be S_FALSE if the PdbFileDia class didn't properly balance its CoInitialize() call.
  ASSERT_EQ(result, S_OK);
  CoUninitialize();
}

TEST(PdbFileDiaTest, LoadsFunctionsOnlyInPublicSymbols) {
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "libomp.dll.pdb";

  ErrorMessageOr<std::unique_ptr<PdbFile>> pdb_file_result =
      PdbFileDia::CreatePdbFile(file_path_pdb, ObjectFileInfo{0});
  ASSERT_THAT(pdb_file_result, HasNoError());
  std::unique_ptr<orbit_object_utils::PdbFile> pdb_file = std::move(pdb_file_result.value());
  auto symbols_result = pdb_file->LoadDebugSymbols();
  ASSERT_THAT(symbols_result, HasNoError());

  auto symbols = std::move(symbols_result.value());

  absl::flat_hash_map<uint64_t, const SymbolInfo*> symbol_infos_by_address;
  for (const SymbolInfo& symbol_info : symbols.symbol_infos()) {
    symbol_infos_by_address.emplace(symbol_info.address(), &symbol_info);
  }

  ASSERT_EQ(symbol_infos_by_address.size(), 6868);

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x0F187B];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "FormatMessageW");
    EXPECT_EQ(symbol->address(), 0xF187B);
    EXPECT_EQ(symbol->size(), 6);
  }
}
