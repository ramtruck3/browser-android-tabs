// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_parser.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/upload_data_stream.h"
#include "net/http/http_chunked_decoder.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_util.h"
#include "net/log/net_log_event_type.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/stream_socket.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_info.h"
#include "url/url_canon.h"

namespace net {

namespace {

const uint64_t kMaxMergedHeaderAndBodySize = 1400;
const size_t kRequestBodyBufferSize = 1 << 14;  // 16KB

std::string GetResponseHeaderLines(const HttpResponseHeaders& headers) {
  std::string raw_headers = headers.raw_headers();
  const char* null_separated_headers = raw_headers.c_str();
  const char* header_line = null_separated_headers;
  std::string cr_separated_headers;
  while (header_line[0] != 0) {
    cr_separated_headers += header_line;
    cr_separated_headers += "\n";
    header_line += strlen(header_line) + 1;
  }
  return cr_separated_headers;
}

// Return true if |headers| contain multiple |field_name| fields with different
// values.
bool HeadersContainMultipleCopiesOfField(const HttpResponseHeaders& headers,
                                         const std::string& field_name) {
  size_t it = 0;
  std::string field_value;
  if (!headers.EnumerateHeader(&it, field_name, &field_value))
    return false;
  // There's at least one |field_name| header.  Check if there are any more
  // such headers, and if so, return true if they have different values.
  std::string field_value2;
  while (headers.EnumerateHeader(&it, field_name, &field_value2)) {
    if (field_value != field_value2)
      return true;
  }
  return false;
}

std::unique_ptr<base::Value> NetLogSendRequestBodyCallback(
    uint64_t length,
    bool is_chunked,
    bool did_merge,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("length", static_cast<int>(length));
  dict->SetBoolean("is_chunked", is_chunked);
  dict->SetBoolean("did_merge", did_merge);
  return std::move(dict);
}

// Returns true if |error_code| is an error for which we give the server a
// chance to send a body containing error information, if the error was received
// while trying to upload a request body.
bool ShouldTryReadingOnUploadError(int error_code) {
  return (error_code == ERR_CONNECTION_RESET);
}

}  // namespace

// Similar to DrainableIOBuffer(), but this version comes with its own
// storage. The motivation is to avoid repeated allocations of
// DrainableIOBuffer.
//
// Example:
//
// scoped_refptr<SeekableIOBuffer> buf =
//     base::MakeRefCounted<SeekableIOBuffer>(1024);
// // capacity() == 1024. size() == BytesRemaining() == BytesConsumed() == 0.
// // data() points to the beginning of the buffer.
//
// // Read() takes an IOBuffer.
// int bytes_read = some_reader->Read(buf, buf->capacity());
// buf->DidAppend(bytes_read);
// // size() == BytesRemaining() == bytes_read. data() is unaffected.
//
// while (buf->BytesRemaining() > 0) {
//   // Write() takes an IOBuffer. If it takes const char*, we could
///  // simply use the regular IOBuffer like buf->data() + offset.
//   int bytes_written = Write(buf, buf->BytesRemaining());
//   buf->DidConsume(bytes_written);
// }
// // BytesRemaining() == 0. BytesConsumed() == size().
// // data() points to the end of the consumed bytes (exclusive).
//
// // If you want to reuse the buffer, be sure to clear the buffer.
// buf->Clear();
// // size() == BytesRemaining() == BytesConsumed() == 0.
// // data() points to the beginning of the buffer.
//
class HttpStreamParser::SeekableIOBuffer : public IOBuffer {
 public:
  explicit SeekableIOBuffer(int capacity)
    : IOBuffer(capacity),
      real_data_(data_),
      capacity_(capacity),
      size_(0),
      used_(0) {
  }

  // DidConsume() changes the |data_| pointer so that |data_| always points
  // to the first unconsumed byte.
  void DidConsume(int bytes) {
    SetOffset(used_ + bytes);
  }

  // Returns the number of unconsumed bytes.
  int BytesRemaining() const {
    return size_ - used_;
  }

  // Seeks to an arbitrary point in the buffer. The notion of bytes consumed
  // and remaining are updated appropriately.
  void SetOffset(int bytes) {
    DCHECK_GE(bytes, 0);
    DCHECK_LE(bytes, size_);
    used_ = bytes;
    data_ = real_data_ + used_;
  }

  // Called after data is added to the buffer. Adds |bytes| added to
  // |size_|. data() is unaffected.
  void DidAppend(int bytes) {
    DCHECK_GE(bytes, 0);
    DCHECK_GE(size_ + bytes, 0);
    DCHECK_LE(size_ + bytes, capacity_);
    size_ += bytes;
  }

  // Changes the logical size to 0, and the offset to 0.
  void Clear() {
    size_ = 0;
    SetOffset(0);
  }

  // Returns the logical size of the buffer (i.e the number of bytes of data
  // in the buffer).
  int size() const { return size_; }

  // Returns the capacity of the buffer. The capacity is the size used when
  // the object is created.
  int capacity() const { return capacity_; }

 private:
  ~SeekableIOBuffer() override {
    // data_ will be deleted in IOBuffer::~IOBuffer().
    data_ = real_data_;
  }

  char* real_data_;
  const int capacity_;
  int size_;
  int used_;
};

// 2 CRLFs + max of 8 hex chars.
const size_t HttpStreamParser::kChunkHeaderFooterSize = 12;

HttpStreamParser::HttpStreamParser(StreamSocket* stream_socket,
                                   bool connection_is_reused,
                                   const HttpRequestInfo* request,
                                   GrowableIOBuffer* read_buffer,
                                   const NetLogWithSource& net_log)
    : io_state_(STATE_NONE),
      request_(request),
      request_headers_(nullptr),
      request_headers_length_(0),
      http_09_on_non_default_ports_enabled_(false),
      read_buf_(read_buffer),
      read_buf_unused_offset_(0),
      response_header_start_offset_(std::string::npos),
      received_bytes_(0),
      sent_bytes_(0),
      response_(nullptr),
      response_body_length_(-1),
      response_is_keep_alive_(false),
      response_body_read_(0),
      user_read_buf_(nullptr),
      user_read_buf_len_(0),
      stream_socket_(stream_socket),
      connection_is_reused_(connection_is_reused),
      net_log_(net_log),
      sent_last_chunk_(false),
      upload_error_(OK),
      weak_ptr_factory_(this) {
  io_callback_ = base::BindRepeating(&HttpStreamParser::OnIOComplete,
                                     weak_ptr_factory_.GetWeakPtr());
}

HttpStreamParser::~HttpStreamParser() = default;

int HttpStreamParser::SendRequest(
    const std::string& request_line,
    const HttpRequestHeaders& headers,
    const NetworkTrafficAnnotationTag& traffic_annotation,
    HttpResponseInfo* response,
    CompletionOnceCallback callback) {
  DCHECK_EQ(STATE_NONE, io_state_);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());
  DCHECK(response);

  net_log_.AddEvent(NetLogEventType::HTTP_TRANSACTION_SEND_REQUEST_HEADERS,
                    base::Bind(&HttpRequestHeaders::NetLogCallback,
                               base::Unretained(&headers), &request_line));

  DVLOG(1) << __func__ << "() request_line = \"" << request_line << "\""
           << " headers = \"" << headers.ToString() << "\"";
  traffic_annotation_ = MutableNetworkTrafficAnnotationTag(traffic_annotation);
  response_ = response;

  // Put the peer's IP address and port into the response.
  IPEndPoint ip_endpoint;
  int result = stream_socket_->GetPeerAddress(&ip_endpoint);
  if (result != OK)
    return result;
  response_->remote_endpoint = ip_endpoint;

  std::string request = request_line + headers.ToString();
  request_headers_length_ = request.size();

  if (request_->upload_data_stream != nullptr) {
    request_body_send_buf_ =
        base::MakeRefCounted<SeekableIOBuffer>(kRequestBodyBufferSize);
    if (request_->upload_data_stream->is_chunked()) {
      // Read buffer is adjusted to guarantee that |request_body_send_buf_| is
      // large enough to hold the encoded chunk.
      request_body_read_buf_ = base::MakeRefCounted<SeekableIOBuffer>(
          kRequestBodyBufferSize - kChunkHeaderFooterSize);
    } else {
      // No need to encode request body, just send the raw data.
      request_body_read_buf_ = request_body_send_buf_;
    }
  }

  io_state_ = STATE_SEND_HEADERS;

  // If we have a small request body, then we'll merge with the headers into a
  // single write.
  bool did_merge = false;
  if (ShouldMergeRequestHeadersAndBody(request, request_->upload_data_stream)) {
    int merged_size = static_cast<int>(
        request_headers_length_ + request_->upload_data_stream->size());
    scoped_refptr<IOBuffer> merged_request_headers_and_body =
        base::MakeRefCounted<IOBuffer>(merged_size);
    // We'll repurpose |request_headers_| to store the merged headers and
    // body.
    request_headers_ = base::MakeRefCounted<DrainableIOBuffer>(
        merged_request_headers_and_body, merged_size);

    memcpy(request_headers_->data(), request.data(), request_headers_length_);
    request_headers_->DidConsume(request_headers_length_);

    uint64_t todo = request_->upload_data_stream->size();
    while (todo) {
      int consumed = request_->upload_data_stream->Read(
          request_headers_.get(), static_cast<int>(todo),
          CompletionOnceCallback());
      // Read() must succeed synchronously if not chunked and in memory.
      DCHECK_GT(consumed, 0);
      request_headers_->DidConsume(consumed);
      todo -= consumed;
    }
    DCHECK(request_->upload_data_stream->IsEOF());
    // Reset the offset, so the buffer can be read from the beginning.
    request_headers_->SetOffset(0);
    did_merge = true;

    net_log_.AddEvent(NetLogEventType::HTTP_TRANSACTION_SEND_REQUEST_BODY,
                      base::Bind(&NetLogSendRequestBodyCallback,
                                 request_->upload_data_stream->size(),
                                 false, /* not chunked */
                                 true /* merged */));
  }

  if (!did_merge) {
    // If we didn't merge the body with the headers, then |request_headers_|
    // contains just the HTTP headers.
    scoped_refptr<StringIOBuffer> headers_io_buf =
        base::MakeRefCounted<StringIOBuffer>(request);
    request_headers_ = base::MakeRefCounted<DrainableIOBuffer>(
        std::move(headers_io_buf), request.size());
  }

  result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    callback_ = std::move(callback);

  return result > 0 ? OK : result;
}

int HttpStreamParser::ConfirmHandshake(CompletionOnceCallback callback) {
  int ret = stream_socket_->ConfirmHandshake(
      base::BindOnce(&HttpStreamParser::RunConfirmHandshakeCallback,
                     weak_ptr_factory_.GetWeakPtr()));
  if (ret == ERR_IO_PENDING)
    confirm_handshake_callback_ = std::move(callback);
  return ret;
}

int HttpStreamParser::ReadResponseHeaders(CompletionOnceCallback callback) {
  DCHECK(io_state_ == STATE_NONE || io_state_ == STATE_DONE);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());
  DCHECK_EQ(0, read_buf_unused_offset_);
  DCHECK(SendRequestBuffersEmpty());

  // This function can be called with io_state_ == STATE_DONE if the
  // connection is closed after seeing just a 1xx response code.
  if (io_state_ == STATE_DONE)
    return ERR_CONNECTION_CLOSED;

  int result = OK;
  io_state_ = STATE_READ_HEADERS;

  if (read_buf_->offset() > 0) {
    // Simulate the state where the data was just read from the socket.
    result = read_buf_->offset();
    read_buf_->set_offset(0);
  }
  if (result > 0)
    io_state_ = STATE_READ_HEADERS_COMPLETE;

  result = DoLoop(result);
  if (result == ERR_IO_PENDING)
    callback_ = std::move(callback);

  return result > 0 ? OK : result;
}

int HttpStreamParser::ReadResponseBody(IOBuffer* buf,
                                       int buf_len,
                                       CompletionOnceCallback callback) {
  DCHECK(io_state_ == STATE_NONE || io_state_ == STATE_DONE);
  DCHECK(callback_.is_null());
  DCHECK(!callback.is_null());
  DCHECK_LE(buf_len, kMaxBufSize);
  DCHECK(SendRequestBuffersEmpty());
  // Added to investigate crbug.com/499663.
  CHECK(buf);

  if (io_state_ == STATE_DONE)
    return OK;

  user_read_buf_ = buf;
  user_read_buf_len_ = buf_len;
  io_state_ = STATE_READ_BODY;

  // Invalidate HttpRequestInfo pointer. This is to allow the stream to be
  // shared across multiple consumers.
  // It is safe to reset it at this point since request_->upload_data_stream
  // is also not needed anymore.
  request_ = nullptr;

  int result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    callback_ = std::move(callback);

  return result;
}

void HttpStreamParser::OnIOComplete(int result) {
  result = DoLoop(result);

  // The client callback can do anything, including destroying this class,
  // so any pending callback must be issued after everything else is done.
  if (result != ERR_IO_PENDING && !callback_.is_null()) {
    base::ResetAndReturn(&callback_).Run(result);
  }
}

int HttpStreamParser::DoLoop(int result) {
  do {
    DCHECK_NE(ERR_IO_PENDING, result);
    DCHECK_NE(STATE_DONE, io_state_);
    DCHECK_NE(STATE_NONE, io_state_);
    State state = io_state_;
    io_state_ = STATE_NONE;
    switch (state) {
      case STATE_SEND_HEADERS:
        DCHECK_EQ(OK, result);
        result = DoSendHeaders();
        DCHECK_NE(STATE_NONE, io_state_);
        break;
      case STATE_SEND_HEADERS_COMPLETE:
        result = DoSendHeadersComplete(result);
        DCHECK_NE(STATE_NONE, io_state_);
        break;
      case STATE_SEND_BODY:
        DCHECK_EQ(OK, result);
        result = DoSendBody();
        DCHECK_NE(STATE_NONE, io_state_);
        break;
      case STATE_SEND_BODY_COMPLETE:
        result = DoSendBodyComplete(result);
        DCHECK_NE(STATE_NONE, io_state_);
        break;
      case STATE_SEND_REQUEST_READ_BODY_COMPLETE:
        result = DoSendRequestReadBodyComplete(result);
        DCHECK_NE(STATE_NONE, io_state_);
        break;
      case STATE_SEND_REQUEST_COMPLETE:
        result = DoSendRequestComplete(result);
        break;
      case STATE_READ_HEADERS:
        net_log_.BeginEvent(NetLogEventType::HTTP_STREAM_PARSER_READ_HEADERS);
        DCHECK_GE(result, 0);
        result = DoReadHeaders();
        break;
      case STATE_READ_HEADERS_COMPLETE:
        result = DoReadHeadersComplete(result);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_STREAM_PARSER_READ_HEADERS, result);
        break;
      case STATE_READ_BODY:
        DCHECK_GE(result, 0);
        result = DoReadBody();
        break;
      case STATE_READ_BODY_COMPLETE:
        result = DoReadBodyComplete(result);
        break;
      default:
        NOTREACHED();
        break;
    }
  } while (result != ERR_IO_PENDING &&
           (io_state_ != STATE_DONE && io_state_ != STATE_NONE));

  return result;
}

int HttpStreamParser::DoSendHeaders() {
  int bytes_remaining = request_headers_->BytesRemaining();
  DCHECK_GT(bytes_remaining, 0);

  // Record our best estimate of the 'request time' as the time when we send
  // out the first bytes of the request headers.
  if (bytes_remaining == request_headers_->size())
    response_->request_time = base::Time::Now();

  io_state_ = STATE_SEND_HEADERS_COMPLETE;
  return stream_socket_->Write(
      request_headers_.get(), bytes_remaining, io_callback_,
      NetworkTrafficAnnotationTag(traffic_annotation_));
}

int HttpStreamParser::DoSendHeadersComplete(int result) {
  if (result < 0) {
    // In the unlikely case that the headers and body were merged, all the
    // the headers were sent, but not all of the body way, and |result| is
    // an error that this should try reading after, stash the error for now and
    // act like the request was successfully sent.
    io_state_ = STATE_SEND_REQUEST_COMPLETE;
    if (request_headers_->BytesConsumed() >= request_headers_length_ &&
        ShouldTryReadingOnUploadError(result)) {
      upload_error_ = result;
      return OK;
    }
    return result;
  }

  sent_bytes_ += result;
  request_headers_->DidConsume(result);
  if (request_headers_->BytesRemaining() > 0) {
    io_state_ = STATE_SEND_HEADERS;
    return OK;
  }

  if (request_->upload_data_stream != nullptr &&
      (request_->upload_data_stream->is_chunked() ||
       // !IsEOF() indicates that the body wasn't merged.
       (request_->upload_data_stream->size() > 0 &&
        !request_->upload_data_stream->IsEOF()))) {
    net_log_.AddEvent(NetLogEventType::HTTP_TRANSACTION_SEND_REQUEST_BODY,
                      base::Bind(&NetLogSendRequestBodyCallback,
                                 request_->upload_data_stream->size(),
                                 request_->upload_data_stream->is_chunked(),
                                 false /* not merged */));
    io_state_ = STATE_SEND_BODY;
    return OK;
  }

  // Finished sending the request.
  io_state_ = STATE_SEND_REQUEST_COMPLETE;
  return OK;
}

int HttpStreamParser::DoSendBody() {
  if (request_body_send_buf_->BytesRemaining() > 0) {
    io_state_ = STATE_SEND_BODY_COMPLETE;
    return stream_socket_->Write(
        request_body_send_buf_.get(), request_body_send_buf_->BytesRemaining(),
        io_callback_, NetworkTrafficAnnotationTag(traffic_annotation_));
  }

  if (request_->upload_data_stream->is_chunked() && sent_last_chunk_) {
    // Finished sending the request.
    io_state_ = STATE_SEND_REQUEST_COMPLETE;
    return OK;
  }

  request_body_read_buf_->Clear();
  io_state_ = STATE_SEND_REQUEST_READ_BODY_COMPLETE;
  return request_->upload_data_stream->Read(
      request_body_read_buf_.get(), request_body_read_buf_->capacity(),
      base::BindOnce(&HttpStreamParser::OnIOComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

int HttpStreamParser::DoSendBodyComplete(int result) {
  if (result < 0) {
    // If |result| is an error that this should try reading after, stash the
    // error for now and act like the request was successfully sent.
    io_state_ = STATE_SEND_REQUEST_COMPLETE;
    if (ShouldTryReadingOnUploadError(result)) {
      upload_error_ = result;
      return OK;
    }
    return result;
  }

  sent_bytes_ += result;
  request_body_send_buf_->DidConsume(result);

  io_state_ = STATE_SEND_BODY;
  return OK;
}

int HttpStreamParser::DoSendRequestReadBodyComplete(int result) {
  // |result| is the result of read from the request body from the last call to
  // DoSendBody().
  if (result < 0) {
    io_state_ = STATE_SEND_REQUEST_COMPLETE;
    return result;
  }

  // Chunked data needs to be encoded.
  if (request_->upload_data_stream->is_chunked()) {
    if (result == 0) {  // Reached the end.
      DCHECK(request_->upload_data_stream->IsEOF());
      sent_last_chunk_ = true;
    }
    // Encode the buffer as 1 chunk.
    const base::StringPiece payload(request_body_read_buf_->data(), result);
    request_body_send_buf_->Clear();
    result = EncodeChunk(payload,
                         request_body_send_buf_->data(),
                         request_body_send_buf_->capacity());
  }

  if (result == 0) {  // Reached the end.
    // Reaching EOF means we can finish sending request body unless the data is
    // chunked. (i.e. No need to send the terminal chunk.)
    DCHECK(request_->upload_data_stream->IsEOF());
    DCHECK(!request_->upload_data_stream->is_chunked());
    // Finished sending the request.
    io_state_ = STATE_SEND_REQUEST_COMPLETE;
  } else if (result > 0) {
    request_body_send_buf_->DidAppend(result);
    result = 0;
    io_state_ = STATE_SEND_BODY;
  }
  return result;
}

int HttpStreamParser::DoSendRequestComplete(int result) {
  DCHECK_NE(result, ERR_IO_PENDING);
  request_headers_ = nullptr;
  request_body_send_buf_ = nullptr;
  request_body_read_buf_ = nullptr;

  return result;
}

int HttpStreamParser::DoReadHeaders() {
  io_state_ = STATE_READ_HEADERS_COMPLETE;

  // Grow the read buffer if necessary.
  if (read_buf_->RemainingCapacity() == 0)
    read_buf_->SetCapacity(read_buf_->capacity() + kHeaderBufInitialSize);

  // http://crbug.com/16371: We're seeing |user_buf_->data()| return NULL.
  // See if the user is passing in an IOBuffer with a NULL |data_|.
  CHECK(read_buf_->data());

  return stream_socket_->Read(read_buf_.get(), read_buf_->RemainingCapacity(),
                              io_callback_);
}

int HttpStreamParser::DoReadHeadersComplete(int result) {
  // DoReadHeadersComplete is called with the result of Socket::Read, which is a
  // (byte_count | error), and returns (error | OK).

  result = HandleReadHeaderResult(result);

  // TODO(mmenke):  The code below is ugly and hacky.  A much better and more
  // flexible long term solution would be to separate out the read and write
  // loops, though this would involve significant changes, both here and
  // elsewhere (WebSockets, for instance).

  // If still reading the headers, or there was no error uploading the request
  // body, just return the result.
  if (io_state_ == STATE_READ_HEADERS || upload_error_ == OK)
    return result;

  // If the result is ERR_IO_PENDING, |io_state_| should be STATE_READ_HEADERS.
  DCHECK_NE(ERR_IO_PENDING, result);

  // On errors, use the original error received when sending the request.
  // The main cases where these are different is when there's a header-related
  // error code, or when there's an ERR_CONNECTION_CLOSED, which can result in
  // special handling of partial responses and HTTP/0.9 responses.
  if (result < 0) {
    // Nothing else to do.  In the HTTP/0.9 or only partial headers received
    // cases, can normally go to other states after an error reading headers.
    io_state_ = STATE_DONE;
    // Don't let caller see the headers.
    response_->headers = nullptr;
    return upload_error_;
  }

  // Skip over 1xx responses as usual, and allow 4xx/5xx error responses to
  // override the error received while uploading the body.
  int response_code_class = response_->headers->response_code() / 100;
  if (response_code_class == 1 || response_code_class == 4 ||
      response_code_class == 5) {
    return result;
  }

  // All other status codes are not allowed after an error during upload, to
  // make sure the consumer has some indication there was an error.

  // Nothing else to do.
  io_state_ = STATE_DONE;
  // Don't let caller see the headers.
  response_->headers = nullptr;
  return upload_error_;
}

int HttpStreamParser::DoReadBody() {
  io_state_ = STATE_READ_BODY_COMPLETE;

  // Added to investigate crbug.com/499663.
  CHECK(user_read_buf_.get());

  // There may be some data left over from reading the response headers.
  if (read_buf_->offset()) {
    int available = read_buf_->offset() - read_buf_unused_offset_;
    if (available) {
      CHECK_GT(available, 0);
      int bytes_from_buffer = std::min(available, user_read_buf_len_);
      memcpy(user_read_buf_->data(),
             read_buf_->StartOfBuffer() + read_buf_unused_offset_,
             bytes_from_buffer);
      read_buf_unused_offset_ += bytes_from_buffer;
      if (bytes_from_buffer == available) {
        read_buf_->SetCapacity(0);
        read_buf_unused_offset_ = 0;
      }
      return bytes_from_buffer;
    } else {
      read_buf_->SetCapacity(0);
      read_buf_unused_offset_ = 0;
    }
  }

  // Check to see if we're done reading.
  if (IsResponseBodyComplete())
    return 0;

  DCHECK_EQ(0, read_buf_->offset());
  return stream_socket_->Read(user_read_buf_.get(), user_read_buf_len_,
                              io_callback_);
}

int HttpStreamParser::DoReadBodyComplete(int result) {
  // When the connection is closed, there are numerous ways to interpret it.
  //
  //  - If a Content-Length header is present and the body contains exactly that
  //    number of bytes at connection close, the response is successful.
  //
  //  - If a Content-Length header is present and the body contains fewer bytes
  //    than promised by the header at connection close, it may indicate that
  //    the connection was closed prematurely, or it may indicate that the
  //    server sent an invalid Content-Length header. Unfortunately, the invalid
  //    Content-Length header case does occur in practice and other browsers are
  //    tolerant of it. We choose to treat it as an error for now, but the
  //    download system treats it as a non-error, and URLRequestHttpJob also
  //    treats it as OK if the Content-Length is the post-decoded body content
  //    length.
  //
  //  - If chunked encoding is used and the terminating chunk has been processed
  //    when the connection is closed, the response is successful.
  //
  //  - If chunked encoding is used and the terminating chunk has not been
  //    processed when the connection is closed, it may indicate that the
  //    connection was closed prematurely or it may indicate that the server
  //    sent an invalid chunked encoding. We choose to treat it as
  //    an invalid chunked encoding.
  //
  //  - If a Content-Length is not present and chunked encoding is not used,
  //    connection close is the only way to signal that the response is
  //    complete. Unfortunately, this also means that there is no way to detect
  //    early close of a connection. No error is returned.
  if (result == 0 && !IsResponseBodyComplete() && CanFindEndOfResponse()) {
    if (chunked_decoder_.get())
      result = ERR_INCOMPLETE_CHUNKED_ENCODING;
    else
      result = ERR_CONTENT_LENGTH_MISMATCH;
  }

  if (result > 0)
    received_bytes_ += result;

  // Filter incoming data if appropriate.  FilterBuf may return an error.
  if (result > 0 && chunked_decoder_.get()) {
    result = chunked_decoder_->FilterBuf(user_read_buf_->data(), result);
    if (result == 0 && !chunked_decoder_->reached_eof()) {
      // Don't signal completion of the Read call yet or else it'll look like
      // we received end-of-file.  Wait for more data.
      io_state_ = STATE_READ_BODY;
      return OK;
    }
  }

  if (result > 0)
    response_body_read_ += result;

  if (result <= 0 || IsResponseBodyComplete()) {
    io_state_ = STATE_DONE;

    // Save the overflow data, which can be in two places.  There may be
    // some left over in |user_read_buf_|, plus there may be more
    // in |read_buf_|.  But the part left over in |user_read_buf_| must have
    // come from the |read_buf_|, so there's room to put it back at the
    // start first.
    int additional_save_amount = read_buf_->offset() - read_buf_unused_offset_;
    int save_amount = 0;
    if (chunked_decoder_.get()) {
      save_amount = chunked_decoder_->bytes_after_eof();
    } else if (response_body_length_ >= 0) {
      int64_t extra_data_read = response_body_read_ - response_body_length_;
      if (extra_data_read > 0) {
        save_amount = static_cast<int>(extra_data_read);
        if (result > 0)
          result -= save_amount;
      }
    }

    CHECK_LE(save_amount + additional_save_amount, kMaxBufSize);
    if (read_buf_->capacity() < save_amount + additional_save_amount) {
      read_buf_->SetCapacity(save_amount + additional_save_amount);
    }

    if (save_amount) {
      received_bytes_ -= save_amount;
      memcpy(read_buf_->StartOfBuffer(), user_read_buf_->data() + result,
             save_amount);
    }
    read_buf_->set_offset(save_amount);
    if (additional_save_amount) {
      memmove(read_buf_->data(),
              read_buf_->StartOfBuffer() + read_buf_unused_offset_,
              additional_save_amount);
      read_buf_->set_offset(save_amount + additional_save_amount);
    }
    read_buf_unused_offset_ = 0;
  } else {
    // Now waiting for more of the body to be read.
    user_read_buf_ = nullptr;
    user_read_buf_len_ = 0;
  }

  return result;
}

int HttpStreamParser::HandleReadHeaderResult(int result) {
  DCHECK_EQ(0, read_buf_unused_offset_);

  if (result == 0)
    result = ERR_CONNECTION_CLOSED;

  if (result == ERR_CONNECTION_CLOSED) {
    // The connection closed without getting any more data.
    if (read_buf_->offset() == 0) {
      io_state_ = STATE_DONE;
      // If the connection has not been reused, it may have been a 0-length
      // HTTP/0.9 responses, but it was most likely an error, so just return
      // ERR_EMPTY_RESPONSE instead. If the connection was reused, just pass
      // on the original connection close error, as rather than being an
      // empty HTTP/0.9 response it's much more likely the server closed the
      // socket before it received the request.
      if (!connection_is_reused_)
        return ERR_EMPTY_RESPONSE;
      return result;
    }

    // Accepting truncated headers over HTTPS is a potential security
    // vulnerability, so just return an error in that case.
    //
    // If response_header_start_offset_ is std::string::npos, this may be a < 8
    // byte HTTP/0.9 response. However, accepting such a response over HTTPS
    // would allow a MITM to truncate an HTTP/1.x status line to look like a
    // short HTTP/0.9 response if the peer put a record boundary at the first 8
    // bytes. To ensure that all response headers received over HTTPS are
    // pristine, treat such responses as errors.
    //
    // TODO(mmenke):  Returning ERR_RESPONSE_HEADERS_TRUNCATED when a response
    // looks like an HTTP/0.9 response is weird.  Should either come up with
    // another error code, or, better, disable HTTP/0.9 over HTTPS (and give
    // that a new error code).
    if (request_->url.SchemeIsCryptographic()) {
      io_state_ = STATE_DONE;
      return ERR_RESPONSE_HEADERS_TRUNCATED;
    }

    // Parse things as well as we can and let the caller decide what to do.
    int end_offset;
    if (response_header_start_offset_ != std::string::npos) {
      // The response looks to be a truncated set of HTTP headers.
      io_state_ = STATE_READ_BODY_COMPLETE;
      end_offset = read_buf_->offset();
    } else {
      // The response is apparently using HTTP/0.9.  Treat the entire response
      // as the body.
      end_offset = 0;
    }
    int rv = ParseResponseHeaders(end_offset);
    if (rv < 0)
      return rv;
    return result;
  }

  if (result < 0) {
    io_state_ = STATE_DONE;
    return result;
  }

  // Record our best estimate of the 'response time' as the time when we read
  // the first bytes of the response headers.
  if (read_buf_->offset() == 0)
    response_->response_time = base::Time::Now();

  // For |response_start_time_|, use the time that we received the first byte of
  // *any* response- including 1XX, as per the resource timing spec for
  // responseStart (see note at
  // https://www.w3.org/TR/resource-timing-2/#dom-performanceresourcetiming-responsestart).
  if (response_start_time_.is_null())
    response_start_time_ = base::TimeTicks::Now();

  read_buf_->set_offset(read_buf_->offset() + result);
  DCHECK_LE(read_buf_->offset(), read_buf_->capacity());
  DCHECK_GT(result, 0);

  int end_of_header_offset = FindAndParseResponseHeaders(result);

  // Note: -1 is special, it indicates we haven't found the end of headers.
  // Anything less than -1 is a net::Error, so we bail out.
  if (end_of_header_offset < -1)
    return end_of_header_offset;

  if (end_of_header_offset == -1) {
    io_state_ = STATE_READ_HEADERS;
    // Prevent growing the headers buffer indefinitely.
    if (read_buf_->offset() >= kMaxHeaderBufSize) {
      io_state_ = STATE_DONE;
      return ERR_RESPONSE_HEADERS_TOO_BIG;
    }
  } else {
    CalculateResponseBodySize();

    // If the body is zero length, the caller may not call ReadResponseBody,
    // which is where any extra data is copied to read_buf_, so we move the
    // data here.
    if (response_body_length_ == 0) {
      int extra_bytes = read_buf_->offset() - end_of_header_offset;
      if (extra_bytes) {
        CHECK_GT(extra_bytes, 0);
        memmove(read_buf_->StartOfBuffer(),
                read_buf_->StartOfBuffer() + end_of_header_offset,
                extra_bytes);
      }
      read_buf_->SetCapacity(extra_bytes);
      if (response_->headers->response_code() / 100 == 1) {
        // After processing a 1xx response, the caller will ask for the next
        // header, so reset state to support that. We don't completely ignore a
        // 1xx response because it cannot be returned in reply to a CONNECT
        // request so we return OK here, which lets the caller inspect the
        // response and reject it in the event that we're setting up a CONNECT
        // tunnel.
        response_header_start_offset_ = std::string::npos;
        response_body_length_ = -1;
        // Now waiting for the second set of headers to be read.
      } else {
        // Only set keep-alive based on final set of headers.
        response_is_keep_alive_ = response_->headers->IsKeepAlive();

        io_state_ = STATE_DONE;
      }
      return OK;
    }

    // Only set keep-alive based on final set of headers.
    response_is_keep_alive_ = response_->headers->IsKeepAlive();

    // Note where the headers stop.
    read_buf_unused_offset_ = end_of_header_offset;
    // Now waiting for the body to be read.
  }
  return OK;
}

void HttpStreamParser::RunConfirmHandshakeCallback(int rv) {
  std::move(confirm_handshake_callback_).Run(rv);
}

int HttpStreamParser::FindAndParseResponseHeaders(int new_bytes) {
  DCHECK_GT(new_bytes, 0);
  DCHECK_EQ(0, read_buf_unused_offset_);
  size_t end_offset = std::string::npos;

  // Look for the start of the status line, if it hasn't been found yet.
  if (response_header_start_offset_ == std::string::npos) {
    response_header_start_offset_ = HttpUtil::LocateStartOfStatusLine(
        read_buf_->StartOfBuffer(), read_buf_->offset());
  }

  if (response_header_start_offset_ != std::string::npos) {
    // LocateEndOfHeaders looks for two line breaks in a row (With or without
    // carriage returns). So the end of the headers includes at most the last 3
    // bytes of the buffer from the past read. This optimization avoids O(n^2)
    // performance in the case each read only returns a couple bytes. It's not
    // too important in production, but for fuzzers with memory instrumentation,
    // it's needed to avoid timing out.
    size_t lower_bound =
        (base::ClampedNumeric<size_t>(read_buf_->offset()) - new_bytes - 3)
            .RawValue();
    size_t search_start = std::max(response_header_start_offset_, lower_bound);
    end_offset = HttpUtil::LocateEndOfHeaders(
        read_buf_->StartOfBuffer(), read_buf_->offset(), search_start);
  } else if (read_buf_->offset() >= 8) {
    // Enough data to decide that this is an HTTP/0.9 response.
    // 8 bytes = (4 bytes of junk) + "http".length()
    end_offset = 0;
  }

  if (end_offset == std::string::npos)
    return -1;

  int rv = ParseResponseHeaders(end_offset);
  if (rv < 0)
    return rv;
  return end_offset;
}

int HttpStreamParser::ParseResponseHeaders(int end_offset) {
  scoped_refptr<HttpResponseHeaders> headers;
  DCHECK_EQ(0, read_buf_unused_offset_);

  if (response_header_start_offset_ != std::string::npos) {
    received_bytes_ += end_offset;
    headers = HttpResponseHeaders::TryToCreate(
        base::StringPiece(read_buf_->StartOfBuffer(), end_offset));
    if (!headers)
      return net::ERR_INVALID_HTTP_RESPONSE;
  } else {
    // Enough data was read -- there is no status line, so this is HTTP/0.9, or
    // the server is broken / doesn't speak HTTP.

    // If the port is not the default for the scheme, assume it's not a real
    // HTTP/0.9 response, and fail the request.
    base::StringPiece scheme = request_->url.scheme_piece();
    if (!http_09_on_non_default_ports_enabled_ &&
        url::DefaultPortForScheme(scheme.data(), scheme.length()) !=
            request_->url.EffectiveIntPort()) {
      // Allow Shoutcast responses over HTTP, as it's somewhat common and relies
      // on HTTP/0.9 on weird ports to work.
      // See
      // https://groups.google.com/a/chromium.org/forum/#!topic/blink-dev/qS63pYso4P0
      if (read_buf_->offset() < 3 || scheme != "http" ||
          !base::LowerCaseEqualsASCII(
              base::StringPiece(read_buf_->StartOfBuffer(), 3), "icy")) {
        return ERR_INVALID_HTTP_RESPONSE;
      }
    }

    headers = new HttpResponseHeaders(std::string("HTTP/0.9 200 OK"));
  }

  // Check for multiple Content-Length headers when the response is not
  // chunked-encoded.  If they exist, and have distinct values, it's a potential
  // response smuggling attack.
  if (!headers->IsChunkEncoded()) {
    if (HeadersContainMultipleCopiesOfField(*headers, "Content-Length"))
      return ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH;
  }

  // Check for multiple Content-Disposition or Location headers.  If they exist,
  // it's also a potential response smuggling attack.
  if (HeadersContainMultipleCopiesOfField(*headers, "Content-Disposition"))
    return ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION;
  if (HeadersContainMultipleCopiesOfField(*headers, "Location"))
    return ERR_RESPONSE_HEADERS_MULTIPLE_LOCATION;

  response_->headers = headers;
  if (headers->GetHttpVersion() == HttpVersion(0, 9)) {
    response_->connection_info = HttpResponseInfo::CONNECTION_INFO_HTTP0_9;
  } else if (headers->GetHttpVersion() == HttpVersion(1, 0)) {
    response_->connection_info = HttpResponseInfo::CONNECTION_INFO_HTTP1_0;
  } else if (headers->GetHttpVersion() == HttpVersion(1, 1)) {
    response_->connection_info = HttpResponseInfo::CONNECTION_INFO_HTTP1_1;
  }
  response_->vary_data.Init(*request_, *response_->headers);
  DVLOG(1) << __func__ << "() content_length = \""
           << response_->headers->GetContentLength() << "\n\""
           << " headers = \"" << GetResponseHeaderLines(*response_->headers)
           << "\"";
  return OK;
}

void HttpStreamParser::CalculateResponseBodySize() {
  // Figure how to determine EOF:

  // For certain responses, we know the content length is always 0. From
  // RFC 7230 Section 3.3 Message Body:
  //
  // The presence of a message body in a response depends on both the
  // request method to which it is responding and the response status code
  // (Section 3.1.2).  Responses to the HEAD request method (Section 4.3.2
  // of [RFC7231]) never include a message body because the associated
  // response header fields (e.g., Transfer-Encoding, Content-Length,
  // etc.), if present, indicate only what their values would have been if
  // the request method had been GET (Section 4.3.1 of [RFC7231]). 2xx
  // (Successful) responses to a CONNECT request method (Section 4.3.6 of
  // [RFC7231]) switch to tunnel mode instead of having a message body.
  // All 1xx (Informational), 204 (No Content), and 304 (Not Modified)
  // responses do not include a message body.  All other responses do
  // include a message body, although the body might be of zero length.
  //
  // From RFC 7231 Section 6.3.6 205 Reset Content:
  //
  // Since the 205 status code implies that no additional content will be
  // provided, a server MUST NOT generate a payload in a 205 response.
  if (response_->headers->response_code() / 100 == 1) {
    response_body_length_ = 0;
  } else {
    switch (response_->headers->response_code()) {
      case 204:  // No Content
      case 205:  // Reset Content
      case 304:  // Not Modified
        response_body_length_ = 0;
        break;
    }
  }
  if (request_->method == "HEAD")
    response_body_length_ = 0;

  if (response_body_length_ == -1) {
    // "Transfer-Encoding: chunked" trumps "Content-Length: N"
    if (response_->headers->IsChunkEncoded()) {
      chunked_decoder_.reset(new HttpChunkedDecoder());
    } else {
      response_body_length_ = response_->headers->GetContentLength();
      // If response_body_length_ is still -1, then we have to wait
      // for the server to close the connection.
    }
  }
}

bool HttpStreamParser::IsResponseBodyComplete() const {
  if (chunked_decoder_.get())
    return chunked_decoder_->reached_eof();
  if (response_body_length_ != -1)
    return response_body_read_ >= response_body_length_;

  return false;  // Must read to EOF.
}

bool HttpStreamParser::CanFindEndOfResponse() const {
  return chunked_decoder_.get() || response_body_length_ >= 0;
}

bool HttpStreamParser::IsMoreDataBuffered() const {
  return read_buf_->offset() > read_buf_unused_offset_;
}

bool HttpStreamParser::CanReuseConnection() const {
  if (!CanFindEndOfResponse())
    return false;

  if (!response_is_keep_alive_)
    return false;

  // Check if extra data was received after reading the entire response body. If
  // extra data was received, reusing the socket is not a great idea. This does
  // have the down side of papering over certain server bugs, but seems to be
  // the best option here.
  //
  // TODO(mmenke): Consider logging this - hard to decipher socket reuse
  //     behavior makes NetLogs harder to read.
  if (IsResponseBodyComplete() && IsMoreDataBuffered())
    return false;

  return stream_socket_->IsConnected();
}

void HttpStreamParser::GetSSLInfo(SSLInfo* ssl_info) {
  if (!request_->url.SchemeIsCryptographic() ||
      !stream_socket_->GetSSLInfo(ssl_info)) {
    ssl_info->Reset();
  }
}

void HttpStreamParser::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) {
  cert_request_info->Reset();
  if (request_->url.SchemeIsCryptographic())
    stream_socket_->GetSSLCertRequestInfo(cert_request_info);
}

int HttpStreamParser::EncodeChunk(const base::StringPiece& payload,
                                  char* output,
                                  size_t output_size) {
  if (output_size < payload.size() + kChunkHeaderFooterSize)
    return ERR_INVALID_ARGUMENT;

  char* cursor = output;
  // Add the header.
  const int num_chars = base::snprintf(output, output_size,
                                       "%X\r\n",
                                       static_cast<int>(payload.size()));
  cursor += num_chars;
  // Add the payload if any.
  if (payload.size() > 0) {
    memcpy(cursor, payload.data(), payload.size());
    cursor += payload.size();
  }
  // Add the trailing CRLF.
  memcpy(cursor, "\r\n", 2);
  cursor += 2;

  return cursor - output;
}

// static
bool HttpStreamParser::ShouldMergeRequestHeadersAndBody(
    const std::string& request_headers,
    const UploadDataStream* request_body) {
  if (request_body != nullptr &&
      // IsInMemory() ensures that the request body is not chunked.
      request_body->IsInMemory() && request_body->size() > 0) {
    uint64_t merged_size = request_headers.size() + request_body->size();
    if (merged_size <= kMaxMergedHeaderAndBodySize)
      return true;
  }
  return false;
}

bool HttpStreamParser::SendRequestBuffersEmpty() {
  return request_headers_ == nullptr && request_body_send_buf_ == nullptr &&
         request_body_read_buf_ == nullptr;
}

}  // namespace net
