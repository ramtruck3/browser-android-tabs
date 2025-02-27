// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_FIDO_VIRTUAL_CTAP2_DEVICE_H_
#define DEVICE_FIDO_VIRTUAL_CTAP2_DEVICE_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/component_export.h"
#include "base/containers/span.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/optional.h"
#include "components/cbor/values.h"
#include "device/fido/attested_credential_data.h"
#include "device/fido/authenticator_data.h"
#include "device/fido/authenticator_supported_options.h"
#include "device/fido/ctap_get_assertion_request.h"
#include "device/fido/ctap_make_credential_request.h"
#include "device/fido/fido_constants.h"
#include "device/fido/virtual_fido_device.h"

namespace device {

class VirtualU2fDevice;

class COMPONENT_EXPORT(DEVICE_FIDO) VirtualCtap2Device
    : public VirtualFidoDevice {
 public:
  struct COMPONENT_EXPORT(DEVICE_FIDO) Config {
    Config();

    // u2f_support, if true, makes this device a dual-protocol (i.e. CTAP2 and
    // U2F) device.
    bool u2f_support = false;
    bool pin_support = false;
    bool internal_uv_support = false;
    bool resident_key_support = false;
    // resident_credential_storage is the number of resident credentials that
    // the device will store before returning KEY_STORE_FULL.
    size_t resident_credential_storage = 3;
    // return_immediate_invalid_credential_error causes an INVALID_CREDENTIAL
    // error to be returned from GetAssertion, before getting a touch, when no
    // credentials are recognised. This behaviour is exhibited by some CTAP2
    // authenticators in the wild.
    bool return_immediate_invalid_credential_error = false;
    // return_attested_cred_data_in_get_assertion_response causes
    // attestedCredentialData to be included in the authenticator data of an
    // GetAssertion response.
    bool return_attested_cred_data_in_get_assertion_response = false;
    // reject_large_allow_and_exclude_lists causes the authenticator to respond
    // with an error if an allowList or an excludeList contains more than one
    // credential ID.
    bool reject_large_allow_and_exclude_lists = false;
    // reject_silent_authenticator_requests causes the authenticator to return
    // an error if a up=false assertion request is received.
    bool reject_silent_authentication_requests = false;
  };

  VirtualCtap2Device();
  explicit VirtualCtap2Device(scoped_refptr<State> state, const Config& config);
  ~VirtualCtap2Device() override;

  // FidoDevice:
  void Cancel(CancelToken) override;
  CancelToken DeviceTransact(std::vector<uint8_t> command,
                             DeviceCallback cb) override;
  base::WeakPtr<FidoDevice> GetWeakPtr() override;

  void SetAuthenticatorSupportedOptions(
      const AuthenticatorSupportedOptions& options);

 private:
  CtapDeviceResponseCode OnMakeCredential(base::span<const uint8_t> request,
                                          std::vector<uint8_t>* response);
  CtapDeviceResponseCode OnGetAssertion(base::span<const uint8_t> request,
                                        std::vector<uint8_t>* response);
  CtapDeviceResponseCode OnGetNextAssertion(base::span<const uint8_t> request,
                                            std::vector<uint8_t>* response);
  CtapDeviceResponseCode OnPINCommand(base::span<const uint8_t> request,
                                      std::vector<uint8_t>* response);

  CtapDeviceResponseCode OnAuthenticatorGetInfo(
      std::vector<uint8_t>* response) const;

  AttestedCredentialData ConstructAttestedCredentialData(
      std::vector<uint8_t> u2f_data,
      std::unique_ptr<PublicKey> public_key);
  AuthenticatorData ConstructAuthenticatorData(
      base::span<const uint8_t, kRpIdHashLength> rp_id_hash,
      bool user_verified,
      uint32_t current_signature_count,
      base::Optional<AttestedCredentialData> attested_credential_data,
      base::Optional<cbor::Value> extensions);

  std::unique_ptr<VirtualU2fDevice> u2f_device_;

  const Config config_;
  base::WeakPtrFactory<FidoDevice> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(VirtualCtap2Device);
};

// Decodes a CBOR-encoded CTAP2 authenticatorMakeCredential request message. The
// request's client_data_json() value will be empty, and the hashed client data
// is returned separately.
COMPONENT_EXPORT(DEVICE_FIDO)
base::Optional<std::pair<CtapMakeCredentialRequest,
                         CtapMakeCredentialRequest::ClientDataHash>>
ParseCtapMakeCredentialRequest(base::span<const uint8_t> request_bytes);

// Decodes a CBOR-encoded CTAP2 authenticatorGetAssertion request message. The
// request's client_data_json() value will be empty, and the hashed client data
// is returned separately.
COMPONENT_EXPORT(DEVICE_FIDO)
base::Optional<
    std::pair<CtapGetAssertionRequest, CtapGetAssertionRequest::ClientDataHash>>
ParseCtapGetAssertionRequest(base::span<const uint8_t> request_bytes);

}  // namespace device

#endif  // DEVICE_FIDO_VIRTUAL_CTAP2_DEVICE_H_
