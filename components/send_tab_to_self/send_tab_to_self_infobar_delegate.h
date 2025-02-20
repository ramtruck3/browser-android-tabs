// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEND_TAB_TO_SELF_SEND_TAB_TO_SELF_INFOBAR_DELEGATE_H_
#define COMPONENTS_SEND_TAB_TO_SELF_SEND_TAB_TO_SELF_INFOBAR_DELEGATE_H_

#include <memory>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "components/infobars/core/infobar_delegate.h"

namespace send_tab_to_self {

class SendTabToSelfEntry;

// Delegate containing logic about what to display and how to behave
// in the SendTabToSelf infobar. Used across Android and iOS.
class SendTabToSelfInfoBarDelegate : public infobars::InfoBarDelegate {
 public:
  static std::unique_ptr<SendTabToSelfInfoBarDelegate> Create(
      const SendTabToSelfEntry* entry);
  ~SendTabToSelfInfoBarDelegate() override;

  // Returns the message to be shown in the infobar.
  base::string16 GetInfobarMessage() const;

  // Opens a tab to the url of the shared |entry_|.
  void OpenTab();

  // InfoBarDelegate:
  void InfoBarDismissed() override;
  infobars::InfoBarDelegate::InfoBarIdentifier GetIdentifier() const override;

 private:
  explicit SendTabToSelfInfoBarDelegate(const SendTabToSelfEntry* entry);

  // The entry that was share to this device. Must outlive this instance.
  const SendTabToSelfEntry* entry_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(SendTabToSelfInfoBarDelegate);
};

}  // namespace send_tab_to_self

#endif  // COMPONENTS_SEND_TAB_TO_SELF_SEND_TAB_TO_SELF_INFOBAR_DELEGATE_H_