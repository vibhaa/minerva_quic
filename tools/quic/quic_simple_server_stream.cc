// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_simple_server_stream.h"

#include <list>
#include <utility>

#include "net/quic/core/quic_spdy_stream.h"
#include "net/quic/core/spdy_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_map_util.h"
#include "net/quic/platform/api/quic_text_utils.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/tools/quic/quic_http_response_cache.h"
#include "net/tools/quic/quic_simple_server_session.h"

using std::string;

namespace net {

QuicSimpleServerStream::QuicSimpleServerStream(
    QuicStreamId id,
    QuicSpdySession* session,
    QuicHttpResponseCache* response_cache)
    : QuicSpdyServerStreamBase(id, session),
      content_length_(-1),
      response_cache_(response_cache) {}

QuicSimpleServerStream::~QuicSimpleServerStream() {}

void QuicSimpleServerStream::OnInitialHeadersComplete(
    bool fin,
    size_t frame_len,
    const QuicHeaderList& header_list) {
  QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (!SpdyUtils::CopyAndValidateHeaders(header_list, &content_length_,
                                         &request_headers_)) {
    QUIC_DVLOG(1) << "Invalid headers";
    SendErrorResponse();
  }
  ConsumeHeaderList();
}

void QuicSimpleServerStream::OnTrailingHeadersComplete(
    bool fin,
    size_t frame_len,
    const QuicHeaderList& header_list) {
  QUIC_BUG << "Server does not support receiving Trailers.";
  SendErrorResponse();
}

void QuicSimpleServerStream::OnDataAvailable() {
  while (HasBytesToRead()) {
    struct iovec iov;
    if (GetReadableRegions(&iov, 1) == 0) {
      // No more data to read.
      break;
    }
    QUIC_DVLOG(1) << "Stream " << id() << " processed " << iov.iov_len
                  << " bytes.";
    body_.append(static_cast<char*>(iov.iov_base), iov.iov_len);

    if (content_length_ >= 0 &&
        body_.size() > static_cast<uint64_t>(content_length_)) {
      QUIC_DVLOG(1) << "Body size (" << body_.size() << ") > content length ("
                    << content_length_ << ").";
      SendErrorResponse();
      return;
    }
    MarkConsumed(iov.iov_len);
  }
  if (!sequencer()->IsClosed()) {
    sequencer()->SetUnblocked();
    return;
  }

  // If the sequencer is closed, then all the body, including the fin, has been
  // consumed.
  OnFinRead();

  if (write_side_closed() || fin_buffered()) {
    return;
  }

  SendResponse();
}

void QuicSimpleServerStream::PushResponse(
    SpdyHeaderBlock push_request_headers) {
  if (id() % 2 != 0) {
    QUIC_BUG << "Client initiated stream shouldn't be used as promised stream.";
    return;
  }
  // Change the stream state to emulate a client request.
  request_headers_ = std::move(push_request_headers);
  content_length_ = 0;
  QUIC_DVLOG(1) << "Stream " << id()
                << " ready to receive server push response.";

  // Set as if stream decompresed the headers and received fin.
  QuicSpdyStream::OnInitialHeadersComplete(/*fin=*/true, 0, QuicHeaderList());
}

std::map<string, string> QuicSimpleServerStream::ParseClientParams(string path_string) {
  // extract buffer and screen size from the path
  std::map<string, string> param_map;
  auto pos = path_string.find('?');
  if (pos == std::string::npos) {
    DLOG(INFO) << "no params";
    return param_map;
  }
  std::istringstream iss(path_string.substr(pos+1));
  string token;
  while(std::getline(iss, token, '&')) {
    int eq = token.find('=');
    string key = token.substr(0, eq);
    string val = token.substr(eq+1);
    param_map[key] = val;
    DLOG(INFO) << "Got arg " << key << " = " << val;
  }

  // Fill as many as we can here and return the param map for later code.
  string buffer = param_map["buffer"];
  string screen = param_map["screen"];
  string trace_file = param_map["trace_file"];
  string value_func = param_map["value_func"];
  string past_qoe = param_map["qoe"];
  QUIC_DVLOG(0) << "url is " << path_string << "  buffer is" << buffer << "screen size is" << screen;

  ClientData *cdata = spdy_session()->get_client_data();
  // update client data with buffer and screen size
  if (buffer.length() > 0)
    cdata->set_buffer_estimate(stod(buffer));
  if (screen.length() > 0)
    cdata->set_screen_size(stod(screen));
  if (trace_file.length() > 0)
    cdata->set_trace_file(trace_file);
  if (past_qoe.length() > 0)
    cdata->set_past_qoe(stod(past_qoe));
  if (value_func.length() > 0) {
    string vf_dir = "/home/ubuntu/value_funcs/";
    cdata->load_value_function(vf_dir + value_func);
  }
  return param_map;
}

void QuicSimpleServerStream::SendResponse() {
  if (request_headers_.empty()) {
    QUIC_DVLOG(1) << "Request headers empty.";
    SendErrorResponse();
    return;
  }

  if (content_length_ > 0 &&
      static_cast<uint64_t>(content_length_) != body_.size()) {
    QUIC_DVLOG(1) << "Content length (" << content_length_ << ") != body size ("
                  << body_.size() << ").";
    SendErrorResponse();
    return;
  }

  if (!QuicContainsKey(request_headers_, ":authority") ||
      !QuicContainsKey(request_headers_, ":path")) {
    QUIC_DVLOG(1) << "Request headers do not contain :authority or :path.";
    SendErrorResponse();
    return;
  }

  // Find response in cache. If not found, send error response.
  const QuicHttpResponseCache::Response* response = nullptr;
  auto authority = request_headers_.find(":authority");
  auto path = request_headers_.find(":path");
  if (authority != request_headers_.end() && path != request_headers_.end()) {
    response = response_cache_->GetResponse(authority->second, path->second);
  }
  if (response == nullptr) {
    QUIC_DVLOG(1) << "Response not found in cache.";
    SendNotFoundResponse();
    return;
  }

  if (response->response_type() == QuicHttpResponseCache::CLOSE_CONNECTION) {
    QUIC_DVLOG(1) << "Special response: closing connection.";
    CloseConnectionWithDetails(QUIC_NO_ERROR, "Toy server forcing close");
    return;
  }

  if (response->response_type() == QuicHttpResponseCache::IGNORE_REQUEST) {
    QUIC_DVLOG(1) << "Special response: ignoring request.";
    return;
  }

  // Examing response status, if it was not pure integer as typical h2
  // response status, send error response. Notice that
  // QuicHttpResponseCache push urls are strictly authority + path only,
  // scheme is not included (see |QuicHttpResponseCache::GetKey()|).

  string path_string = request_headers_[":path"].as_string();
  string request_url = request_headers_[":authority"].as_string() + path_string;
  std::map<string, string> param_map = ParseClientParams(path_string);

  int response_code;
  const SpdyHeaderBlock& response_headers = response->headers();
  if (!ParseHeaderStatusCode(response_headers, &response_code)) {
    auto status = response_headers.find(":status");
    if (status == response_headers.end()) {
      QUIC_LOG(WARNING)
          << ":status not present in response from cache for request "
          << request_url;
    } else {
      QUIC_LOG(WARNING) << "Illegal (non-integer) response :status from cache: "
                        << status->second << " for request " << request_url;
    }
    SendErrorResponse();
    return;
  }

  size_t pos = request_url.find(".m4s");
  if (pos != string::npos && request_url.find("Header.m4s") == string::npos) {
    uint64_t chunk_len = 0;
    int bitrate = 0;
    if (param_map["bitrate"].length() > 0) {
        bitrate = stoi(param_map["bitrate"])/1000;
    }
    QuicTextUtils::StringToUint64(response_headers.find("content-length")->second, &chunk_len);
    DLOG(INFO) << "chunk length isi " << chunk_len;
    spdy_session()->get_client_data()->new_chunk(bitrate, chunk_len);  
  }

  if (id() % 2 == 0) {
    // A server initiated stream is only used for a server push response,
    // and only 200 and 30X response codes are supported for server push.
    // This behavior mirrors the HTTP/2 implementation.
    bool is_redirection = response_code / 100 == 3;
    if (response_code != 200 && !is_redirection) {
      QUIC_LOG(WARNING) << "Response to server push request " << request_url
                        << " result in response code " << response_code;
      Reset(QUIC_STREAM_CANCELLED);
      return;
    }
  }
  std::list<QuicHttpResponseCache::ServerPushInfo> resources =
      response_cache_->GetServerPushResources(request_url);
  QUIC_DVLOG(1) << "Stream " << id() << " found " << resources.size()
                << " push resources.";

  if (!resources.empty()) {
    QuicSimpleServerSession* session =
        static_cast<QuicSimpleServerSession*>(spdy_session());
    session->PromisePushResources(request_url, resources, id(),
                                  request_headers_);
  }

  QUIC_DVLOG(1) << "Stream " << id() << " sending response.";
  SendHeadersAndBodyAndTrailers(response->headers().Clone(), response->body(),
                                response->trailers().Clone());
}

void QuicSimpleServerStream::SendNotFoundResponse() {
  QUIC_DVLOG(1) << "Stream " << id() << " sending not found response.";
  SpdyHeaderBlock headers;
  headers[":status"] = "404";
  headers["content-length"] =
      QuicTextUtils::Uint64ToString(strlen(kNotFoundResponseBody));
  SendHeadersAndBody(std::move(headers), kNotFoundResponseBody);
}

void QuicSimpleServerStream::SendErrorResponse() {
  QUIC_DVLOG(1) << "Stream " << id() << " sending error response.";
  SpdyHeaderBlock headers;
  headers[":status"] = "500";
  headers["content-length"] =
      QuicTextUtils::Uint64ToString(strlen(kErrorResponseBody));
  SendHeadersAndBody(std::move(headers), kErrorResponseBody);
}

void QuicSimpleServerStream::SendHeadersAndBody(
    SpdyHeaderBlock response_headers,
    QuicStringPiece body) {
  SendHeadersAndBodyAndTrailers(std::move(response_headers), body,
                                SpdyHeaderBlock());
}

void QuicSimpleServerStream::SendHeadersAndBodyAndTrailers(
    SpdyHeaderBlock response_headers,
    QuicStringPiece body,
    SpdyHeaderBlock response_trailers) {
  // Send the headers, with a FIN if there's nothing else to send.
  bool send_fin = (body.empty() && response_trailers.empty());
  QUIC_DLOG(INFO) << "Stream " << id() << " writing headers (fin = " << send_fin
                  << ") : " << response_headers.DebugString();
  WriteHeaders(std::move(response_headers), send_fin, nullptr);
  if (send_fin) {
    // Nothing else to send.
    return;
  }

  // Send the body, with a FIN if there's no trailers to send.
  send_fin = response_trailers.empty();
  QUIC_DLOG(INFO) << "Stream " << id() << " writing body (fin = " << send_fin
                  << ") with size: " << body.size();
  if (!body.empty() || send_fin) {
    WriteOrBufferData(body, send_fin, nullptr);
  }
  if (send_fin) {
    // Nothing else to send.
    return;
  }

  // Send the trailers. A FIN is always sent with trailers.
  QUIC_DLOG(INFO) << "Stream " << id() << " writing trailers (fin = true): "
                  << response_trailers.DebugString();
  WriteTrailers(std::move(response_trailers), nullptr);
}

const char* const QuicSimpleServerStream::kErrorResponseBody = "bad";
const char* const QuicSimpleServerStream::kNotFoundResponseBody =
    "file not found";

}  // namespace net
