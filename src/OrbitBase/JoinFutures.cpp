// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitBase/JoinFutures.h"

#include <memory>

#include "OrbitBase/Future.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/Promise.h"
#include "absl/synchronization/mutex.h"

namespace orbit_base {
namespace {
struct SharedStateJoin {
  Promise<void> promise;
  absl::Mutex mutex;
  int incompleted_futures;
};
}  // namespace

Future<void> JoinFutures(absl::Span<const Future<void>> futures) {
  if (futures.empty()) {
    Promise<void> promise;
    promise.MarkFinished();
    return promise.GetFuture();
  }

  auto shared_state = std::make_shared<SharedStateJoin>();
  shared_state->incompleted_futures = static_cast<int>(futures.size());

  for (const auto& future : futures) {
    CHECK(future.IsValid());
    orbit_base::FutureRegisterContinuationResult const result =
        future.RegisterContinuation([shared_state]() {
          absl::MutexLock lock{&shared_state->mutex};
          --shared_state->incompleted_futures;

          if (shared_state->incompleted_futures == 0) {
            shared_state->promise.MarkFinished();
          }
        });

    if (result != orbit_base::FutureRegisterContinuationResult::kSuccessfullyRegistered) {
      absl::MutexLock lock{&shared_state->mutex};
      --shared_state->incompleted_futures;
    }
  }

  {
    absl::MutexLock lock{&shared_state->mutex};
    if (shared_state->incompleted_futures == 0) {
      shared_state->promise.MarkFinished();
    }
  }

  return shared_state->promise.GetFuture();
}
}  // namespace orbit_base
