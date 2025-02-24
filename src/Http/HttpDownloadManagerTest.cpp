// Copyright (c) 2022 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaEnum>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <memory>

#include "Http/HttpDownloadManager.h"
#include "OrbitBase/CanceledOr.h"
#include "OrbitBase/File.h"
#include "OrbitBase/Future.h"
#include "OrbitBase/Promise.h"
#include "OrbitBase/Result.h"
#include "OrbitBase/StopSource.h"
#include "OrbitBase/TemporaryFile.h"
#include "OrbitBase/WhenAll.h"
#include "QtUtils/MainThreadExecutorImpl.h"
#include "Test/Path.h"
#include "TestUtils/TestUtils.h"

namespace orbit_http {
using orbit_base::CanceledOr;
using orbit_base::FileExists;
using orbit_base::Future;
using orbit_base::IsCanceled;
using orbit_base::StopSource;
using orbit_base::TemporaryFile;
using orbit_test_utils::HasError;
using orbit_test_utils::HasNoError;

namespace {

[[nodiscard]] static TemporaryFile GetTemporaryFile() {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  EXPECT_THAT(temporary_file_or_error, HasNoError());
  return std::move(temporary_file_or_error.value());
}

class HttpDownloadManagerTest : public ::testing::Test {
 protected:
  HttpDownloadManagerTest()
      : executor_(orbit_qt_utils::MainThreadExecutorImpl::Create()),
        manager_(new HttpDownloadManager) {
#ifdef _WIN32
    local_http_server_process_.setProgram("py");
    local_http_server_process_.setArguments(
        QStringList{"-3", "-m", R"(http.server)", "--bind", "localhost", "--directory",
                    QString::fromStdString(orbit_test::GetTestdataDir().string()), "0"});
#else
    local_http_server_process_.setProgram("python3");
    local_http_server_process_.setArguments(
        QStringList{"-m", R"(http.server)", "--bind", "localhost", "--directory",
                    QString::fromStdString(orbit_test::GetTestdataDir().string()), "0"});
#endif

    QProcessEnvironment current_env = local_http_server_process_.processEnvironment();
    current_env.insert("PYTHONUNBUFFERED", "true");
    local_http_server_process_.setProcessEnvironment(current_env);

    ORBIT_LOG("Execute command:\n\"%s %s\"\n", local_http_server_process_.program().toStdString(),
              local_http_server_process_.arguments().join(" ").toStdString());

    QEventLoop loop{};
    QObject::connect(&local_http_server_process_, &QProcess::readyReadStandardOutput,
                     [&loop, this]() {
                       const QString prefix = "Serving HTTP on";
                       QString std_output = local_http_server_process_.readAllStandardOutput();
                       if (!std_output.contains(prefix)) return;

                       QRegularExpression portRegex("port ([0-9]+)");
                       QRegularExpressionMatch portMatch = portRegex.match(std_output);
                       if (portMatch.hasMatch()) {
                         port_ = portMatch.captured(1);
                         loop.quit();
                       }
                     });

    QObject::connect(&local_http_server_process_, &QProcess::errorOccurred,
                     [&loop, this](QProcess::ProcessError error) {
                       if (error == QProcess::Crashed) return;
                       ORBIT_LOG("Error while executing process.\nError:\n%s,\nDetails:\n%s.\n",
                                 QMetaEnum::fromType<QProcess::ProcessError>().valueToKey(error),
                                 local_http_server_process_.errorString().toStdString());
                       if (loop.isRunning()) loop.quit();
                     });

    local_http_server_process_.start();
    loop.exec();
  }

  ~HttpDownloadManagerTest() override {
    if (manager_) manager_->~HttpDownloadManager();
  }

  [[nodiscard]] std::string GetUrl(std::string filename) const {
    return absl::StrFormat("http://localhost:%s/%s", port_.toStdString(), filename);
  }

  std::shared_ptr<orbit_qt_utils::MainThreadExecutorImpl> executor_;
  QPointer<HttpDownloadManager> manager_;

 private:
  QProcess local_http_server_process_;
  QString port_;
};
}  // namespace

TEST_F(HttpDownloadManagerTest, DownloadSingleSucceeded) {
  std::string valid_url = GetUrl("dllmain.dll");
  TemporaryFile temporary_file = GetTemporaryFile();
  std::filesystem::path local_path = temporary_file.file_path();
  temporary_file.CloseAndRemove();
  StopSource stop_source{};

  auto future = manager_->Download(valid_url, local_path, stop_source.GetStopToken());
  future.Then(executor_.get(), [&local_path](ErrorMessageOr<CanceledOr<void>> result) {
    EXPECT_THAT(result, HasNoError());
    EXPECT_FALSE(IsCanceled(result.value()));

    auto exists_or_error = FileExists(local_path);
    ASSERT_THAT(exists_or_error, HasNoError());
    EXPECT_TRUE(exists_or_error.value());
    QCoreApplication::exit();
  });

  QCoreApplication::exec();
}

TEST_F(HttpDownloadManagerTest, DownloadSingleCanceled) {
  std::string valid_url = GetUrl("dllmain.dll");
  TemporaryFile temporary_file = GetTemporaryFile();
  std::filesystem::path local_path = temporary_file.file_path();
  temporary_file.CloseAndRemove();
  StopSource stop_source{};

  stop_source.RequestStop();

  auto future = manager_->Download(valid_url, local_path, stop_source.GetStopToken());
  future.Then(executor_.get(), [](ErrorMessageOr<CanceledOr<void>> result) {
    EXPECT_THAT(result, HasNoError());
    EXPECT_TRUE(IsCanceled(result.value()));
    QCoreApplication::exit();
  });

  QCoreApplication::exec();
}

TEST_F(HttpDownloadManagerTest, DownloadSingleInvalidUrl) {
  std::string invalid_url = GetUrl("non_exist.dll");
  TemporaryFile temporary_file = GetTemporaryFile();
  std::filesystem::path local_path = temporary_file.file_path();
  temporary_file.CloseAndRemove();
  StopSource stop_source{};

  auto future = manager_->Download(invalid_url, local_path, stop_source.GetStopToken());
  future.Then(executor_.get(), [](ErrorMessageOr<CanceledOr<void>> result) {
    EXPECT_THAT(result, HasError("File not found"));
    QCoreApplication::exit();
  });

  QCoreApplication::exec();
}

TEST_F(HttpDownloadManagerTest, DownloadSingleInvalidSaveFilePath) {
  std::string invalid_url = GetUrl("non_exist.dll");
  std::filesystem::path local_path = "invalid/local/saving/path/non_exist.dll";
  StopSource stop_source{};

  auto future = manager_->Download(invalid_url, local_path, stop_source.GetStopToken());
  future.Then(executor_.get(), [](ErrorMessageOr<CanceledOr<void>> result) {
    EXPECT_THAT(result, HasError("Failed to open save file"));
    QCoreApplication::exit();
  });

  QCoreApplication::exec();
}

TEST_F(HttpDownloadManagerTest, DownloadMultipleSucceeded) {
  constexpr size_t kDownloadCounts = 3;
  const std::array<std::string, kDownloadCounts> kURLs = {
      GetUrl("dllmain.dll"), GetUrl("non_exist.dll"), GetUrl("hello_world_elf")};
  std::array<TemporaryFile, kDownloadCounts> kTemporaryFiles{GetTemporaryFile(), GetTemporaryFile(),
                                                             GetTemporaryFile()};
  std::array<StopSource, kDownloadCounts> stop_sources{};

  std::vector<Future<ErrorMessageOr<CanceledOr<void>>>> futures;
  futures.reserve(kDownloadCounts);
  for (size_t i = 0; i < kDownloadCounts; ++i) {
    kTemporaryFiles[i].CloseAndRemove();
    auto future = manager_->Download(kURLs[i], kTemporaryFiles[i].file_path(),
                                     stop_sources[i].GetStopToken());
    futures.emplace_back(std::move(future));
  }

  orbit_base::WhenAll(absl::MakeConstSpan(futures))
      .Then(executor_.get(),
            [&kTemporaryFiles](std::vector<ErrorMessageOr<CanceledOr<void>>> results) {
              EXPECT_THAT(results[0], HasNoError());
              EXPECT_THAT(results[1], HasError("File not found"));
              EXPECT_THAT(results[2], HasNoError());

              auto exists_or_error = FileExists(kTemporaryFiles[0].file_path());
              ASSERT_THAT(exists_or_error, HasNoError());
              EXPECT_TRUE(exists_or_error.value());

              exists_or_error = FileExists(kTemporaryFiles[2].file_path());
              ASSERT_THAT(exists_or_error, HasNoError());
              EXPECT_TRUE(exists_or_error.value());

              QCoreApplication::exit();
            });

  QCoreApplication::exec();
}

TEST_F(HttpDownloadManagerTest, DownloadSingleDestroyManagerEarly) {
  std::string valid_url = GetUrl("dllmain.dll");
  TemporaryFile temporary_file = GetTemporaryFile();
  std::filesystem::path local_path = temporary_file.file_path();
  temporary_file.CloseAndRemove();
  StopSource stop_source{};

  auto future = manager_->Download(valid_url, local_path, stop_source.GetStopToken());
  manager_->~HttpDownloadManager();
  future.Then(executor_.get(), [](ErrorMessageOr<CanceledOr<void>> result) {
    EXPECT_THAT(result, HasNoError());
    EXPECT_TRUE(IsCanceled(result.value()));
    QCoreApplication::exit();
  });

  QCoreApplication::exec();
}

}  // namespace orbit_http