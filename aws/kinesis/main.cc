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

#ifndef BOOST_OS_WINDOWS
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>

#include <gperftools/profiler.h>

#include <aws/kinesis/core/kinesis_producer.h>
#include <aws/utils/io_service_executor.h>
#include <aws/http/io_service_socket.h>

namespace {

void check_pipe(const char* path) {
#ifndef BOOST_OS_WINDOWS
  struct ::stat stat;
  int code = ::stat(path, &stat);
  if (code < 0) {
    LOG(ERROR) << "Could not stat file \"" << path << "\", does it exist?";
    throw 1;
  }
  if (!S_ISFIFO(stat.st_mode)) {
    LOG(ERROR) << "\"" << path
               << "\" is not a FIFO. We can only work with FIFOs.";
    throw 1;
  }
#endif
}

void setup_logging(const aws::kinesis::core::Configuration& config) {
  google::InitGoogleLogging("");
  FLAGS_logtostderr = 1;
  FLAGS_minloglevel = 0;
  auto log_level = config.log_level();
  if (log_level == "warning") {
    FLAGS_minloglevel = 1;
  } else if (log_level == "error") {
    FLAGS_minloglevel = 2;
  }
}

std::shared_ptr<aws::kinesis::core::Configuration> get_config(std::string hex) {
  std::string serialized_config;
  boost::to_upper(hex);
  try {
    boost::algorithm::unhex(hex, std::back_inserter(serialized_config));
  } catch (const std::exception e) {
    LOG(ERROR) << "Config is not a valid base16 data string: " << hex << "\n";
    throw 1;
  }

  aws::kinesis::protobuf::Message msg;
  if (!msg.ParseFromString(serialized_config)) {
    LOG(ERROR) << "Could not deserialize config\n";
    throw 1;
  }

  if (!msg.has_configuration()) {
    LOG(ERROR) << "Protobuf message did not contain a Configuration message\n";
    throw 1;
  }

  auto config = std::make_shared<aws::kinesis::core::Configuration>();
  try {
    config->transfer_from_protobuf_msg(msg);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error in config: " << e.what() << "\n";
    throw 1;
  }

  return config;
}

std::string get_region(const aws::kinesis::core::Configuration& config,
                       const std::shared_ptr<aws::http::Ec2Metadata>& ec2_md) {
  if (!config.region().empty()) {
    return config.region();
  }

  auto ec2_region = ec2_md->get_region();
  if (!ec2_region) {
    LOG(ERROR) << "Could not configure the region. It was not given in the "
               << "config and we were unable to retrieve it from EC2 metadata.";
    throw 1;
  }
  return *ec2_region;
}

std::shared_ptr<aws::auth::AwsCredentialsProvider>
get_creds_provider(const std::shared_ptr<aws::utils::Executor>& executor,
                   const aws::kinesis::core::Configuration& config,
                   const std::shared_ptr<aws::http::Ec2Metadata>& ec2_md) {
  if (!config.aws_access_key_id().empty() &&
      !config.aws_secret_key().empty()) {
    return
        std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
            config.aws_access_key_id(),
            config.aws_secret_key());
  }

  auto provider =
      std::make_shared<aws::auth::DefaultAwsCredentialsProvider>(
          executor,
          ec2_md);
  aws::utils::sleep_for(std::chrono::milliseconds(150));
  if (!provider->get_credentials()) {
    LOG(ERROR) << "Could not retrieve credentials from anywhere.";
    throw 1;
  }
  return provider;
}

std::shared_ptr<aws::utils::Executor> get_executor() {
  int cores = aws::thread::hardware_concurrency();
  int workers = std::min(8, std::max(1, cores - 2));
  return std::make_shared<aws::utils::IoServiceExecutor>(workers);
}

std::shared_ptr<aws::http::SocketFactory> get_socket_factory() {
  return std::make_shared<aws::http::IoServiceSocketFactory>();;
}

std::shared_ptr<aws::kinesis::core::IpcManager>
get_ipc_manager(const char* in_file, const char* out_file) {
  check_pipe(in_file);
  check_pipe(out_file);

  auto ipc_channel =
      std::make_shared<aws::kinesis::core::detail::IpcChannel>(
          in_file,
          out_file);
  return std::make_shared<aws::kinesis::core::IpcManager>(ipc_channel);
}

} // namespace

int main(int argc, const char* argv[]) {
  if (argc < 3) {
    LOG(ERROR) << "Usage:\n"
               << argv[0] << " [in_pipe] [out_pipe] [serialized config]\n";
    return 1;
  }

  try {
    auto config = get_config(argv[3]);

    setup_logging(*config);

    auto ipc_manager = get_ipc_manager(argv[1], argv[2]);
    auto executor = get_executor();
    auto socket_factory = get_socket_factory();
    auto ec2_md =
      std::make_shared<aws::http::Ec2Metadata>(executor, socket_factory);
    auto region = get_region(*config, ec2_md);
    auto creds_provider = get_creds_provider(executor, *config, ec2_md);

    aws::kinesis::core::KinesisProducer kp(
        ipc_manager,
        region,
        config,
        creds_provider,
        executor,
        socket_factory);

    // Never returns
    kp.join();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
    return 2;
  } catch (int code) {
    return code;
  }

  return 0;
}
