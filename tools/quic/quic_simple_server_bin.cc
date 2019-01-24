// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A binary wrapper for QuicServer.  It listens forever on --port
// (default 6121) until it's killed or ctrl-cd to death.

#include <iostream>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task_scheduler/task_scheduler.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/quic/chromium/crypto/proof_source_chromium.h"
#include "net/quic/core/quic_packets.h"
#include "net/tools/quic/quic_http_response_cache.h"
#include "net/tools/quic/quic_simple_server.h"

// The port the quic server will listen on.
int32_t FLAGS_port = 6121;

std::unique_ptr<net::ProofSource> CreateProofSource(
    const base::FilePath& cert_path,
    const base::FilePath& key_path) {
  std::unique_ptr<net::ProofSourceChromium> proof_source(
      new net::ProofSourceChromium());
  CHECK(proof_source->Initialize(cert_path, key_path, base::FilePath()));
  return std::move(proof_source);
}

int main(int argc, char* argv[]) {
  base::TaskScheduler::CreateAndStartWithDefaultParams("quic_server");
  base::AtExitManager exit_manager;
  base::MessageLoopForIO message_loop;

  base::CommandLine::Init(argc, argv);
  base::CommandLine* line = base::CommandLine::ForCurrentProcess();

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  CHECK(logging::InitLogging(settings));

  if (line->HasSwitch("h") || line->HasSwitch("help")) {
    const char* help_str =
        "Usage: quic_server [options]\n"
        "\n"
        "Options:\n"
        "-h, --help                  show this help message and exit\n"
        "--host=<host>               specify the address of the interface to listen on.\n"
        "                            If using Mahimahi, this must be set to a non-zero value\n"
        "--port=<port>               specify the port to listen on\n"
        "--quic_response_cache_dir  directory containing response data\n"
        "                            to load\n"
        "--certificate_file=<file>   path to the certificate chain\n"
        "--key_file=<file>           path to the pkcs8 private key\n"
        "--congestion_control        type of congestion control to use:"; 
    std::cout << help_str;
    exit(0);
  }

  DLOG(INFO) << "Starting response cache initialization";
  net::QuicHttpResponseCache response_cache;
  if (line->HasSwitch("quic_response_cache_dir")) {
    response_cache.InitializeFromDirectory(
        line->GetSwitchValueASCII("quic_response_cache_dir"));
  }
  DLOG(INFO) << "Initialized response cache";

  if (line->HasSwitch("port")) {
    if (!base::StringToInt(line->GetSwitchValueASCII("port"), &FLAGS_port)) {
      LOG(ERROR) << "--port must be an integer\n";
      return 1;
    }
  }

  if (!line->HasSwitch("certificate_file")) {
    LOG(ERROR) << "missing --certificate_file";
    return 1;
  }

  if (!line->HasSwitch("key_file")) {
    LOG(ERROR) << "missing --key_file";
    return 1;
  }

  if (!line->HasSwitch("congestion_control")) {
    LOG(ERROR) << "missing --congestion_control";
    return 1;
  }

  if (!line->HasSwitch("host_ip")) {
    LOG(WARNING) << "missing --host_ip, so defaulting to 0.0.0.0. " <<
        "This will not work with Mahimahi";
  }

  net::IPAddress ip = net::IPAddress::IPv4AllZeros();
  if (line->HasSwitch("host_ip") &&
      !ip.AssignFromIPLiteral(line->GetSwitchValueASCII("host_ip"))) {
    LOG(ERROR) << "--host_ip is a malformed IP address";
    return 1;
  }
  net::QuicConfig config;
  net::QuicSimpleServer server(
      CreateProofSource(line->GetSwitchValuePath("certificate_file"),
                        line->GetSwitchValuePath("key_file")),
      config, net::QuicCryptoServerConfig::ConfigOptions(),
      net::AllSupportedTransportVersions(), &response_cache);

  std::string cc_type = line->GetSwitchValueASCII("congestion_control");
  LOG(INFO) << "CC: " << cc_type;
  if (cc_type == "bbr") {
      server.SetCongestionControlType(net::kBBR);
  } else if (cc_type == "valueFuncFast") {
      server.SetCongestionControlType(net::kValueFuncFast);
  } else if (cc_type == "valueFuncCubic") {
      server.SetCongestionControlType(net::kValueFuncCubic);
  } else if (cc_type == "valueFuncReno") {
      server.SetCongestionControlType(net::kValueFuncReno);
  } else if (cc_type == "VMAFAwareFast") {
      server.SetCongestionControlType(net::kVMAFAwareFast);
  } else if (cc_type == "VMAFAwareCubic") {
      server.SetCongestionControlType(net::kVMAFAwareCubic);
  } else if (cc_type == "VMAFAwareReno") {
      server.SetCongestionControlType(net::kVMAFAwareReno);
  } else if (cc_type == "PropSSFast") {
      server.SetCongestionControlType(net::kPropSSFast);
  } else if (cc_type == "PropSSCubic") {
      server.SetCongestionControlType(net::kPropSSCubic);
  } else if (cc_type == "fast") {
      server.SetCongestionControlType(net::kFast);
  } else if (cc_type == "cubic") {
      server.SetCongestionControlType(net::kCubicBytes);
  } else if (cc_type  == "reno") {
      server.SetCongestionControlType(net::kRenoBytes);
  } else if (cc_type == "pcc") {
      server.SetCongestionControlType(net::kPCC);
  } else {
    LOG(ERROR) << "invalid --congestion_control";
    return 1;
  }

  int rc = server.Listen(net::IPEndPoint(ip, FLAGS_port));
  if (rc < 0) {
    return 1;
  }

  base::RunLoop().Run();

  return 0;
}
