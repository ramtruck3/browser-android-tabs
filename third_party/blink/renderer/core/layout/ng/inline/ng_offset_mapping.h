// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGOffsetMapping_h
#define NGOffsetMapping_h

#include "base/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/editing/forward.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_caret_navigator.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/allocator.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class LayoutBlockFlow;
class LayoutObject;

enum class NGOffsetMappingUnitType { kIdentity, kCollapsed, kExpanded };

// An NGOffsetMappingUnit indicates a "simple" offset mapping between dom offset
// range [dom_start, dom_end] on node |owner| and text content offset range
// [text_content_start, text_content_end]. The mapping between them falls in one
// of the following categories, depending on |type|:
// - kIdentity: The mapping between the two ranges is the identity mapping. In
//   other words, the two ranges have the same length, and the offsets are
//   mapped one-to-one.
// - kCollapsed: The mapping is collapsed, namely, |text_content_start| and
//   |text_content_end| are the same, and characters in the dom range are
//   collapsed.
// - kExpanded: The mapping is expanded, namely, |dom_end == dom_start + 1|, and
//   |text_content_end > text_content_start + 1|, indicating that the character
//   in the dom range is expanded into multiple characters.
// See design doc https://goo.gl/CJbxky for details.
class CORE_EXPORT NGOffsetMappingUnit {
  DISALLOW_NEW();

 public:
  NGOffsetMappingUnit(NGOffsetMappingUnitType,
                      const LayoutObject&,
                      unsigned dom_start,
                      unsigned dom_end,
                      unsigned text_content_start,
                      unsigned text_content_end);
  ~NGOffsetMappingUnit();

  // Returns associated node for this unit or null if this unit is associated
  // to generated content.
  const Node* AssociatedNode() const;
  NGOffsetMappingUnitType GetType() const { return type_; }
  const LayoutObject& GetLayoutObject() const { return *layout_object_; }
  // Returns |Node| for this unit. If this unit comes from CSS generated
  // content, we can't use this function.
  // TODO(yosin): We should rename |GetOwner()| to |NonPseudoNode()|.
  const Node& GetOwner() const;
  unsigned DOMStart() const { return dom_start_; }
  unsigned DOMEnd() const { return dom_end_; }
  unsigned TextContentStart() const { return text_content_start_; }
  unsigned TextContentEnd() const { return text_content_end_; }

  // If the passed unit can be concatenated to |this| to create a bigger unit,
  // replaces |this| by the result and returns true; Returns false otherwise.
  bool Concatenate(const NGOffsetMappingUnit&);

  unsigned ConvertDOMOffsetToTextContent(unsigned) const;

  unsigned ConvertTextContentToFirstDOMOffset(unsigned) const;
  unsigned ConvertTextContentToLastDOMOffset(unsigned) const;

 private:
  NGOffsetMappingUnitType type_ = NGOffsetMappingUnitType::kIdentity;

  const LayoutObject* layout_object_;
  // TODO(yosin): We should rename |dom_start_| and |dom_end_| to appropriate
  // names since |layout_object_| is for generated text, these offsets are
  // offset in |LayoutText::text_| instead of DOM node.
  unsigned dom_start_;
  unsigned dom_end_;
  unsigned text_content_start_;
  unsigned text_content_end_;

  friend class NGOffsetMappingBuilder;
};

class NGMappingUnitRange {
  STACK_ALLOCATED();

 public:
  const NGOffsetMappingUnit* begin() const { return begin_; }
  const NGOffsetMappingUnit* end() const { return end_; }

  NGMappingUnitRange() : begin_(nullptr), end_(nullptr) {}
  NGMappingUnitRange(const NGOffsetMappingUnit* begin,
                     const NGOffsetMappingUnit* end)
      : begin_(begin), end_(end) {}

 private:
  const NGOffsetMappingUnit* begin_;
  const NGOffsetMappingUnit* end_;
};

// Each inline formatting context laid out with LayoutNG has an NGOffsetMapping
// object that stores the mapping information between DOM positions and offsets
// in the text content string of the context.
// See design doc https://goo.gl/CJbxky for details.
class CORE_EXPORT NGOffsetMapping {
  USING_FAST_MALLOC(NGOffsetMapping);

 public:
  using UnitVector = Vector<NGOffsetMappingUnit>;
  using RangeMap =
      HashMap<Persistent<const Node>, std::pair<unsigned, unsigned>>;

  NGOffsetMapping(UnitVector&&, RangeMap&&, String);
  ~NGOffsetMapping();

  const UnitVector& GetUnits() const { return units_; }
  const RangeMap& GetRanges() const { return ranges_; }
  const String& GetText() const { return text_; }

  // ------ Static getters for offset mapping objects  ------

  // TODO(xiaochengh): Unify the following getters and make them work on both
  // legacy and LayoutNG.

  // NGOffsetMapping APIs only accept the following positions:
  // 1. Offset-in-anchor in a text node;
  // 2. Before/After-anchor of an atomic inline or a text-like node like <br>.
  static bool AcceptsPosition(const Position&);

  // Returns the mapping object of the inline formatting context laying out the
  // given position.
  static const NGOffsetMapping* GetFor(const Position&);

  // Returns the mapping object of the inline formatting context containing the
  // given LayoutObject, if it's laid out with LayoutNG. If the LayoutObject is
  // itself an inline formatting context, returns its own offset mapping object.
  // This makes the retrieval of the mapping object easier when we already have
  // a LayoutObject at hand.
  static const NGOffsetMapping* GetFor(const LayoutObject*);

  // Returns the inline formatting context (which is a block flow) where the
  // given object is laid out -- this is the block flow whose offset mapping
  // contains the given object. Note that the object can be in either legacy or
  // NG layout, while NGOffsetMapping is supported on both of them.
  static LayoutBlockFlow* GetInlineFormattingContextOf(const LayoutObject&);

  // Variants taking position instead of |LayoutObject|.
  static LayoutBlockFlow* GetInlineFormattingContextOf(const Position&);

  // ------ Mapping APIs from DOM to text content ------

  // Returns the NGOffsetMappingUnit whose DOM range contains the position.
  // If there are multiple qualifying units, returns the last one.
  const NGOffsetMappingUnit* GetMappingUnitForPosition(const Position&) const;

  // Returns all NGOffsetMappingUnits whose DOM ranges has non-empty (but
  // possibly collapsed) intersections with the passed in DOM range. If a unit
  // partially intersects the range, it is clamped with only the part within the
  // range returned. This API only accepts ranges whose start and end have the
  // same anchor node.
  UnitVector GetMappingUnitsForDOMRange(const EphemeralRange&) const;

  // Returns all NGOffsetMappingUnits associated to |node|. When |node| is
  // laid out with ::first-letter, this function returns both first-letter part
  // and remaining part. Note: |node| should have associated mapping.
  NGMappingUnitRange GetMappingUnitsForNode(const Node& node) const;

  // Returns all NGOffsetMappingUnits associated to |layout_object|. This
  // function works even if |layout_object| is for CSS generated content
  // ("content" property in ::before/::after, etc.)
  // Note: Unlike |GetMappingUnitsForNode()|, this function returns units
  // for first-letter or remaining part only instead of both parts.
  // Note: |layout_object| should have associated mapping.
  NGMappingUnitRange GetMappingUnitsForLayoutObject(
      const LayoutObject& layout_object) const;

  // Returns the text content offset corresponding to the given position.
  // Returns nullopt when the position is not laid out in this context.
  base::Optional<unsigned> GetTextContentOffset(const Position&) const;

  // Starting from the given position, searches for non-collapsed content in
  // the anchor node in forward/backward direction and returns the position
  // before/after it; Returns null if there is no more non-collapsed content in
  // the anchor node.
  Position StartOfNextNonCollapsedContent(const Position&) const;
  Position EndOfLastNonCollapsedContent(const Position&) const;

  // Returns true if the position is right before/after non-collapsed content in
  // the anchor node. Note that false is returned if the position is already at
  // the end/start of the anchor node.
  bool IsBeforeNonCollapsedContent(const Position&) const;
  bool IsAfterNonCollapsedContent(const Position&) const;

  // Maps the given position to a text content offset, and then returns the text
  // content character before the offset. Returns nullopt if it does not exist.
  base::Optional<UChar> GetCharacterBefore(const Position&) const;

  // ------ Mapping APIs from text content to DOM ------

  // These APIs map a text content offset to DOM positions, or return null when
  // both characters next to the offset are in generated content (list markers,
  // ::before/after, generated BiDi control characters, ...). The returned
  // position is either offset in a text node, or before/after an atomic inline
  // (IMG, BR, ...).
  // Note 1: there can be multiple positions mapped to the same offset when,
  // for example, there are collapsed whitespaces. Hence, we have two APIs to
  // return the first/last one of them.
  // Note 2: there is a corner case where Shadow DOM changes the ordering of
  // nodes in the flat tree, so that they are not laid out in the same order as
  // in the DOM tree. In this case, "first" and "last" position are defined on
  // the layout order, aka the flat tree order.
  Position GetFirstPosition(unsigned) const;
  Position GetLastPosition(unsigned) const;

  // Converts the given caret position on text content to a PositionWithAffinity
  // in DOM. If |position| is before a character, the function creates a
  // downstream position before |GetLastPosition()| of the character; otherwise,
  // it returns an upstream position after |GetFirstPosition()| of the character
  PositionWithAffinity GetPositionWithAffinity(
      const NGCaretNavigator::Position& position) const;

  // Returns all NGOffsetMappingUnits whose text content ranges has non-empty
  // (but possibly collapsed) intersection with (start, end). Note that units
  // that only "touch" |start| or |end| are excluded.
  // Note: Returned range may include units for generated content.
  NGMappingUnitRange GetMappingUnitsForTextContentOffsetRange(
      unsigned start,
      unsigned end) const;

  // Returns the last |NGOffsetMappingUnit| where |TextContentStart() >= offset|
  // including unit for generated content.
  const NGOffsetMappingUnit* GetLastMappingUnit(unsigned offset) const;

  // ------ APIs inspecting the text content string ------

  // Returns false if all characters in [start, end) of |text_| are bidi
  // control charcters. Returns true otherwise.
  bool HasBidiControlCharactersOnly(unsigned start, unsigned end) const;

 private:
  // The NGOffsetMappingUnits of the inline formatting context in osrted order.
  UnitVector units_;

  // Stores the unit range for each node in inline formatting context.
  RangeMap ranges_;

  // The text content string of the inline formatting context. Same string as
  // |NGInlineNodeData::text_content_|.
  String text_;

  DISALLOW_COPY_AND_ASSIGN(NGOffsetMapping);
};

CORE_EXPORT LayoutBlockFlow* NGInlineFormattingContextOf(const Position&);

}  // namespace blink

#endif  // NGOffsetMapping_h
