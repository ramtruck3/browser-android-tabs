// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_CHROME_CLEANER_ENGINES_TARGET_ENGINE_REQUESTS_PROXY_H_
#define CHROME_CHROME_CLEANER_ENGINES_TARGET_ENGINE_REQUESTS_PROXY_H_

#include <windows.h>

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/chrome_cleaner/engines/target/sandbox_request_helper.h"
#include "chrome/chrome_cleaner/interfaces/engine_requests.mojom.h"
#include "chrome/chrome_cleaner/os/task_scheduler.h"

namespace chrome_cleaner {

// Accessors to send the scanning requests over the Mojo connection.
class EngineRequestsProxy
    : public base::RefCountedThreadSafe<EngineRequestsProxy> {
 public:
  EngineRequestsProxy(mojom::EngineRequestsAssociatedPtr engine_requests_ptr,
                      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  scoped_refptr<base::SingleThreadTaskRunner> task_runner() const {
    return task_runner_;
  }

  void UnbindRequestsPtr();

  // Implements synchronous callbacks to be called on arbitrary threads.
  virtual bool GetKnownFolderPath(mojom::KnownFolder folder_id,
                                  base::FilePath* folder_path);
  virtual bool GetProcesses(std::vector<base::ProcessId>* processes);
  virtual bool GetTasks(std::vector<TaskScheduler::TaskInfo>* task_list);
  virtual bool GetProcessImagePath(base::ProcessId pid,
                                   base::FilePath* image_path);
  virtual bool GetLoadedModules(base::ProcessId pid,
                                std::vector<base::string16>* modules);
  virtual bool GetProcessCommandLine(base::ProcessId pid,
                                     base::string16* command_line);
  virtual bool GetUserInfoFromSID(const SID* const sid,
                                  mojom::UserInformation* user_info);
  virtual uint32_t OpenReadOnlyRegistry(HANDLE root_key,
                                        const base::string16& sub_key,
                                        uint32_t dw_access,
                                        HANDLE* registry_handle);
  virtual uint32_t NtOpenReadOnlyRegistry(HANDLE root_key,
                                          const String16EmbeddedNulls& sub_key,
                                          uint32_t dw_access,
                                          HANDLE* registry_handle);

 protected:
  virtual ~EngineRequestsProxy();

 private:
  friend class base::RefCountedThreadSafe<EngineRequestsProxy>;
  friend class TestEngineRequestInvoker;

  // Invokes the desired function call from the IPC thread.
  MojoCallStatus SandboxGetKnownFolderPath(
      mojom::KnownFolder folder_id,
      mojom::EngineRequests::SandboxGetKnownFolderPathCallback result_callback);
  MojoCallStatus SandboxGetProcesses(
      mojom::EngineRequests::SandboxGetProcessesCallback result_callback);
  MojoCallStatus SandboxGetTasks(
      mojom::EngineRequests::SandboxGetTasksCallback result_callback);
  MojoCallStatus SandboxGetProcessImagePath(
      base::ProcessId pid,
      mojom::EngineRequests::SandboxGetProcessImagePathCallback
          result_callback);
  MojoCallStatus SandboxGetLoadedModules(
      base::ProcessId pid,
      mojom::EngineRequests::SandboxGetLoadedModulesCallback result_callback);
  MojoCallStatus SandboxGetProcessCommandLine(
      base::ProcessId pid,
      mojom::EngineRequests::SandboxGetProcessCommandLineCallback
          result_callback);
  MojoCallStatus SandboxGetUserInfoFromSID(
      const SID* const sid,
      mojom::EngineRequests::SandboxGetUserInfoFromSIDCallback result_callback);
  MojoCallStatus SandboxOpenReadOnlyRegistry(
      HANDLE root_key,
      const base::string16& sub_key,
      uint32_t dw_access,
      mojom::EngineRequests::SandboxOpenReadOnlyRegistryCallback
          result_callback);
  MojoCallStatus SandboxNtOpenReadOnlyRegistry(
      HANDLE root_key,
      const String16EmbeddedNulls& sub_key,
      uint32_t dw_access,
      mojom::EngineRequests::SandboxNtOpenReadOnlyRegistryCallback
          result_callback);

  // A EngineRequests that will send the requests over the Mojo
  // connection.
  mojom::EngineRequestsAssociatedPtr engine_requests_ptr_;

  // A task runner for the IPC thread.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
};

}  // namespace chrome_cleaner

#endif  // CHROME_CHROME_CLEANER_ENGINES_TARGET_ENGINE_REQUESTS_PROXY_H_
