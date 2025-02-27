// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/device_sync/cryptauth_key_registry.h"

namespace chromeos {

namespace device_sync {

CryptAuthKeyRegistry::CryptAuthKeyRegistry() = default;

CryptAuthKeyRegistry::~CryptAuthKeyRegistry() = default;

const CryptAuthKeyRegistry::KeyBundleMap&
CryptAuthKeyRegistry::enrolled_key_bundles() const {
  return enrolled_key_bundles_;
}

const CryptAuthKeyBundle* CryptAuthKeyRegistry::GetKeyBundle(
    CryptAuthKeyBundle::Name name) const {
  auto it = enrolled_key_bundles_.find(name);
  if (it == enrolled_key_bundles_.end())
    return nullptr;

  return &it->second;
}

const CryptAuthKey* CryptAuthKeyRegistry::GetActiveKey(
    CryptAuthKeyBundle::Name name) const {
  auto it_bundle = enrolled_key_bundles_.find(name);
  if (it_bundle == enrolled_key_bundles_.end())
    return nullptr;

  return it_bundle->second.GetActiveKey();
}

void CryptAuthKeyRegistry::AddEnrolledKey(CryptAuthKeyBundle::Name name,
                                          const CryptAuthKey& key) {
  auto it_bundle = enrolled_key_bundles_.find(name);

  // If a bundle with |name| does not already exist, create one.
  if (it_bundle == enrolled_key_bundles_.end()) {
    auto it_success_pair = enrolled_key_bundles_.try_emplace(name, name);
    DCHECK(it_success_pair.second);

    it_bundle = it_success_pair.first;
  }

  it_bundle->second.AddKey(key);

  OnKeyRegistryUpdated();
}

void CryptAuthKeyRegistry::SetActiveKey(CryptAuthKeyBundle::Name name,
                                        const std::string& handle) {
  auto it_bundle = enrolled_key_bundles_.find(name);
  DCHECK(it_bundle != enrolled_key_bundles_.end());

  it_bundle->second.SetActiveKey(handle);

  OnKeyRegistryUpdated();
}

void CryptAuthKeyRegistry::DeactivateKeys(CryptAuthKeyBundle::Name name) {
  auto it_bundle = enrolled_key_bundles_.find(name);
  DCHECK(it_bundle != enrolled_key_bundles_.end());

  it_bundle->second.DeactivateKeys();

  OnKeyRegistryUpdated();
}

void CryptAuthKeyRegistry::DeleteKey(CryptAuthKeyBundle::Name name,
                                     const std::string& handle) {
  auto it_bundle = enrolled_key_bundles_.find(name);
  DCHECK(it_bundle != enrolled_key_bundles_.end());

  it_bundle->second.DeleteKey(handle);

  OnKeyRegistryUpdated();
}

void CryptAuthKeyRegistry::SetKeyDirective(
    CryptAuthKeyBundle::Name name,
    const cryptauthv2::KeyDirective& key_directive) {
  auto it_bundle = enrolled_key_bundles_.find(name);

  // If a bundle with |name| does not already exist, create one.
  if (it_bundle == enrolled_key_bundles_.end()) {
    auto it_success_pair = enrolled_key_bundles_.try_emplace(name, name);
    DCHECK(it_success_pair.second);
    it_bundle = it_success_pair.first;
  }

  it_bundle->second.set_key_directive(key_directive);

  OnKeyRegistryUpdated();
}

}  // namespace device_sync

}  // namespace chromeos
