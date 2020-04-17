/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <uv.h>

#include <tensorpipe/common/callback.h>
#include <tensorpipe/common/defs.h>

namespace tensorpipe {
namespace transport {
namespace uv {

class Connection;
class Listener;

class Loop final {
 public:
  Loop();

  // Prefer using deferToLoop over runInLoop when you don't need to wait for the
  // result.
  template <typename F>
  void runInLoop(F&& fn) {
    // When called from the event loop thread itself (e.g., from a callback),
    // deferring would cause a deadlock because the given callable can only be
    // run when the loop is allowed to proceed. On the other hand, it means it
    // is thread-safe to run it immediately. The danger here however is that it
    // can lead to an inconsistent order between operations run from the event
    // loop, from outside of it, and deferred.
    if (std::this_thread::get_id() == thread_.get_id()) {
      fn();
    } else {
      // Must use a copyable wrapper around std::promise because
      // we use it from a std::function which must be copyable.
      auto promise = std::make_shared<std::promise<void>>();
      auto future = promise->get_future();
      deferToLoop([promise, fn{std::forward<F>(fn)}]() {
        try {
          fn();
          promise->set_value();
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
      future.get();
    }
  }

  void deferToLoop(std::function<void()> fn);

  inline bool inLoopThread() {
    return std::this_thread::get_id() == thread_.get_id();
  }

  uv_loop_t* ptr() {
    return loop_.get();
  }

  void close();

  void join();

  ~Loop() noexcept;

 private:
  std::mutex mutex_;
  std::thread thread_;
  std::unique_ptr<uv_loop_t> loop_;
  std::unique_ptr<uv_async_t> async_;
  std::atomic<bool> closed_{false};
  std::atomic<bool> joined_{false};
  ClosingEmitter closingEmitter_;

  // Wake up the event loop.
  void wakeup();

  // Event loop thread entry function.
  void loop();

  // List of deferred functions to run when the loop is ready.
  std::vector<std::function<void()>> fns_;

  // This function is called by the event loop thread whenever
  // we have to run a number of deferred functions.
  static void uv__async_cb(uv_async_t* handle);

  // Companion function to uv__async_cb as member function
  // on the loop class.
  void runFunctionsFromLoop();

  friend class Connection;
  friend class Listener;
};

} // namespace uv
} // namespace transport
} // namespace tensorpipe
