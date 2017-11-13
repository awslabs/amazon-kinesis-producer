// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#include <boost/predef.h>
// #include <getopt.h>
#include <algorithm>

#if !BOOST_OS_WINDOWS
  #include <unistd.h>
  #include <sys/resource.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

#include <regex>

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

#include <aws/auth/mutable_static_creds_provider.h>

#include <aws/kinesis/core/kinesis_producer.h>
#include <aws/utils/io_service_executor.h>
#include <aws/utils/logging.h>
#include <aws/utils/signal_handler.h>

#include "aws/utils/md5_hasher.h"
#include "platform/platform.h"

#include <aws/core/Aws.h>
#include <aws/core/utils/logging/LogLevel.h>

#ifdef WIN32
#include <windows.h>
#endif

namespace {

struct Options {
  std::string input_pipe;
  std::string output_pipe;
  std::string configuration;
  std::string kinesis_credentials;
  std::string cloudwatch_credentials;
  std::string ca_path;
  int enable_stack_trace = 0;
  Aws::Utils::Logging::LogLevel aws_log_level = Aws::Utils::Logging::LogLevel::Warn;
  boost::log::trivial::severity_level boost_log_level = boost::log::trivial::info;
};

Options options;

void usage(const std::string& program_name, const boost::program_options::options_description& desc, const std::string& error_message = "") {
  namespace po = boost::program_options;
  std::cerr << "Usage: " << program_name << " -i <input pipe> -o <output pipe> -c <configuration> -k <kinesis credentials>" << std::endl;
  if (!error_message.empty()) {
    std::cerr << "\t" << error_message << std::endl;
  }
  std::cerr << desc;
}

bool parsed_options(int argc, char* const* argv) {
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
    ("help,h", "Usage Information")
    ("input-pipe,i", po::value<std::string>())
    ("output-pipe,o", po::value<std::string>())
    ("configuration,c", po::value<std::string>())
    ("kinesis-credentials,k", po::value<std::string>())
    ("cloudwatch-credentials,w", po::value<std::string>())
    ("enable-stack-trace,t", "Enable Stack Traces (Does Nothing)");

  try {
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);

    std::string program_name = argv[0];

    if (vm.count("help")) {
      usage(program_name, desc);
      return false;
    }

    if (!vm.count("input-pipe")) {
      usage(program_name, desc, "input-pipe is required");
      return false;
    }    
    if (!vm.count("output-pipe")) {
      usage(program_name, desc, "output-pipe is required");
      return false;
    }
    if (!vm.count("configuration")) {
      usage(program_name, desc, "output-pipe is required");
      return false;
    }
    if (!vm.count("kinesis-credentials")) {
      usage(program_name, desc, "kinesis-credential is required");
      return false;
    }
    options.input_pipe = vm["input-pipe"].as<std::string>();
    options.output_pipe = vm["output-pipe"].as<std::string>();
    options.configuration = vm["configuration"].as<std::string>();
    options.kinesis_credentials = vm["kinesis-credentials"].as<std::string>();
    options.cloudwatch_credentials = vm["cloudwatch-credentials"].as<std::string>();
    options.enable_stack_trace = vm.count("enable-stack-trace");
  }
  catch (std::exception& ex) {
    std::cerr << "Unexpected exception: " << ex.what() << std::endl;
    return false;
  }

  return true;
}

void handle_log_level(std::string input_level) {
    std::string level = input_level;
    std::transform(level.begin(), level.end(), level.begin(), ::tolower);
    using AwsLog = Aws::Utils::Logging::LogLevel;
	
    using BoostLog = boost::log::trivial::severity_level;
    std::unordered_map< std::string, std::pair<AwsLog, BoostLog> > level_mapping;

    level_mapping["trace"] = std::make_pair(AwsLog::Trace, BoostLog::trace);
    level_mapping["debug"] = std::make_pair(AwsLog::Debug, BoostLog::debug);
    level_mapping["info"] = std::make_pair(AwsLog::Info, BoostLog::info);
    level_mapping["warn"] = std::make_pair(AwsLog::Warn, BoostLog::warning);
    level_mapping["error"] = std::make_pair(AwsLog::Error, BoostLog::error);
    level_mapping["fatal"] = std::make_pair(AwsLog::Fatal, BoostLog::fatal);
    //
    // Boost doesn't have the equivalent value from AWS Logging
    //
    level_mapping["off"] = std::make_pair(AwsLog::Off, BoostLog::fatal);

    auto result_level = level_mapping.find(level);
    if (result_level != level_mapping.end()) {
        options.aws_log_level = result_level->second.first;
        options.boost_log_level = result_level->second.second;
    } else {
        options.aws_log_level = AwsLog::Info;
        options.boost_log_level = BoostLog::info;
    }

}

void check_pipe(std::string& path) {
#if !BOOST_OS_WINDOWS
  struct ::stat stat;
  int code = ::stat(path.c_str(), &stat);
  if (code < 0) {
    LOG(error) << "Could not stat file \"" << path << "\", does it exist?";
    throw 1;
  }
  if (!S_ISFIFO(stat.st_mode)) {
    LOG(error) << "\"" << path
               << "\" is not a FIFO. We can only work with FIFOs.";
    throw 1;
  }
#endif
}

aws::kinesis::protobuf::Message deserialize_msg(std::string hex) {
  boost::to_upper(hex);
  std::string bytes;
  try {
    boost::algorithm::unhex(hex, std::back_inserter(bytes));
  } catch (const std::exception e) {
    throw std::runtime_error("Input is not valid hexadecimal");
  }

  aws::kinesis::protobuf::Message msg;
  if (!msg.ParseFromString(bytes)) {
    throw std::runtime_error("Could not deserialize protobuf message");
  }

  return msg;
}

std::shared_ptr<aws::kinesis::core::Configuration> get_config(std::string hex) {
  aws::kinesis::protobuf::Message msg;
  try {
    msg = deserialize_msg(hex);
  } catch (const std::exception& e) {
    LOG(error) << "Could not deserialize config: " << e.what();
    throw 1;
  }

  if (!msg.has_configuration()) {
    LOG(error) << "Protobuf message did not contain a Configuration message\n";
    throw 1;
  }

  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  try {
    config->transfer_from_protobuf_msg(msg);
  } catch (const std::exception& e) {
    LOG(error) << "Error in config: " << e.what() << "\n";
    throw 1;
  }

  return config;
}

using CredsCmd = aws::kinesis::protobuf::SetCredentials;
using CredsProvider = aws::auth::MutableStaticCredentialsProvider;

aws::kinesis::protobuf::SetCredentials create_creds_cmd(const std::string& serialized_data) {
  aws::kinesis::protobuf::Message msg;
  try {
    msg = deserialize_msg(serialized_data);
  } catch (const std::exception& e) {
    LOG(error) << "Could not deserialize credentials: " << e.what();
    throw 1;
  }
  if (!msg.has_set_credentials()) {
    LOG(error) << "Message is not a SetCredentials message";
    throw 1;
  }
  return msg.set_credentials();
}

std::shared_ptr<aws::auth::MutableStaticCredentialsProvider> create_creds(const aws::kinesis::protobuf::SetCredentials& sc) {
    return std::make_shared<CredsProvider>(sc.credentials().akid(),
                                           sc.credentials().secret_key(),
                                           sc.credentials().has_token() ?
                                               sc.credentials().token() : "");
}

std::string get_region(const aws::kinesis::core::Configuration& config) {
  if (!config.region().empty()) {
    return config.region();
  }

  Aws::Internal::EC2MetadataClient ec2_md;
  auto az = ec2_md.GetResource(
      "/latest/meta-data/placement/availability-zone/");
  auto regex = std::regex("^([a-z]+-[a-z]+-[0-9])[a-z]$",
                          std::regex::ECMAScript);
  std::smatch m;
  if (std::regex_match(az, m, regex)) {
    return m.str(1);
  }

  LOG(error) << "Could not configure the region. It was not given in the "
             << "config and we were unable to retrieve it from EC2 metadata.";
  throw 1;
}

std::pair<
    std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>,
    std::shared_ptr<aws::auth::MutableStaticCredentialsProvider>>
get_creds_providers() {

  auto kinesis_creds_cmd = create_creds_cmd(options.kinesis_credentials);
  auto kinesis_creds_provider = create_creds(kinesis_creds_cmd);

  std::shared_ptr<CredsProvider> cw_creds_provider;
  if (options.cloudwatch_credentials.empty()) {
      cw_creds_provider = create_creds(kinesis_creds_cmd);
  } else {
      cw_creds_provider = create_creds(create_creds_cmd(options.cloudwatch_credentials));
  }

  if (!kinesis_creds_provider || !cw_creds_provider) {
    LOG(error) << "Credentials are required at start up";
    throw 1;
  }

  return std::make_pair(kinesis_creds_provider, cw_creds_provider);
}

std::shared_ptr<aws::utils::Executor> get_executor() {
  int cores = aws::thread::hardware_concurrency();
  int workers = std::min(8, std::max(1, cores - 2));
  return std::make_shared<aws::utils::IoServiceExecutor>(workers);
}

std::shared_ptr<aws::kinesis::core::IpcManager>
get_ipc_manager(std::string in_file, std::string out_file) {
  check_pipe(in_file);
  check_pipe(out_file);

  auto ipc_channel =
      std::make_shared<aws::kinesis::core::detail::IpcChannel>(
          in_file,
          out_file);
  return std::make_shared<aws::kinesis::core::IpcManager>(ipc_channel);
}

void set_core_limit() {
#if !BOOST_OS_WINDOWS
  struct rlimit lim;
  int ret = getrlimit(RLIMIT_CORE, &lim);
  if (ret != 0) {
    LOG(error) << "Could not get current core file limit, err code " << ret;
    return;
  }

  rlim_t desired = 128 * 1024 * 1024;
  rlim_t target = std::max(lim.rlim_cur, std::min(desired, lim.rlim_max));
  LOG(info) << "Current core file soft limit is " << lim.rlim_cur << "; "
            << "hard limit is " << lim.rlim_max << "; "
            << "desired value is " << desired << "; "
            << "setting soft limit to " << target;
  lim.rlim_cur = target;
  ret = setrlimit(RLIMIT_CORE, &lim);
  if (ret != 0) {
    LOG(error) << "Could not set the core file limit, err code " << ret;
  }
#endif
}

std::string get_ca_path() {
  std::string p = ".";
  if (!options.ca_path.empty()) {
    p = options.ca_path;
  } else {
    auto v = std::getenv("CA_DIR");

    if (v) {
      p = v;
    }
  }
  LOG(info) << "Setting CA path to " << p;
  return p;
}

void wait_for_debugger() {
#if defined(WIN32) && defined(WAIT_FOR_DEBUGGER)
  while (!IsDebuggerPresent()) {
    std::cerr << "Awaiting debugger" << std::endl;
    Sleep(1000);
  }
#endif
}

} // namespace




int main(int argc, char* const* argv) {
  //
  // Setup platform specific configuration
  //
  aws::kinesis::platform::initialize();
  aws::utils::MD5::initialize();
  if (!parsed_options(argc, argv)) {
    return 1;
  }
  //process_options(argc, argv);
  //aws::utils::setup_logging(options.boost_log_level);
  //aws::utils::setup_aws_logging(options.aws_log_level);

  Aws::SDKOptions sdk_options;
  Aws::InitAPI(sdk_options);

  try {
    auto config = get_config(options.configuration);

    if (config->enable_core_dumps()) {
      set_core_limit();
    }

    aws::utils::set_log_level(config->log_level());

    auto executor = get_executor();
    auto region = get_region(*config);
    auto creds_providers = get_creds_providers();
    auto ipc_manager = get_ipc_manager(options.output_pipe, options.input_pipe);
    auto ca_path = get_ca_path();
    LOG(info) << "Starting up main producer";

    aws::kinesis::core::KinesisProducer kp(
        ipc_manager,
        region,
        config,
        creds_providers.first,
        creds_providers.second,
        executor,
        ca_path);

    LOG(info) << "Entering join";

    // Never returns
    kp.join();
  } catch (const std::exception& e) {
    LOG(error) << e.what();
    return 2;
  } catch (int code) {
    return code;
  }

  return 0;
}
