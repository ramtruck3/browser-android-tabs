// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/validating_combobox.h"

#include <utility>

#include "ui/base/models/combobox_model.h"

namespace payments {

ValidatingCombobox::ValidatingCombobox(
    std::unique_ptr<ui::ComboboxModel> model,
    std::unique_ptr<ValidationDelegate> delegate)
    : Combobox(std::move(model)), delegate_(std::move(delegate)) {
  // No need to remove observer on owned model.
  this->model()->AddObserver(this);
  SetFocusBehavior(FocusBehavior::ALWAYS);
}

ValidatingCombobox::~ValidatingCombobox() {}

void ValidatingCombobox::OnBlur() {
  Combobox::OnBlur();

  // Validations will occur when the content changes. Do not validate if the
  // view is being removed.
  if (!being_removed_) {
    Validate();
  }
}

void ValidatingCombobox::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  if (details.child == this && !details.is_add)
    being_removed_ = true;
}

void ValidatingCombobox::OnContentsChanged() {
  Validate();
}

void ValidatingCombobox::OnComboboxModelChanged(
    ui::ComboboxModel* unused_model) {
  ModelChanged();
  delegate_->ComboboxModelChanged(this);
}

bool ValidatingCombobox::IsValid() {
  base::string16 unused;
  return delegate_->IsValidCombobox(this, &unused);
}

void ValidatingCombobox::Validate() {
  // ComboboxValueChanged may have side-effects, such as displaying errors.
  SetInvalid(!delegate_->ComboboxValueChanged(this));
}

}  // namespace payments
