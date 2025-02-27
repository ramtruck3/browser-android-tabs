// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NETWORK_ERROR_LOGGING_MOCK_PERSISTENT_NEL_STORE_H_
#define NET_NETWORK_ERROR_LOGGING_MOCK_PERSISTENT_NEL_STORE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "net/network_error_logging/network_error_logging_service.h"
#include "url/origin.h"

namespace net {

// A NetworkErrorLoggingService::PersistentNELStore implementation that stashes
// the received commands in order in a vector, to be checked by tests.
// Simulates loading pre-existing stored policies, which can be provided
// using SetLoadExpectation().
class MockPersistentNELStore
    : public NetworkErrorLoggingService::PersistentNELStore {
 public:
  // Represents a command that has been passed to the MockPersistentNELStore.
  struct Command {
    enum class Type {
      LOAD_NEL_POLICIES,
      ADD_NEL_POLICY,
      UPDATE_NEL_POLICY,
      DELETE_NEL_POLICY,
      FLUSH
    };

    // Constructor for LOAD_NEL_POLICIES commands.
    Command(Type type, NELPoliciesLoadedCallback loaded_callback);
    // Constructor for ADD_NEL_POLICY, UPDATE_NEL_POLICY, and DELETE_NEL_POLICY
    // commands.
    Command(Type type, const NetworkErrorLoggingService::NELPolicy& policy);
    // Constructor for FLUSH commands.
    Command(Type type);

    Command(const Command& other);
    Command(Command&& other);

    ~Command();

    // Type of command.
    Type type;

    // The origin of the policy that the command pertains to. (Only applies for
    // add, update, and delete)
    url::Origin origin;

    // The supplied callback to be run when loading is complete. (Only applies
    // for load commands).
    NELPoliciesLoadedCallback loaded_callback;
  };

  using CommandList = std::vector<Command>;

  MockPersistentNELStore();
  ~MockPersistentNELStore() override;

  // PersistentNELStore implementation:
  void LoadNELPolicies(NELPoliciesLoadedCallback loaded_callback) override;
  void AddNELPolicy(
      const NetworkErrorLoggingService::NELPolicy& policy) override;
  void UpdateNELPolicyAccessTime(
      const NetworkErrorLoggingService::NELPolicy& policy) override;
  void DeleteNELPolicy(
      const NetworkErrorLoggingService::NELPolicy& policy) override;
  void Flush() override;

  // Simulates pre-existing policies that were stored previously. Should only be
  // called once, at the beginning of the test before any other method calls.
  void SetPrestoredPolicies(
      std::vector<NetworkErrorLoggingService::NELPolicy> policies);

  // Simulate finishing loading policies by executing the loaded_callback of the
  // first LOAD_NEL_POLICIES command (which should also be the only
  // LOAD_NEL_POLICIES command). If |load_success| is false, the vector of
  // policies passed to the callback will be empty.  If |load_success| is true,
  // the vector of policies passed to the callback will be
  // |prestored_policies_|.
  void FinishLoading(bool load_success);

  // Verify that |command_list_| matches |expected_commands|.
  bool VerifyCommands(const CommandList& expected_commands) const;

  CommandList GetAllCommands() const;

  // Returns the total number of policies that would be stored in the store, if
  // this were a real store.
  int StoredPoliciesCount() const { return policy_count_; }

  // Generates a string with the list of commands, for ease of debugging.
  std::string GetDebugString() const;

 private:
  // List of commands that we have received so far.
  CommandList command_list_;

  // Simulated pre-existing stored policies.
  std::vector<NetworkErrorLoggingService::NELPolicy> prestored_policies_;

  // Set when LoadNELPolicies() is called.
  bool load_started_;

  // Simulates the total number of policies that would be stored in the store.
  // Updated when pre-stored policies are added, and when Flush() is called.
  int policy_count_;

  // Simulates the delta to be added to |policy_count_| the next time Flush() is
  // called. Reset to 0 when Flush() is called.
  int queued_policy_count_delta_;

  DISALLOW_COPY_AND_ASSIGN(MockPersistentNELStore);
};

bool operator==(const MockPersistentNELStore::Command& lhs,
                const MockPersistentNELStore::Command& rhs);
bool operator!=(const MockPersistentNELStore::Command& lhs,
                const MockPersistentNELStore::Command& rhs);

}  // namespace net

#endif  // NET_NETWORK_ERROR_LOGGING_MOCK_PERSISTENT_NEL_STORE_H_
