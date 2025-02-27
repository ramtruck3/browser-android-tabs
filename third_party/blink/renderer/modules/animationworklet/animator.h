// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ANIMATIONWORKLET_ANIMATOR_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ANIMATIONWORKLET_ANIMATOR_H_

#include "third_party/blink/renderer/modules/animationworklet/worklet_animation_options.h"
#include "third_party/blink/renderer/modules/animationworklet/worklet_group_effect_proxy.h"
#include "third_party/blink/renderer/platform/bindings/name_client.h"
#include "third_party/blink/renderer/platform/bindings/trace_wrapper_v8_reference.h"
#include "third_party/blink/renderer/platform/graphics/animation_worklet_mutators_state.h"
#include "third_party/blink/renderer/platform/heap/handle.h"
#include "third_party/blink/renderer/platform/wtf/time.h"
#include "v8/include/v8.h"

namespace blink {

class AnimatorDefinition;

// Represents an animator instance. It owns the underlying |v8::Object| for the
// instance and knows how to invoke the |animate| function on it.
// See also |AnimationWorkletGlobalScope::CreateInstance|.
class Animator final : public GarbageCollectedFinalized<Animator>,
                       public NameClient {
 public:
  Animator(v8::Isolate*,
           AnimatorDefinition*,
           v8::Local<v8::Value> instance,
           const String& name,
           WorkletAnimationOptions options,
           const std::vector<base::Optional<TimeDelta>>& local_times);
  ~Animator();
  void Trace(blink::Visitor*);
  const char* NameInHeapSnapshot() const override { return "Animator"; }

  // Returns true if it successfully invoked animate callback in JS. It receives
  // latest state coming from |AnimationHost| as input and fills
  // the output state with new updates.
  bool Animate(v8::Isolate* isolate,
               double current_time,
               AnimationWorkletDispatcherOutput::AnimationState* output);
  v8::Local<v8::Value> State(v8::Isolate*, ExceptionState&);
  std::vector<base::Optional<TimeDelta>> GetLocalTimes() const;
  bool IsStateful() const;

  const String& name() const { return name_; }
  WorkletAnimationOptions options() { return options_; }
  int num_effects() const { return group_effect_->getChildren().size(); }

 private:
  // This object keeps the definition object, and animator instance alive.
  // It participates in wrapper tracing as it holds onto V8 wrappers.
  Member<AnimatorDefinition> definition_;
  TraceWrapperV8Reference<v8::Value> instance_;

  // The 'name' and 'options' of the animator need to be stored to be
  // migrated to the new animator upon switching global scopes. For other
  // properties, 'animation id' and 'number of effects' can be, and 'state'
  // should be queried on the fly.
  String name_;
  WorkletAnimationOptions options_;

  Member<WorkletGroupEffectProxy> group_effect_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_ANIMATIONWORKLET_ANIMATOR_H_
