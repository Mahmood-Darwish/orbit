// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OBJECT_UTILS_PDB_FILE_TEST_H_
#define OBJECT_UTILS_PDB_FILE_TEST_H_

#include <absl/container/flat_hash_map.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>

#include "ObjectUtils/CoffFile.h"
#include "ObjectUtils/PdbFile.h"
#include "OrbitBase/Logging.h"
#include "Test/Path.h"
#include "TestUtils/TestUtils.h"

using orbit_grpc_protos::SymbolInfo;
using orbit_object_utils::CreateCoffFile;
using orbit_object_utils::ObjectFileInfo;
using orbit_object_utils::PdbDebugInfo;
using orbit_object_utils::PdbFile;
using orbit_test_utils::HasError;
using orbit_test_utils::HasNoError;
using ::testing::AnyOf;
using ::testing::ElementsAre;

template <typename T>
class PdbFileTest : public testing::Test {
 public:
};

TYPED_TEST_SUITE_P(PdbFileTest);

TYPED_TEST_P(PdbFileTest, LoadDebugSymbols) {
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "dllmain.pdb";

  ErrorMessageOr<std::unique_ptr<PdbFile>> pdb_file_result =
      TypeParam::CreatePdbFile(file_path_pdb, ObjectFileInfo{0x180000000});
  ASSERT_THAT(pdb_file_result, HasNoError());
  std::unique_ptr<orbit_object_utils::PdbFile> pdb_file = std::move(pdb_file_result.value());
  auto symbols_result = pdb_file->LoadDebugSymbols();
  ASSERT_THAT(symbols_result, HasNoError());

  auto symbols = std::move(symbols_result.value());

  absl::flat_hash_map<uint64_t, const SymbolInfo*> symbol_infos_by_address;
  for (const SymbolInfo& symbol_info : symbols.symbol_infos()) {
    symbol_infos_by_address.emplace(symbol_info.address(), &symbol_info);
  }

  // TODO(kuebler): Reading public symbols is not yet implemented in the LLVM-based parser.
  auto test_suite_name = ::testing::UnitTest::GetInstance()->current_test_suite()->name();
  if (absl::StrContains(test_suite_name, "PdbFileLlvmTest")) {
    ASSERT_EQ(symbol_infos_by_address.size(), 5459);
  } else {
    ASSERT_EQ(symbol_infos_by_address.size(), 5552);
  }
  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000eea0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "PrintHelloWorldInternal()");
    EXPECT_EQ(symbol->address(), 0x18000eea0);
    EXPECT_EQ(symbol->size(), 0x2b);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000eee0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "PrintHelloWorld()");
    EXPECT_EQ(symbol->address(), 0x18000eee0);
    EXPECT_EQ(symbol->size(), 0xe);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000ef00];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "PrintString(const char*)");
    EXPECT_EQ(symbol->address(), 0x18000ef00);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000ef20];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesVolatileInt(volatile int)");
    EXPECT_EQ(symbol->address(), 0x18000ef20);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000ef50];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesFooReference(Foo&)");
    EXPECT_EQ(symbol->address(), 0x18000ef50);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000ef80];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesFooRValueReference(Foo&&)");
    EXPECT_EQ(symbol->address(), 0x18000ef80);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000efb0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesConstPtrToInt(int* const)");
    EXPECT_EQ(symbol->address(), 0x18000efb0);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000efe0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesReferenceToIntPtr(int*&)");
    EXPECT_EQ(symbol->address(), 0x18000efe0);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f010];
    ASSERT_NE(symbol, nullptr);
    // LLVM does not handle function pointers correctly, thus we also accept the wrong
    // strings here.
    EXPECT_THAT(symbol->demangled_name(), AnyOf("TakesVoidFunctionPointer(void (*)(int))",
                                                "TakesVoidFunctionPointer(void (int)*)"));
    EXPECT_EQ(symbol->address(), 0x18000f010);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f030];
    ASSERT_NE(symbol, nullptr);
    // LLVM does not handle function pointers correctly, thus we also accept the wrong
    // strings here.
    EXPECT_THAT(symbol->demangled_name(), AnyOf("TakesCharFunctionPointer(char (*)(int))",
                                                "TakesCharFunctionPointer(char (int)*)"));
    EXPECT_EQ(symbol->address(), 0x18000f030);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f060];
    ASSERT_NE(symbol, nullptr);
    // LLVM does not handle function pointers correctly, thus we also accept the wrong
    // strings here.
    EXPECT_THAT(symbol->demangled_name(),
                AnyOf("TakesMemberFunctionPointer(const char* (Foo::*)(int), Foo)",
                      "TakesMemberFunctionPointer(const char* Foo::(int) Foo::*, Foo)"));
    EXPECT_EQ(symbol->address(), 0x18000f060);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f090];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(),
              "TakesVolatilePointerToConstUnsignedChar(const unsigned char* volatile)");
    EXPECT_EQ(symbol->address(), 0x18000f090);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f0b0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(),
              "TakesVolatileConstPtrToVolatileConstChar(const volatile char* const volatile)");
    EXPECT_EQ(symbol->address(), 0x18000f0b0);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f0d0];
    ASSERT_NE(symbol, nullptr);
    // LLVM does not handle function pointers correctly, thus we also accept the wrong
    // strings here.
    EXPECT_THAT(symbol->demangled_name(),
                AnyOf("TakesConstPointerToConstFunctionPointer(char (* const* const)(int))",
                      "TakesConstPointerToConstFunctionPointer(char (int)* const* const)"));
    EXPECT_EQ(symbol->address(), 0x18000f0d0);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f100];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesVariableArguments(int, <no type>)");
    EXPECT_EQ(symbol->address(), 0x18000f100);
  }

  {
    const SymbolInfo* symbol = symbol_infos_by_address[0x18000f1b0];
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->demangled_name(), "TakesUserTypeInNamespace(A::FooA, A::B::FooAB)");
    EXPECT_EQ(symbol->address(), 0x18000f1b0);
  }
}

TYPED_TEST_P(PdbFileTest, CanObtainGuidAndAgeFromPdbAndDll) {
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "dllmain.pdb";

  ErrorMessageOr<std::unique_ptr<orbit_object_utils::PdbFile>> pdb_file_result =
      TypeParam::CreatePdbFile(file_path_pdb, ObjectFileInfo{0x180000000});
  ASSERT_THAT(pdb_file_result, HasNoError());
  std::unique_ptr<orbit_object_utils::PdbFile> pdb_file = std::move(pdb_file_result.value());

  // We load the PDB debug info from the dll to see if it matches the data in the pdb.
  std::filesystem::path file_path = orbit_test::GetTestdataDir() / "dllmain.dll";

  auto coff_file_or_error = CreateCoffFile(file_path);
  ASSERT_THAT(coff_file_or_error, HasNoError());

  auto pdb_debug_info_or_error = coff_file_or_error.value()->GetDebugPdbInfo();
  ASSERT_THAT(pdb_debug_info_or_error, HasNoError());

  EXPECT_EQ(pdb_file->GetAge(), pdb_debug_info_or_error.value().age);
  EXPECT_THAT(pdb_file->GetGuid(), testing::ElementsAreArray(pdb_debug_info_or_error.value().guid));

  EXPECT_EQ(pdb_file->GetBuildId(), coff_file_or_error.value()->GetBuildId());
}

TYPED_TEST_P(PdbFileTest, CreatePdbFailsOnNonPdbFile) {
  // Any non-PDB file can be used here.
  std::filesystem::path file_path_pdb = orbit_test::GetTestdataDir() / "dllmain.dll";

  ErrorMessageOr<std::unique_ptr<orbit_object_utils::PdbFile>> pdb_file_result =
      TypeParam::CreatePdbFile(file_path_pdb, ObjectFileInfo{0x180000000});
  EXPECT_THAT(pdb_file_result, HasError("Unable to load PDB file"));
}

REGISTER_TYPED_TEST_SUITE_P(PdbFileTest, LoadDebugSymbols, CanObtainGuidAndAgeFromPdbAndDll,
                            CreatePdbFailsOnNonPdbFile);

#endif  // OBJECT_UTILS_PDB_FILE_TEST_H_