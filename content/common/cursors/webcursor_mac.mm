// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/cursors/webcursor.h"

#import <AppKit/AppKit.h>
#include <stddef.h>

#include "base/logging.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/mac/sdk_forward_declarations.h"
#include "content/app/resources/grit/content_resources.h"
#include "content/public/common/content_client.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/blink/public/platform/web_cursor_info.h"
#include "third_party/blink/public/platform/web_size.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image.h"

using blink::WebCursorInfo;
using blink::WebSize;

// Private interface to CoreCursor, as of Mac OS X 10.7. This is essentially the
// implementation of WKCursor in WebKitSystemInterface.

enum {
  kArrowCursor = 0,
  kIBeamCursor = 1,
  kMakeAliasCursor = 2,
  kOperationNotAllowedCursor = 3,
  kBusyButClickableCursor = 4,
  kCopyCursor = 5,
  kClosedHandCursor = 11,
  kOpenHandCursor = 12,
  kPointingHandCursor = 13,
  kCountingUpHandCursor = 14,
  kCountingDownHandCursor = 15,
  kCountingUpAndDownHandCursor = 16,
  kResizeLeftCursor = 17,
  kResizeRightCursor = 18,
  kResizeLeftRightCursor = 19,
  kCrosshairCursor = 20,
  kResizeUpCursor = 21,
  kResizeDownCursor = 22,
  kResizeUpDownCursor = 23,
  kContextualMenuCursor = 24,
  kDisappearingItemCursor = 25,
  kVerticalIBeamCursor = 26,
  kResizeEastCursor = 27,
  kResizeEastWestCursor = 28,
  kResizeNortheastCursor = 29,
  kResizeNortheastSouthwestCursor = 30,
  kResizeNorthCursor = 31,
  kResizeNorthSouthCursor = 32,
  kResizeNorthwestCursor = 33,
  kResizeNorthwestSoutheastCursor = 34,
  kResizeSoutheastCursor = 35,
  kResizeSouthCursor = 36,
  kResizeSouthwestCursor = 37,
  kResizeWestCursor = 38,
  kMoveCursor = 39,
  kHelpCursor = 40,  // Present on >= 10.7.3.
  kCellCursor = 41,  // Present on >= 10.7.3.
  kZoomInCursor = 42,  // Present on >= 10.7.3.
  kZoomOutCursor = 43  // Present on >= 10.7.3.
};
typedef long long CrCoreCursorType;

@interface CrCoreCursor : NSCursor {
 @private
  CrCoreCursorType type_;
}

+ (id)cursorWithType:(CrCoreCursorType)type;
- (id)initWithType:(CrCoreCursorType)type;
- (CrCoreCursorType)_coreCursorType;

@end

@implementation CrCoreCursor

+ (id)cursorWithType:(CrCoreCursorType)type {
  NSCursor* cursor = [[CrCoreCursor alloc] initWithType:type];
  if ([cursor image])
    return [cursor autorelease];

  [cursor release];
  return nil;
}

- (id)initWithType:(CrCoreCursorType)type {
  if ((self = [super init])) {
    type_ = type;
  }
  return self;
}

- (CrCoreCursorType)_coreCursorType {
  return type_;
}

@end

namespace {

NSCursor* LoadCursor(int resource_id, int hotspot_x, int hotspot_y) {
  const gfx::Image& cursor_image =
      content::GetContentClient()->GetNativeImageNamed(resource_id);
  DCHECK(!cursor_image.IsEmpty());
  return [[[NSCursor alloc] initWithImage:cursor_image.ToNSImage()
                                  hotSpot:NSMakePoint(hotspot_x,
                                                      hotspot_y)] autorelease];
}

// Gets a specified cursor from CoreCursor, falling back to loading it from the
// image cache if CoreCursor cannot provide it.
NSCursor* GetCoreCursorWithFallback(CrCoreCursorType type,
                                    int resource_id,
                                    int hotspot_x,
                                    int hotspot_y) {
  NSCursor* cursor = [CrCoreCursor cursorWithType:type];
  if (cursor)
    return cursor;

  return LoadCursor(resource_id, hotspot_x, hotspot_y);
}

NSCursor* CreateCustomCursor(const content::CursorInfo& info) {
  float custom_scale = info.image_scale_factor;
  gfx::Size custom_size(info.custom_image.width(), info.custom_image.height());

  // Convert from pixels to view units.
  if (custom_scale == 0)
    custom_scale = 1;
  NSSize dip_size = NSSizeFromCGSize(
      gfx::ScaleToFlooredSize(custom_size, 1 / custom_scale).ToCGSize());
  NSPoint dip_hotspot = NSPointFromCGPoint(
      gfx::ScaleToFlooredPoint(info.hotspot, 1 / custom_scale).ToCGPoint());

  // Both the image and its representation need to have the same size for
  // cursors to appear in high resolution on retina displays. Note that the
  // size of a representation is not the same as pixelsWide or pixelsHigh.
  NSImage* cursor_image = skia::SkBitmapToNSImage(info.custom_image);
  [cursor_image setSize:dip_size];
  [[[cursor_image representations] objectAtIndex:0] setSize:dip_size];

  NSCursor* cursor = [[NSCursor alloc] initWithImage:cursor_image
                                             hotSpot:dip_hotspot];

  return [cursor autorelease];
}

}  // namespace

namespace content {

// Match Safari's cursor choices; see platform/mac/CursorMac.mm .
gfx::NativeCursor WebCursor::GetNativeCursor() {
  switch (info_.type) {
    case WebCursorInfo::kTypePointer:
      return [NSCursor arrowCursor];
    case WebCursorInfo::kTypeCross:
      return [NSCursor crosshairCursor];
    case WebCursorInfo::kTypeHand:
      return [NSCursor pointingHandCursor];
    case WebCursorInfo::kTypeIBeam:
      return [NSCursor IBeamCursor];
    case WebCursorInfo::kTypeWait:
      return GetCoreCursorWithFallback(kBusyButClickableCursor,
                                       IDR_WAIT_CURSOR, 7, 7);
    case WebCursorInfo::kTypeHelp:
      return GetCoreCursorWithFallback(kHelpCursor,
                                       IDR_HELP_CURSOR, 8, 8);
    case WebCursorInfo::kTypeEastResize:
    case WebCursorInfo::kTypeEastPanning:
      return GetCoreCursorWithFallback(kResizeEastCursor,
                                       IDR_EAST_RESIZE_CURSOR, 14, 7);
    case WebCursorInfo::kTypeNorthResize:
    case WebCursorInfo::kTypeNorthPanning:
      return GetCoreCursorWithFallback(kResizeNorthCursor,
                                       IDR_NORTH_RESIZE_CURSOR, 7, 1);
    case WebCursorInfo::kTypeNorthEastResize:
    case WebCursorInfo::kTypeNorthEastPanning:
      return GetCoreCursorWithFallback(kResizeNortheastCursor,
                                       IDR_NORTHEAST_RESIZE_CURSOR, 14, 1);
    case WebCursorInfo::kTypeNorthWestResize:
    case WebCursorInfo::kTypeNorthWestPanning:
      return GetCoreCursorWithFallback(kResizeNorthwestCursor,
                                       IDR_NORTHWEST_RESIZE_CURSOR, 0, 0);
    case WebCursorInfo::kTypeSouthResize:
    case WebCursorInfo::kTypeSouthPanning:
      return GetCoreCursorWithFallback(kResizeSouthCursor,
                                       IDR_SOUTH_RESIZE_CURSOR, 7, 14);
    case WebCursorInfo::kTypeSouthEastResize:
    case WebCursorInfo::kTypeSouthEastPanning:
      return GetCoreCursorWithFallback(kResizeSoutheastCursor,
                                       IDR_SOUTHEAST_RESIZE_CURSOR, 14, 14);
    case WebCursorInfo::kTypeSouthWestResize:
    case WebCursorInfo::kTypeSouthWestPanning:
      return GetCoreCursorWithFallback(kResizeSouthwestCursor,
                                       IDR_SOUTHWEST_RESIZE_CURSOR, 1, 14);
    case WebCursorInfo::kTypeWestResize:
    case WebCursorInfo::kTypeWestPanning:
      return GetCoreCursorWithFallback(kResizeWestCursor,
                                       IDR_WEST_RESIZE_CURSOR, 1, 7);
    case WebCursorInfo::kTypeNorthSouthResize:
      return GetCoreCursorWithFallback(kResizeNorthSouthCursor,
                                       IDR_NORTHSOUTH_RESIZE_CURSOR, 7, 7);
    case WebCursorInfo::kTypeEastWestResize:
      return GetCoreCursorWithFallback(kResizeEastWestCursor,
                                       IDR_EASTWEST_RESIZE_CURSOR, 7, 7);
    case WebCursorInfo::kTypeNorthEastSouthWestResize:
      return GetCoreCursorWithFallback(kResizeNortheastSouthwestCursor,
                                       IDR_NORTHEASTSOUTHWEST_RESIZE_CURSOR,
                                       7, 7);
    case WebCursorInfo::kTypeNorthWestSouthEastResize:
      return GetCoreCursorWithFallback(kResizeNorthwestSoutheastCursor,
                                       IDR_NORTHWESTSOUTHEAST_RESIZE_CURSOR,
                                       7, 7);
    case WebCursorInfo::kTypeColumnResize:
      return [NSCursor resizeLeftRightCursor];
    case WebCursorInfo::kTypeRowResize:
      return [NSCursor resizeUpDownCursor];
    case WebCursorInfo::kTypeMiddlePanning:
    case WebCursorInfo::kTypeMove:
      return GetCoreCursorWithFallback(kMoveCursor,
                                       IDR_MOVE_CURSOR, 7, 7);
    case WebCursorInfo::kTypeVerticalText:
      // IBeamCursorForVerticalLayout is >= 10.7.
      if ([NSCursor respondsToSelector:@selector(IBeamCursorForVerticalLayout)])
        return [NSCursor IBeamCursorForVerticalLayout];
      else
        return LoadCursor(IDR_VERTICALTEXT_CURSOR, 7, 7);
    case WebCursorInfo::kTypeCell:
      return GetCoreCursorWithFallback(kCellCursor,
                                       IDR_CELL_CURSOR, 7, 7);
    case WebCursorInfo::kTypeContextMenu:
      return [NSCursor contextualMenuCursor];
    case WebCursorInfo::kTypeAlias:
      return GetCoreCursorWithFallback(kMakeAliasCursor,
                                       IDR_ALIAS_CURSOR, 11, 3);
    case WebCursorInfo::kTypeProgress:
      return GetCoreCursorWithFallback(kBusyButClickableCursor,
                                       IDR_PROGRESS_CURSOR, 3, 2);
    case WebCursorInfo::kTypeNoDrop:
    case WebCursorInfo::kTypeNotAllowed:
      return [NSCursor operationNotAllowedCursor];
    case WebCursorInfo::kTypeCopy:
      return [NSCursor dragCopyCursor];
    case WebCursorInfo::kTypeNone:
      return LoadCursor(IDR_NONE_CURSOR, 7, 7);
    case WebCursorInfo::kTypeZoomIn:
      return GetCoreCursorWithFallback(kZoomInCursor,
                                       IDR_ZOOMIN_CURSOR, 7, 7);
    case WebCursorInfo::kTypeZoomOut:
      return GetCoreCursorWithFallback(kZoomOutCursor,
                                       IDR_ZOOMOUT_CURSOR, 7, 7);
    case WebCursorInfo::kTypeGrab:
      return [NSCursor openHandCursor];
    case WebCursorInfo::kTypeGrabbing:
      return [NSCursor closedHandCursor];
    case WebCursorInfo::kTypeCustom:
      return CreateCustomCursor(info_);
  }
  NOTREACHED();
  return nil;
}

bool WebCursor::IsPlatformDataEqual(const WebCursor& other) const {
  return true;
}

void WebCursor::CleanupPlatformData() {
  return;
}

void WebCursor::CopyPlatformData(const WebCursor& other) {
  return;
}

}  // namespace content
