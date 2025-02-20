// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/mhtml_generation_manager.h"

#include <utility>

#include "base/bind.h"
#include "base/containers/queue.h"
#include "base/files/file.h"
#include "base/guid.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/scoped_observer.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task_runner_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "components/download/public/common/download_task_runner.h"
#include "content/browser/bad_message.h"
#include "content/browser/download/mhtml_extra_parts_impl.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/common/download/mhtml_file_writer.mojom.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/mhtml_extra_parts.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/mhtml_generation_params.h"
#include "mojo/core/embedder/embedder.h"
#include "net/base/mime_util.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace {
// Callback to notify the UI thread that writing to the MHTML file is complete.
using MHTMLWriteCompleteCallback =
    base::RepeatingCallback<void(content::mojom::MhtmlSaveStatus)>;

const char kContentLocation[] = "Content-Location: ";
const char kContentType[] = "Content-Type: ";
int kInvalidFileSize = -1;
struct CloseFileResult {
  CloseFileResult(content::mojom::MhtmlSaveStatus status, int64_t size)
      : save_status(status), file_size(size) {}
  content::mojom::MhtmlSaveStatus save_status;
  int64_t file_size;
};

base::File CreateMHTMLFile(const base::FilePath& file_path) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());

  // SECURITY NOTE: A file descriptor to the file created below will be passed
  // to multiple renderer processes which (in out-of-process iframes mode) can
  // act on behalf of separate web principals.  Therefore it is important to
  // only allow writing to the file and forbid reading from the file (as this
  // would allow reading content generated by other renderers / other web
  // principals).
  uint32_t file_flags = base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE;

  base::File browser_file(file_path, file_flags);
  if (!browser_file.IsValid()) {
    DLOG(ERROR) << "Failed to create file to save MHTML at: "
                << file_path.value();
  }
  return browser_file;
}

}  // namespace

namespace content {

// The class and all of its members live on the UI thread.  Only static methods
// are executed on other threads.
// Job instances are created in MHTMLGenerationManager::Job::StartNewJob(),
// proceeding with the MHTML saving process unmanaged. Every instance is
// self-owned and responsible for deleting itself upon invoking OnFinished.
// With self-ownership lifetime concerns, we make the following precautions:
// - SerializeAsMHTMLResponse() always proceeds with finalizing upon detecting
//   Job completion/cancellation.
// - Jobs are prematurely finalized and deleted upon detecting a connection
//   error with the message pipe during serialization.
// - Any pending callbacks after deletion are invalidated using weak pointers.
class MHTMLGenerationManager::Job {
 public:
  // Creates and registers a new job.
  static void StartNewJob(WebContents* web_contents,
                          const MHTMLGenerationParams& params,
                          GenerateMHTMLCallback callback);

 private:
  Job(WebContents* web_contents,
      const MHTMLGenerationParams& params,
      GenerateMHTMLCallback callback);
  ~Job();

  // Writes the MHTML footer to the file and closes it. It also receives the
  // SimpleWatcher instance used to watch the data pipe for safe destruction on
  // the IO thread.
  //
  // Note: The same |boundary| marker must be used for all "boundaries" -- in
  // the header, parts and footer -- that belong to the same MHTML document (see
  // also rfc1341, section 7.2.1, "boundary" description).
  static CloseFileResult FinalizeOnFileThread(
      mojom::MhtmlSaveStatus save_status,
      const std::string& boundary,
      base::File file,
      const std::vector<MHTMLExtraDataPart>& extra_data_parts,
      std::unique_ptr<mojo::SimpleWatcher> watcher);

  void AddFrame(RenderFrameHost* render_frame_host);

  // If we have any extra MHTML parts to write out, write them into the file
  // while on the file thread.  Returns true for success, or if there is no data
  // to write.
  static bool WriteExtraDataParts(
      const std::string& boundary,
      base::File& file,
      const std::vector<MHTMLExtraDataPart>& extra_data_parts);

  // Writes the footer into the MHTML file.  Returns false for failure.
  static bool WriteFooter(const std::string& boundary, base::File& file);

  // Called on the UI thread when the file that should hold the MHTML data has
  // been created.
  void OnFileAvailable(base::File browser_file);

  // Called on the UI thread after the file got finalized and we have its size,
  // or an error occurred while creating a new file.
  void OnFinished(const CloseFileResult& close_file_result);

  // Starts watching a handle on the file thread. Instantiates a new instance
  // of |watcher_| upon call.
  void BeginWatchingHandle(MHTMLWriteCompleteCallback callback);

  // Writes data from the consumer handle to the new MHTML file. Only done
  // with on the fly hash computation.
  // Bound to the data pipe watcher and called upon notification of write
  // completion to producer pipe sent to the Renderer.
  // TODO(https://crbug.com/915966): Eventually simplify this implementation
  // with a DataPipeDrainer once error signalling is implemented there.
  void WriteMHTMLToDisk(MHTMLWriteCompleteCallback callback,
                        MojoResult result,
                        const mojo::HandleSignalsState& state);

  // Destroys |watcher_| instance and notifies UI thread of write completion.
  void OnWriteComplete(MHTMLWriteCompleteCallback callback,
                       mojom::MhtmlSaveStatus save_status);

  // Notifies Job of frame write completion and sends request to next render
  // frame if the response was blocked by the write operation.
  void DoneWritingToDisk(mojom::MhtmlSaveStatus save_status);

  // Called when the message pipe to the renderer is disconnected.
  void OnConnectionError();

  // Handler for the Mojo interface callback (a notification from the
  // renderer that the MHTML generation for previous frame has finished).
  void SerializeAsMHTMLResponse(
      mojom::MhtmlSaveStatus save_status,
      const std::vector<std::string>& digests_of_uris_of_serialized_resources,
      base::TimeDelta renderer_main_thread_time);

  // Records newly serialized resource digests into
  // |digests_of_already_serialized_uris_|.
  void RecordDigests(
      const std::vector<std::string>& digests_of_uris_of_serialized_resources);

  // Continues sending serialization requests to the next frame if ready and
  // there are more frames to be serialized.
  void MaybeSendToNextRenderFrame(mojom::MhtmlSaveStatus save_status);

  // Packs up the current status of the MHTML file save operation into a Mojo
  // struct to send to the renderer process.
  mojom::SerializeAsMHTMLParamsPtr CreateMojoParams();

  // Sends Mojo interface call to the renderer, asking for MHTML
  // generation of the next frame. Returns MhtmlSaveStatus::kSuccess or a
  // specific error status.
  mojom::MhtmlSaveStatus SendToNextRenderFrame();

  // Indicates if the writing operation on the IO thread is complete, and
  // we have received a response from the Renderer.
  // This check is necessary to provide synchronization between file writing
  // operations and MHTML serialization.
  bool CurrentFrameDone() const;

  // Called on the UI thread when a job has been finished.
  void Finalize(mojom::MhtmlSaveStatus save_status);

  // Write the MHTML footer and close the file on the file thread and respond
  // back on the UI thread with the updated status and file size (which will be
  // negative in case of errors).
  void CloseFile(mojom::MhtmlSaveStatus save_status);

  // Marks the Job as completed, preventing any further notifications from the
  // Renderer. This prevents the race/crash from https://crbug.com/612098.
  void MarkAsFinished();

  void ReportRendererMainThreadTime(base::TimeDelta renderer_main_thread_time);

  // Close the MHTML file if it looks good, setting the size param.  Returns
  // false for failure.
  static bool CloseFileIfValid(base::File& file, int64_t* file_size);

  // Time tracking for performance metrics reporting.
  const base::TimeTicks creation_time_;
  base::TimeTicks wait_on_renderer_start_time_;
  base::TimeDelta all_renderers_wait_time_;
  base::TimeDelta all_renderers_main_thread_time_;
  base::TimeDelta longest_renderer_main_thread_time_;

  // User-configurable parameters. Includes the file location, binary encoding
  // choices.
  MHTMLGenerationParams params_;

  // The IDs of frames that still need to be processed.
  base::queue<int> pending_frame_tree_node_ids_;

  // Identifies a frame to which we've sent through
  // MhtmlFileWriter::SerializeAsMHTML but for which we didn't yet process
  // the response via SerializeAsMHTMLResponse.
  int frame_tree_node_id_of_busy_frame_;

  // The handle to the file the MHTML is saved to for the browser process.
  base::File browser_file_;

  // MIME multipart boundary to use in the MHTML doc.
  const std::string mhtml_boundary_marker_;

  // Digests of URIs of already generated MHTML parts.
  std::set<std::string> digests_of_already_serialized_uris_;
  std::string salt_;

  // The callback to call once generation is complete.
  GenerateMHTMLCallback callback_;

  // Whether the job is finished (set to true only for the short duration of
  // time between MHTMLGenerationManager::Job::Finalize is called and the job is
  // destroyed by MHTMLGenerationManager::Job::OnFinished).
  bool is_finished_;

  // Any extra data parts that should be emitted into the output MHTML.
  std::vector<MHTMLExtraDataPart> extra_data_parts_;

  // MHTMLFileWriter instance for the frame being currently serialized.
  mojom::MhtmlFileWriterAssociatedPtr writer_;

  // Watcher to detect new data written to |mhtml_data_consumer_|.
  // This is instantiated and destroyed in the download sequence for each frame.
  std::unique_ptr<mojo::SimpleWatcher> watcher_;

  // Consumer handle for data pipe streaming.
  mojo::ScopedDataPipeConsumerHandle mhtml_data_consumer_;

  // Indicates whether there is currently data being streamed from the Renderer.
  // Not used when the renderer is writing directly to file.
  bool waiting_on_data_streaming_;

  base::WeakPtrFactory<Job> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Job);
};

MHTMLGenerationManager::Job::Job(WebContents* web_contents,
                                 const MHTMLGenerationParams& params,
                                 GenerateMHTMLCallback callback)
    : creation_time_(base::TimeTicks::Now()),
      params_(params),
      frame_tree_node_id_of_busy_frame_(FrameTreeNode::kFrameTreeNodeInvalidId),
      mhtml_boundary_marker_(net::GenerateMimeMultipartBoundary()),
      salt_(base::GenerateGUID()),
      callback_(std::move(callback)),
      is_finished_(false),
      waiting_on_data_streaming_(false),
      weak_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(
      "page-serialization", "SavingMhtmlJob", this, "url",
      web_contents->GetLastCommittedURL().possibly_invalid_spec(), "file",
      params.file_path.AsUTF8Unsafe());

  web_contents->ForEachFrame(base::BindRepeating(
      &MHTMLGenerationManager::Job::AddFrame,
      base::Unretained(this)));  // Safe because ForEachFrame() is synchronous.

  // Main frame needs to be processed first.
  DCHECK(!pending_frame_tree_node_ids_.empty());
  DCHECK(FrameTreeNode::GloballyFindByID(pending_frame_tree_node_ids_.front())
             ->parent() == nullptr);

  // Save off any extra data.
  auto* extra_parts = static_cast<MHTMLExtraPartsImpl*>(
      MHTMLExtraParts::FromWebContents(web_contents));
  if (extra_parts)
    extra_data_parts_ = extra_parts->parts();

  base::PostTaskAndReplyWithResult(
      download::GetDownloadTaskRunner().get(), FROM_HERE,
      base::BindOnce(&CreateMHTMLFile, params.file_path),
      base::BindOnce(&Job::OnFileAvailable, weak_factory_.GetWeakPtr()));
}

MHTMLGenerationManager::Job::~Job() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!watcher_);
}

mojom::SerializeAsMHTMLParamsPtr
MHTMLGenerationManager::Job::CreateMojoParams() {
  mojom::SerializeAsMHTMLParamsPtr mojo_params =
      mojom::SerializeAsMHTMLParams::New();
  mojo_params->mhtml_boundary_marker = mhtml_boundary_marker_;
  mojo_params->mhtml_binary_encoding = params_.use_binary_encoding;
  mojo_params->mhtml_popup_overlay_removal = params_.remove_popup_overlay;
  mojo_params->mhtml_problem_detection = params_.use_page_problem_detectors;

  // Tell the renderer to skip (= deduplicate) already covered MHTML parts.
  mojo_params->salt = salt_;
  mojo_params->digests_of_uris_to_skip.assign(
      digests_of_already_serialized_uris_.begin(),
      digests_of_already_serialized_uris_.end());

  return mojo_params;
}

mojom::MhtmlSaveStatus MHTMLGenerationManager::Job::SendToNextRenderFrame() {
  DCHECK(browser_file_.IsValid());
  DCHECK(!pending_frame_tree_node_ids_.empty());

  int frame_tree_node_id = pending_frame_tree_node_ids_.front();
  pending_frame_tree_node_ids_.pop();

  FrameTreeNode* ftn = FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  if (!ftn)  // The contents went away.
    return mojom::MhtmlSaveStatus::kFrameNoLongerExists;
  RenderFrameHost* rfh = ftn->current_frame_host();

  // Bind Mojo interface to the RenderFrame
  rfh->GetRemoteAssociatedInterfaces()->GetInterface(&writer_);

  // Safe, as |writer_| is owned by this Job instance.
  auto error_callback =
      base::BindOnce(&Job::OnConnectionError, base::Unretained(this));
  writer_.set_connection_error_handler(std::move(error_callback));

  mojom::SerializeAsMHTMLParamsPtr params(CreateMojoParams());

  // Initialize method of file writing depending on |compute_contents_hash|
  // flag.
  params->output_handle = mojom::MhtmlOutputHandle::New();
  if (params_.compute_contents_hash) {
    // Create and set up the data pipe.
    mojo::ScopedDataPipeProducerHandle producer;
    if (mojo::CreateDataPipe(nullptr, &producer, &mhtml_data_consumer_) !=
        MOJO_RESULT_OK) {
      DLOG(ERROR) << "Failed to create Mojo Data Pipe.";
      return mojom::MhtmlSaveStatus::kStreamingError;
    }
    MHTMLWriteCompleteCallback write_complete_callback = base::BindRepeating(
        &Job::DoneWritingToDisk, weak_factory_.GetWeakPtr());
    download::GetDownloadTaskRunner().get()->PostTask(
        FROM_HERE,
        base::BindOnce(&Job::BeginWatchingHandle, base::Unretained(this),
                       std::move(write_complete_callback)));
    waiting_on_data_streaming_ = true;
    params->output_handle->set_producer_handle(std::move(producer));
  } else {
    // File::Duplicate() creates a reference to this file for use in the
    // Renderer.
    params->output_handle->set_file_handle(browser_file_.Duplicate());
  }

  // Send a Mojo request to Renderer to serialize its frame.
  DCHECK_EQ(FrameTreeNode::kFrameTreeNodeInvalidId,
            frame_tree_node_id_of_busy_frame_);
  frame_tree_node_id_of_busy_frame_ = frame_tree_node_id;

  auto response_callback = base::BindOnce(&Job::SerializeAsMHTMLResponse,
                                          weak_factory_.GetWeakPtr());
  writer_->SerializeAsMHTML(std::move(params), std::move(response_callback));

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1("page-serialization", "WaitingOnRenderer",
                                    this, "frame tree node id",
                                    frame_tree_node_id_of_busy_frame_);
  DCHECK(wait_on_renderer_start_time_.is_null());
  wait_on_renderer_start_time_ = base::TimeTicks::Now();
  return mojom::MhtmlSaveStatus::kSuccess;
}

void MHTMLGenerationManager::Job::BeginWatchingHandle(
    MHTMLWriteCompleteCallback callback) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());

  DCHECK(!watcher_);
  watcher_ = std::make_unique<mojo::SimpleWatcher>(
      FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::AUTOMATIC,
      download::GetDownloadTaskRunner());

  // base::Unretained is safe, as |this| owns |mhtml_data_consumer_|, which
  // is responsible for invoking |watcher_| callbacks.
  if (watcher_->Watch(
          mhtml_data_consumer_.get(),
          MOJO_HANDLE_SIGNAL_NEW_DATA_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
          MOJO_WATCH_CONDITION_SATISFIED,
          base::BindRepeating(&Job::WriteMHTMLToDisk, base::Unretained(this),
                              callback)) != MOJO_RESULT_OK) {
    DLOG(ERROR) << "Failed to strap watcher to consumer handle.";
    OnWriteComplete(callback, mojom::MhtmlSaveStatus::kStreamingError);
  }
}

void MHTMLGenerationManager::Job::WriteMHTMLToDisk(
    MHTMLWriteCompleteCallback callback,
    MojoResult result,
    const mojo::HandleSignalsState& state) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());
  DCHECK_NE(result, MOJO_RESULT_FAILED_PRECONDITION);

  // Begin consumer data pipe handle read and file write loop.
  char buffer[1024];
  while (result == MOJO_RESULT_OK && state.readable()) {
    uint32_t num_bytes = sizeof(buffer);
    result = mhtml_data_consumer_->ReadData(&buffer, &num_bytes,
                                            MOJO_READ_DATA_FLAG_NONE);

    if (result == MOJO_RESULT_OK &&
        browser_file_.WriteAtCurrentPos(buffer, num_bytes) < 0) {
      DLOG(ERROR) << "Error writing to file handle.";
      OnWriteComplete(std::move(callback),
                      mojom::MhtmlSaveStatus::kFileWritingError);
      return;
    }
  }

  if (result != MOJO_RESULT_OK && result != MOJO_RESULT_FAILED_PRECONDITION &&
      result != MOJO_RESULT_SHOULD_WAIT) {
    DLOG(ERROR) << "Error streaming MHTML data to the Browser.";
    OnWriteComplete(std::move(callback),
                    mojom::MhtmlSaveStatus::kStreamingError);
    return;
  }

  // Only notify successful write completion if peer handle is closed without
  // any errors.
  if (state.peer_closed())
    OnWriteComplete(std::move(callback), mojom::MhtmlSaveStatus::kSuccess);
}

void MHTMLGenerationManager::Job::OnWriteComplete(
    MHTMLWriteCompleteCallback callback,
    mojom::MhtmlSaveStatus save_status) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());

  watcher_.reset();
  base::PostTaskWithTraits(FROM_HERE, {BrowserThread::UI},
                           base::BindOnce(std::move(callback), save_status));
}

void MHTMLGenerationManager::Job::DoneWritingToDisk(
    mojom::MhtmlSaveStatus save_status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // If the Job has prematurely finalized and marked as finished, make this
  // response no-op.
  if (is_finished_)
    return;

  waiting_on_data_streaming_ = false;
  MaybeSendToNextRenderFrame(save_status);
}

void MHTMLGenerationManager::Job::OnConnectionError() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // If message pipe end closes, then it is an unexpected crash.
  DLOG(ERROR) << "Message pipe to renderer closed while expecting response";
  Finalize(mojom::MhtmlSaveStatus::kRenderProcessExited);
}

void MHTMLGenerationManager::Job::OnFileAvailable(base::File browser_file) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!browser_file.IsValid()) {
    DLOG(ERROR) << "Failed to create file";
    Finalize(mojom::MhtmlSaveStatus::kFileCreationError);
    return;
  }

  browser_file_ = std::move(browser_file);

  mojom::MhtmlSaveStatus save_status = SendToNextRenderFrame();
  if (save_status != mojom::MhtmlSaveStatus::kSuccess)
    Finalize(save_status);
}

void MHTMLGenerationManager::Job::OnFinished(
    const CloseFileResult& close_file_result) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  mojom::MhtmlSaveStatus save_status = close_file_result.save_status;
  int64_t file_size = close_file_result.file_size;

  TRACE_EVENT_NESTABLE_ASYNC_END2("page-serialization", "SavingMhtmlJob", this,
                                  "job save status", save_status, "file size",
                                  file_size);
  UMA_HISTOGRAM_TIMES("PageSerialization.MhtmlGeneration.FullPageSavingTime",
                      base::TimeTicks::Now() - creation_time_);
  UMA_HISTOGRAM_ENUMERATION("PageSerialization.MhtmlGeneration.FinalSaveStatus",
                            save_status);
  std::move(callback_).Run(
      save_status == mojom::MhtmlSaveStatus::kSuccess ? file_size : -1);
  delete this;  // This is the last time the Job is referenced.
}

void MHTMLGenerationManager::Job::MarkAsFinished() {
  // MarkAsFinished() may be called twice only in the case which
  // writer_.reset() does not correctly stop OnConnectionError
  // notifications for the case described in https://crbug.com/612098.
  if (is_finished_) {
    NOTREACHED();
    return;
  }
  is_finished_ = true;
  writer_.reset();

  // Additionally, |watcher_| may also invoke DoneWritingToDisk() from
  // the download sequence, potentially calling this twice. We cannot disable
  // |watcher_| notifications similar to |writer_|, since it exists in
  // the download sequence, so we handle the case in DoneWritingToDisk().

  TRACE_EVENT_NESTABLE_ASYNC_INSTANT0("page-serialization", "JobFinished",
                                      this);

  // End of job timing reports.
  if (!wait_on_renderer_start_time_.is_null()) {
    base::TimeDelta renderer_wait_time =
        base::TimeTicks::Now() - wait_on_renderer_start_time_;
    UMA_HISTOGRAM_TIMES(
        "PageSerialization.MhtmlGeneration.BrowserWaitForRendererTime."
        "SingleFrame",
        renderer_wait_time);
    all_renderers_wait_time_ += renderer_wait_time;
  }
  if (!all_renderers_wait_time_.is_zero()) {
    UMA_HISTOGRAM_TIMES(
        "PageSerialization.MhtmlGeneration.BrowserWaitForRendererTime."
        "FrameTree",
        all_renderers_wait_time_);
  }
  if (!all_renderers_main_thread_time_.is_zero()) {
    UMA_HISTOGRAM_TIMES(
        "PageSerialization.MhtmlGeneration.RendererMainThreadTime.FrameTree",
        all_renderers_main_thread_time_);
  }
  if (!longest_renderer_main_thread_time_.is_zero()) {
    UMA_HISTOGRAM_TIMES(
        "PageSerialization.MhtmlGeneration.RendererMainThreadTime.SlowestFrame",
        longest_renderer_main_thread_time_);
  }
}

void MHTMLGenerationManager::Job::ReportRendererMainThreadTime(
    base::TimeDelta renderer_main_thread_time) {
  DCHECK(renderer_main_thread_time > base::TimeDelta());
  if (renderer_main_thread_time > base::TimeDelta())
    all_renderers_main_thread_time_ += renderer_main_thread_time;
  if (renderer_main_thread_time > longest_renderer_main_thread_time_)
    longest_renderer_main_thread_time_ = renderer_main_thread_time;
}

void MHTMLGenerationManager::Job::AddFrame(RenderFrameHost* render_frame_host) {
  auto* rfhi = static_cast<RenderFrameHostImpl*>(render_frame_host);
  int frame_tree_node_id = rfhi->frame_tree_node()->frame_tree_node_id();
  pending_frame_tree_node_ids_.push(frame_tree_node_id);
}

void MHTMLGenerationManager::Job::CloseFile(
    mojom::MhtmlSaveStatus save_status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!mhtml_boundary_marker_.empty());

  // Only update the status if that won't hide an earlier error.
  if (!browser_file_.IsValid() &&
      save_status == mojom::MhtmlSaveStatus::kSuccess)
    save_status = mojom::MhtmlSaveStatus::kFileWritingError;

  // If no previous error occurred the boundary should be sent.
  base::PostTaskAndReplyWithResult(
      download::GetDownloadTaskRunner().get(), FROM_HERE,
      base::BindOnce(&MHTMLGenerationManager::Job::FinalizeOnFileThread,
                     save_status, mhtml_boundary_marker_,
                     std::move(browser_file_), std::move(extra_data_parts_),
                     std::move(watcher_)),
      base::BindOnce(&Job::OnFinished, weak_factory_.GetWeakPtr()));
}

void MHTMLGenerationManager::Job::SerializeAsMHTMLResponse(
    mojom::MhtmlSaveStatus save_status,
    const std::vector<std::string>& digests_of_uris_of_serialized_resources,
    base::TimeDelta renderer_main_thread_time) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  TRACE_EVENT_NESTABLE_ASYNC_END0("page-serialization", "WaitingOnRenderer",
                                  this);
  ReportRendererMainThreadTime(renderer_main_thread_time);

  frame_tree_node_id_of_busy_frame_ = FrameTreeNode::kFrameTreeNodeInvalidId;

  // If the renderer succeeded, update the resource digests.
  if (save_status == mojom::MhtmlSaveStatus::kSuccess)
    RecordDigests(digests_of_uris_of_serialized_resources);

  MaybeSendToNextRenderFrame(save_status);
}

void MHTMLGenerationManager::Job::RecordDigests(
    const std::vector<std::string>& digests_of_uris_of_serialized_resources) {
  DCHECK(!wait_on_renderer_start_time_.is_null());
  base::TimeDelta renderer_wait_time =
      base::TimeTicks::Now() - wait_on_renderer_start_time_;
  UMA_HISTOGRAM_TIMES(
      "PageSerialization.MhtmlGeneration.BrowserWaitForRendererTime."
      "SingleFrame",
      renderer_wait_time);
  all_renderers_wait_time_ += renderer_wait_time;
  wait_on_renderer_start_time_ = base::TimeTicks();

  // Renderer should be deduping resources with the same uris.
  DCHECK_EQ(0u, base::STLSetIntersection<std::set<std::string>>(
                    digests_of_already_serialized_uris_,
                    std::set<std::string>(
                        digests_of_uris_of_serialized_resources.begin(),
                        digests_of_uris_of_serialized_resources.end()))
                    .size());
  digests_of_already_serialized_uris_.insert(
      digests_of_uris_of_serialized_resources.begin(),
      digests_of_uris_of_serialized_resources.end());
}

void MHTMLGenerationManager::Job::MaybeSendToNextRenderFrame(
    mojom::MhtmlSaveStatus save_status) {
  // If current operation is successful and there are more frames to process,
  // let save status depend on the result of sending the next request.
  if (save_status == mojom::MhtmlSaveStatus::kSuccess &&
      !pending_frame_tree_node_ids_.empty() && CurrentFrameDone()) {
    save_status = SendToNextRenderFrame();
  }

  // If there was a failure (either from the renderer or from the job) then
  // terminate the job and return.
  if (save_status != mojom::MhtmlSaveStatus::kSuccess) {
    Finalize(save_status);
    return;
  }

  // Otherwise report completion if there are no more frames to process
  // and Job is done processing the current frame.
  if (pending_frame_tree_node_ids_.empty() && CurrentFrameDone())
    Finalize(mojom::MhtmlSaveStatus::kSuccess);
}

bool MHTMLGenerationManager::Job::CurrentFrameDone() const {
  bool waiting_for_response_from_renderer =
      frame_tree_node_id_of_busy_frame_ !=
      FrameTreeNode::kFrameTreeNodeInvalidId;
  return !waiting_for_response_from_renderer && !waiting_on_data_streaming_;
}

void MHTMLGenerationManager::Job::Finalize(mojom::MhtmlSaveStatus save_status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  MarkAsFinished();
  CloseFile(save_status);
}

// static
void MHTMLGenerationManager::Job::StartNewJob(
    WebContents* web_contents,
    const MHTMLGenerationParams& params,
    GenerateMHTMLCallback callback) {
  // Creates a new Job.
  // The constructor starts the serialization process and it will delete
  // itself upon finishing.
  new Job(web_contents, params, std::move(callback));
}

// static
CloseFileResult MHTMLGenerationManager::Job::FinalizeOnFileThread(
    mojom::MhtmlSaveStatus save_status,
    const std::string& boundary,
    base::File file,
    const std::vector<MHTMLExtraDataPart>& extra_data_parts,
    std::unique_ptr<mojo::SimpleWatcher> watcher) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());

  watcher.reset();

  // If no previous error occurred the boundary should have been provided.
  if (save_status == mojom::MhtmlSaveStatus::kSuccess) {
    TRACE_EVENT0("page-serialization",
                 "MHTMLGenerationManager::Job MHTML footer writing");
    DCHECK(!boundary.empty());

    // Write the extra data into a part of its own, if we have any.
    if (!WriteExtraDataParts(boundary, file, extra_data_parts)) {
      save_status = mojom::MhtmlSaveStatus::kFileWritingError;
    }

    // Write out the footer at the bottom of the file.
    if (save_status == mojom::MhtmlSaveStatus::kSuccess &&
        !WriteFooter(boundary, file)) {
      save_status = mojom::MhtmlSaveStatus::kFileWritingError;
    }
  }

  // If the file is still valid try to close it. Only update the status if that
  // won't hide an earlier error.
  int64_t file_size = kInvalidFileSize;
  if (!CloseFileIfValid(file, &file_size) &&
      save_status == mojom::MhtmlSaveStatus::kSuccess) {
    save_status = mojom::MhtmlSaveStatus::kFileClosingError;
  }

  return CloseFileResult(save_status, file_size);
}

// static
bool MHTMLGenerationManager::Job::WriteExtraDataParts(
    const std::string& boundary,
    base::File& file,
    const std::vector<MHTMLExtraDataPart>& extra_data_parts) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());
  // Don't write an extra data part if there is none.
  if (extra_data_parts.empty())
    return true;

  std::string serialized_extra_data_parts;

  // For each extra part, serialize that part and add to our accumulator
  // string.
  for (const auto& part : extra_data_parts) {
    // Write a newline, then a boundary, a newline, then the content
    // location, a newline, the content type, a newline, extra_headers,
    // two newlines, the body, and end with a newline.
    std::string serialized_extra_data_part = base::StringPrintf(
        "\r\n--%s\r\n%s%s\r\n%s%s\r\n%s\r\n\r\n%s\r\n", boundary.c_str(),
        kContentLocation, part.content_location.c_str(), kContentType,
        part.content_type.c_str(), part.extra_headers.c_str(),
        part.body.c_str());
    DCHECK(base::IsStringASCII(serialized_extra_data_part));

    serialized_extra_data_parts += serialized_extra_data_part;
  }

  // Write the string into the file.  Returns false if we failed the write.
  return (file.WriteAtCurrentPos(serialized_extra_data_parts.data(),
                                 serialized_extra_data_parts.size()) >= 0);
}

// static
bool MHTMLGenerationManager::Job::WriteFooter(const std::string& boundary,
                                              base::File& file) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());
  // Per the spec, the boundary must occur at the beginning of a line.
  std::string footer = base::StringPrintf("\r\n--%s--\r\n", boundary.c_str());
  DCHECK(base::IsStringASCII(footer));
  return (file.WriteAtCurrentPos(footer.data(), footer.size()) >= 0);
}

// static
bool MHTMLGenerationManager::Job::CloseFileIfValid(base::File& file,
                                                   int64_t* file_size) {
  DCHECK(download::GetDownloadTaskRunner()->RunsTasksInCurrentSequence());
  DCHECK(file_size);
  if (file.IsValid()) {
    *file_size = file.GetLength();
    file.Close();
    return true;
  }

  return false;
}

MHTMLGenerationManager* MHTMLGenerationManager::GetInstance() {
  return base::Singleton<MHTMLGenerationManager>::get();
}

MHTMLGenerationManager::MHTMLGenerationManager() {}

MHTMLGenerationManager::~MHTMLGenerationManager() {}

void MHTMLGenerationManager::SaveMHTML(WebContents* web_contents,
                                       const MHTMLGenerationParams& params,
                                       GenerateMHTMLCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  Job::StartNewJob(web_contents, params, std::move(callback));
}

}  // namespace content
