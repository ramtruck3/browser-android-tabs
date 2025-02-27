// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_EFFECT_NODE_H_
#define CC_TREES_EFFECT_NODE_H_

#include "cc/cc_export.h"
#include "cc/paint/filter_operations.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/rrect_f.h"

namespace base {
namespace trace_event {
class TracedValue;
}  // namespace trace_event
}  // namespace base

namespace cc {

struct CC_EXPORT EffectNode {
  EffectNode();
  EffectNode(const EffectNode& other);

  enum StableIdLabels { INVALID_STABLE_ID = 0 };

  // The node index of this node in the effect tree node vector.
  int id;
  // The node index of the parent node in the effect tree node vector.
  int parent_id;
  // An opaque, unique, stable identifer for this effect that persists across
  // frame commits. This id is used only for internal implementation
  // details such as RenderSurface and RenderPass ids, and should not
  // be assumed to have semantic meaning.
  uint64_t stable_id;

  float opacity;
  float screen_space_opacity;

  FilterOperations filters;
  FilterOperations backdrop_filters;
  gfx::RRectF backdrop_filter_bounds;
  float backdrop_filter_quality;
  gfx::PointF filters_origin;

  // Bounds of rounded corner rrect in the space of the transform node
  // associated with this effect node.
  gfx::RRectF rounded_corner_bounds;

  SkBlendMode blend_mode;

  gfx::Vector2dF surface_contents_scale;

  gfx::Size unscaled_mask_target_size;

  bool has_render_surface : 1;
  bool cache_render_surface : 1;
  bool has_copy_request : 1;
  bool hidden_by_backface_visibility : 1;
  bool double_sided : 1;
  bool trilinear_filtering : 1;
  bool is_drawn : 1;
  // TODO(jaydasika) : Delete this after implementation of
  // SetHideLayerAndSubtree is cleaned up. (crbug.com/595843)
  bool subtree_hidden : 1;
  // Whether this node has a potentially running (i.e., irrespective
  // of exact timeline) filter animation.
  bool has_potential_filter_animation : 1;
  // Whether this node has a potentially running (i.e., irrespective
  // of exact timeline) opacity animation.
  bool has_potential_opacity_animation : 1;
  // Whether this node has a currently running filter animation.
  bool is_currently_animating_filter : 1;
  // Whether this node has a currently running opacity animation.
  bool is_currently_animating_opacity : 1;
  // Whether this node has a child node with kDstIn blend mode.
  bool has_masking_child : 1;
  // Whether this node has a mask. This bit is not used when using layer lists.
  bool is_masked : 1;
  // Whether this node's effect has been changed since the last
  // frame. Needed in order to compute damage rect.
  bool effect_changed : 1;
  bool subtree_has_copy_request : 1;
  // If set, the effect node tries to not trigger a render surface due to it
  // having a rounded corner.
  bool is_fast_rounded_corner;
  // The transform node index of the transform to apply to this effect
  // node's content when rendering to a surface.
  int transform_id;
  // The clip node index of the clip to apply to this effect node's
  // content when rendering to a surface.
  int clip_id;

  // This is the id of the ancestor effect node that induces a
  // RenderSurfaceImpl.
  int target_id;
  // The layer id of the mask layer, if any, to apply to this effect
  // node's content when rendering to a surface.
  int mask_layer_id;
  int closest_ancestor_with_cached_render_surface_id;
  int closest_ancestor_with_copy_request_id;

  bool operator==(const EffectNode& other) const;

  void AsValueInto(base::trace_event::TracedValue* value) const;
};

}  // namespace cc

#endif  // CC_TREES_EFFECT_NODE_H_
