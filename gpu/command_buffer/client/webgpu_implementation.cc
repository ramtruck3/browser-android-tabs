// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/client/webgpu_implementation.h"

#include <algorithm>
#include <vector>

#include "base/numerics/checked_math.h"
#include "gpu/command_buffer/client/gpu_control.h"
#include "gpu/command_buffer/client/shared_memory_limits.h"

#define GPU_CLIENT_SINGLE_THREAD_CHECK()

namespace gpu {
namespace webgpu {

// Include the auto-generated part of this file. We split this because it means
// we can easily edit the non-auto generated parts right here in this file
// instead of having to edit some template or the code generator.
#include "gpu/command_buffer/client/webgpu_implementation_impl_autogen.h"

WebGPUImplementation::WebGPUImplementation(
    WebGPUCmdHelper* helper,
    TransferBufferInterface* transfer_buffer,
    GpuControl* gpu_control)
    : ImplementationBase(helper, transfer_buffer, gpu_control),
      helper_(helper),
#if BUILDFLAG(USE_DAWN)
      wire_client_(new dawn_wire::WireClient(this)),
      procs_(wire_client_->GetProcs()),
#endif
      c2s_buffer_(helper, transfer_buffer) {
}

WebGPUImplementation::~WebGPUImplementation() {}

gpu::ContextResult WebGPUImplementation::Initialize(
    const SharedMemoryLimits& limits) {
  TRACE_EVENT0("gpu", "WebGPUImplementation::Initialize");
  auto result = ImplementationBase::Initialize(limits);
  if (result != gpu::ContextResult::kSuccess) {
    return result;
  }

  c2s_buffer_default_size_ = limits.start_transfer_buffer_size;
  DCHECK_GT(c2s_buffer_default_size_, 0u);

  return gpu::ContextResult::kSuccess;
}

// ContextSupport implementation.
void WebGPUImplementation::SetAggressivelyFreeResources(
    bool aggressively_free_resources) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::Swap(uint32_t flags,
                                SwapCompletedCallback complete_callback,
                                PresentationCallback presentation_callback) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::SwapWithBounds(
    const std::vector<gfx::Rect>& rects,
    uint32_t flags,
    SwapCompletedCallback swap_completed,
    PresentationCallback presentation_callback) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::PartialSwapBuffers(
    const gfx::Rect& sub_buffer,
    uint32_t flags,
    SwapCompletedCallback swap_completed,
    PresentationCallback presentation_callback) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::CommitOverlayPlanes(
    uint32_t flags,
    SwapCompletedCallback swap_completed,
    PresentationCallback presentation_callback) {
  NOTREACHED();
}
void WebGPUImplementation::ScheduleOverlayPlane(
    int plane_z_order,
    gfx::OverlayTransform plane_transform,
    unsigned overlay_texture_id,
    const gfx::Rect& display_bounds,
    const gfx::RectF& uv_rect,
    bool enable_blend,
    unsigned gpu_fence_id) {
  NOTREACHED();
}
uint64_t WebGPUImplementation::ShareGroupTracingGUID() const {
  NOTIMPLEMENTED();
  return 0;
}
void WebGPUImplementation::SetErrorMessageCallback(
    base::RepeatingCallback<void(const char*, int32_t)> callback) {
  NOTIMPLEMENTED();
}
bool WebGPUImplementation::ThreadSafeShallowLockDiscardableTexture(
    uint32_t texture_id) {
  NOTREACHED();
  return false;
}
void WebGPUImplementation::CompleteLockDiscardableTexureOnContextThread(
    uint32_t texture_id) {
  NOTREACHED();
}
bool WebGPUImplementation::ThreadsafeDiscardableTextureIsDeletedForTracing(
    uint32_t texture_id) {
  NOTREACHED();
  return false;
}
void* WebGPUImplementation::MapTransferCacheEntry(uint32_t serialized_size) {
  NOTREACHED();
  return nullptr;
}
void WebGPUImplementation::UnmapAndCreateTransferCacheEntry(uint32_t type,
                                                            uint32_t id) {
  NOTREACHED();
}
bool WebGPUImplementation::ThreadsafeLockTransferCacheEntry(uint32_t type,
                                                            uint32_t id) {
  NOTREACHED();
  return false;
}
void WebGPUImplementation::UnlockTransferCacheEntries(
    const std::vector<std::pair<uint32_t, uint32_t>>& entries) {
  NOTREACHED();
}
void WebGPUImplementation::DeleteTransferCacheEntry(uint32_t type,
                                                    uint32_t id) {
  NOTREACHED();
}
unsigned int WebGPUImplementation::GetTransferBufferFreeSize() const {
  NOTREACHED();
  return 0;
}
bool WebGPUImplementation::CanDecodeWithHardwareAcceleration(
    base::span<const uint8_t> encoded_data) const {
  NOTREACHED();
  return false;
}

// InterfaceBase implementation.
void WebGPUImplementation::GenSyncTokenCHROMIUM(GLbyte* sync_token) {
  ImplementationBase::GenSyncToken(sync_token);
}
void WebGPUImplementation::GenUnverifiedSyncTokenCHROMIUM(GLbyte* sync_token) {
  ImplementationBase::GenUnverifiedSyncToken(sync_token);
}
void WebGPUImplementation::VerifySyncTokensCHROMIUM(GLbyte** sync_tokens,
                                                    GLsizei count) {
  ImplementationBase::VerifySyncTokens(sync_tokens, count);
}
void WebGPUImplementation::WaitSyncTokenCHROMIUM(const GLbyte* sync_token) {
  ImplementationBase::WaitSyncToken(sync_token);
}

// ImplementationBase implementation.
void WebGPUImplementation::IssueShallowFlush() {
  NOTIMPLEMENTED();
}

void WebGPUImplementation::SetGLError(GLenum error,
                                      const char* function_name,
                                      const char* msg) {
  GPU_CLIENT_LOG("[" << GetLogPrefix() << "] Client Synthesized Error: "
                     << gles2::GLES2Util::GetStringError(error) << ": "
                     << function_name << ": " << msg);
  NOTIMPLEMENTED();
}

// GpuControlClient implementation.
void WebGPUImplementation::OnGpuControlLostContext() {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::OnGpuControlLostContextMaybeReentrant() {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::OnGpuControlErrorMessage(const char* message,
                                                    int32_t id) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::OnGpuControlSwapBuffersCompleted(
    const SwapBuffersCompleteParams& params) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::OnSwapBufferPresented(
    uint64_t swap_id,
    const gfx::PresentationFeedback& feedback) {
  NOTIMPLEMENTED();
}
void WebGPUImplementation::OnGpuControlReturnData(
    base::span<const uint8_t> data) {
#if BUILDFLAG(USE_DAWN)
  if (!wire_client_->HandleCommands(
      reinterpret_cast<const char*>(data.data()), data.size())) {
    // TODO(enga): Lose the context.
    NOTREACHED();
  }
#endif
}

void* WebGPUImplementation::GetCmdSpace(size_t size) {
  // The buffer size must be initialized before any commands are serialized.
  if (c2s_buffer_default_size_ == 0u) {
    NOTREACHED();
    return nullptr;
  }

  base::CheckedNumeric<uint32_t> checked_next_offset(c2s_put_offset_);
  checked_next_offset += size;

  uint32_t next_offset;
  bool next_offset_valid = checked_next_offset.AssignIfValid(&next_offset);

  // If the buffer does not have enough space, or if the buffer is not
  // initialized, flush and reset the command stream.
  if (!next_offset_valid || next_offset > c2s_buffer_.size() ||
      !c2s_buffer_.valid()) {
    Flush();

    uint32_t max_allocation = transfer_buffer_->GetMaxSize();
    // TODO(crbug.com/951558): Handle command chunking or ensure commands aren't
    // this large.
    CHECK_LE(size, max_allocation);

    uint32_t allocation_size =
        std::max(c2s_buffer_default_size_, static_cast<uint32_t>(size));
    c2s_buffer_.Reset(allocation_size);
    c2s_put_offset_ = 0;
    next_offset = size;

    // TODO(crbug.com/951558): Handle OOM.
    CHECK(c2s_buffer_.valid());
    CHECK_LE(size, c2s_buffer_.size());
  }

  DCHECK(c2s_buffer_.valid());
  uint8_t* ptr = static_cast<uint8_t*>(c2s_buffer_.address());
  ptr += c2s_put_offset_;

  c2s_put_offset_ = next_offset;
  return ptr;
}

bool WebGPUImplementation::Flush() {
  if (c2s_buffer_.valid()) {
    c2s_buffer_.Shrink(c2s_put_offset_);
    helper_->DawnCommands(c2s_buffer_.shm_id(), c2s_buffer_.offset(),
                          c2s_put_offset_);
    c2s_put_offset_ = 0;
    c2s_buffer_.Release();
  }
  return true;
}

const DawnProcTable& WebGPUImplementation::GetProcs() const {
#if !BUILDFLAG(USE_DAWN)
  NOTREACHED();
#endif
  return procs_;
}

void WebGPUImplementation::FlushCommands() {
  Flush();
  helper_->Flush();
}

DawnDevice WebGPUImplementation::GetDefaultDevice() {
#if BUILDFLAG(USE_DAWN)
  return wire_client_->GetDevice();
#else
  NOTREACHED();
  return {};
#endif
}

}  // namespace webgpu
}  // namespace gpu
