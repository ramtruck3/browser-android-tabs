// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_NS_VIEW_BRIDGE_H_
#define CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_NS_VIEW_BRIDGE_H_

#import <Cocoa/Cocoa.h>

#include <memory>

#import "base/mac/scoped_nsobject.h"
#include "base/macros.h"
#include "content/common/content_export.h"
#include "content/public/common/web_contents_ns_view_bridge.mojom.h"
#include "ui/base/cocoa/ns_view_ids.h"

@class WebContentsViewCocoa;

namespace content {

class WebContentsViewMac;

// A wrapper around a WebContentsViewCocoa, to be accessed via the mojo
// interface WebContentsNSViewBridge.
class CONTENT_EXPORT WebContentsNSViewBridge
    : public mojom::WebContentsNSViewBridge {
 public:
  // Create a bridge that will access its client in another process via a mojo
  // interface.
  WebContentsNSViewBridge(uint64_t view_id,
                          mojom::WebContentsNSViewClientAssociatedPtr client);
  // Create a bridge that will access its client directly in-process.
  // TODO(ccameron): Change this to expose only the mojom::WebContentsNSView
  // when all communication is through mojo.
  WebContentsNSViewBridge(uint64_t view_id,
                          WebContentsViewMac* web_contents_view);
  ~WebContentsNSViewBridge() override;

  WebContentsViewCocoa* cocoa_view() const { return cocoa_view_.get(); }

  // mojom::WebContentsNSViewBridge:
  void SetParentNSView(uint64_t parent_ns_view_id) override;
  void ResetParentNSView() override;
  void SetBounds(const gfx::Rect& bounds_in_window) override;
  void SetVisible(bool visible) override;
  void MakeFirstResponder() override;
  void TakeFocus(bool reverse) override;
  void StartDrag(const DropData& drop_data,
                 uint32_t operation_mask,
                 const gfx::ImageSkia& image,
                 const gfx::Vector2d& image_offset) override;

 private:
  base::scoped_nsobject<WebContentsViewCocoa> cocoa_view_;
  mojom::WebContentsNSViewClientAssociatedPtr client_;

  std::unique_ptr<ui::ScopedNSViewIdMapping> view_id_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsNSViewBridge);
};

}  // namespace content

#endif  // CONTENT_BROWSER_WEB_CONTENTS_WEB_CONTENTS_NS_VIEW_BRIDGE_H_
