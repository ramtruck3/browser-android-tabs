// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/metadata/metadata_types.h"

#include <utility>

#include "ui/views/metadata/type_conversion.h"

namespace views {
namespace metadata {

ClassMetaData::ClassMetaData() = default;
ClassMetaData::~ClassMetaData() = default;

void ClassMetaData::AddMemberData(
    std::unique_ptr<MemberMetaDataBase> member_data) {
  members_.push_back(member_data.release());
}

MemberMetaDataBase* ClassMetaData::FindMemberData(
    const std::string& member_name) {
  for (MemberMetaDataBase* member_data : members_) {
    if (member_data->member_name() == member_name)
      return member_data;
  }

  if (parent_class_meta_data_ != nullptr)
    return parent_class_meta_data_->FindMemberData(member_name);

  return nullptr;
}

/** Member Iterator */
ClassMetaData::ClassMemberIterator::ClassMemberIterator(
    const ClassMetaData::ClassMemberIterator& other) {
  current_collection_ = other.current_collection_;
  current_vector_index_ = other.current_vector_index_;
}
ClassMetaData::ClassMemberIterator::~ClassMemberIterator() = default;

ClassMetaData::ClassMemberIterator::ClassMemberIterator(
    ClassMetaData* starting_container) {
  current_collection_ = starting_container;
  current_vector_index_ = (current_collection_ ? 0 : SIZE_MAX);
}

bool ClassMetaData::ClassMemberIterator::operator==(
    const ClassMemberIterator& rhs) const {
  return current_vector_index_ == rhs.current_vector_index_ &&
         current_collection_ == rhs.current_collection_;
}

ClassMetaData::ClassMemberIterator& ClassMetaData::ClassMemberIterator::
operator++() {
  IncrementHelper();
  return *this;
}

ClassMetaData::ClassMemberIterator ClassMetaData::ClassMemberIterator::
operator++(int) {
  ClassMetaData::ClassMemberIterator tmp(*this);
  IncrementHelper();
  return tmp;
}

void ClassMetaData::ClassMemberIterator::IncrementHelper() {
  DCHECK_LT(current_vector_index_, SIZE_MAX);
  ++current_vector_index_;

  if (current_vector_index_ >= current_collection_->members().size()) {
    do {
      current_collection_ = current_collection_->parent_class_meta_data();
      current_vector_index_ = (current_collection_ ? 0 : SIZE_MAX);
    } while (current_collection_ && current_collection_->members().empty());
  }
}

ClassMetaData::ClassMemberIterator ClassMetaData::begin() {
  return ClassMemberIterator(this);
}

ClassMetaData::ClassMemberIterator ClassMetaData::end() {
  return ClassMemberIterator(nullptr);
}

void ClassMetaData::SetTypeName(const std::string& type_name) {
  type_name_ = type_name;
}

}  // namespace metadata
}  // namespace views
