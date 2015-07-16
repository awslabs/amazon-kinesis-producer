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
#include <boost/make_shared.hpp>

#include <aws/kinesis/core/kinesis_producer.h>
#include <aws/utils/io_service_executor.h>
#include <aws/http/io_service_socket.h>
#include <aws/utils/logging.h>

namespace {

void check_pipe(const char* path) {
#ifndef BOOST_OS_WINDOWS
  struct ::stat stat;
  int code = ::stat(path, &stat);
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

std::string get_region(const aws::kinesis::core::Configuration& config,
                       const std::shared_ptr<aws::http::Ec2Metadata>& ec2_md) {
  if (!config.region().empty()) {
    return config.region();
  }

  auto ec2_region = ec2_md->get_region();
  if (!ec2_region) {
    LOG(error) << "Could not configure the region. It was not given in the "
               << "config and we were unable to retrieve it from EC2 metadata.";
    throw 1;
  }
  return *ec2_region;
}

std::pair<
    std::shared_ptr<aws::auth::AwsCredentialsProvider>,
    std::shared_ptr<aws::auth::AwsCredentialsProvider>>
get_creds_providers(
    const std::shared_ptr<aws::utils::Executor>& executor,
    const std::shared_ptr<aws::http::Ec2Metadata>& ec2_md,
    int argc,
    const char** argv,
    int first_set_creds_arg) {

  std::vector<aws::kinesis::protobuf::SetCredentials> set_creds;
  for (int i = first_set_creds_arg; i < argc; i++) {
    aws::kinesis::protobuf::Message msg;
    try {
      msg = deserialize_msg(argv[i]);
    } catch (const std::exception& e) {
      LOG(error) << "Could not deserialize credentials: " << e.what();
      throw 1;
    }
    if (!msg.has_set_credentials()) {
      LOG(error) << "Message is not a SetCredentials message";
      throw 1;
    }
    set_creds.push_back(msg.set_credentials());
  }

  std::shared_ptr<aws::auth::AwsCredentialsProvider> creds_provider;
  std::shared_ptr<aws::auth::AwsCredentialsProvider> metrics_creds_provider;

  for (auto sc : set_creds) {
    (sc.for_metrics()
        ? metrics_creds_provider
        : creds_provider) =
            std::make_shared<aws::auth::BasicAwsCredentialsProvider>(
                sc.credentials().akid(),
                sc.credentials().secret_key(),
                sc.credentials().has_token()
                    ? boost::optional<std::string>(sc.credentials().token())
                    : boost::none);
  }

  if (!creds_provider) {
    creds_provider =
        std::make_shared<aws::auth::DefaultAwsCredentialsProviderChain>(
            executor,
            ec2_md);
    aws::utils::sleep_for(std::chrono::milliseconds(250));
  }

  if (!creds_provider->try_get_credentials()) {
    LOG(error) << "Could not retrieve credentials from anywhere.";
    throw 1;
  }

  if (!metrics_creds_provider) {
    metrics_creds_provider = creds_provider;
  }

  return std::make_pair(creds_provider, metrics_creds_provider);
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
  aws::utils::setup_logging();

  if (argc < 3) {
    LOG(error) << "Usage:\n"
               << argv[0]
               << " {in_pipe} {out_pipe} {serialized Configuration} "
               << "[serializaed SetCredentials...]\n";
    return 1;
  }

  try {
    auto config = get_config(argv[3]);

    aws::utils::set_log_level(config->log_level());

    auto executor = get_executor();
    auto socket_factory = get_socket_factory();
    auto ec2_md =
      std::make_shared<aws::http::Ec2Metadata>(executor, socket_factory);
    auto region = get_region(*config, ec2_md);
    auto creds_providers = get_creds_providers(executor, ec2_md, argc, argv, 4);
    auto ipc_manager = get_ipc_manager(argv[1], argv[2]);

    aws::kinesis::core::KinesisProducer kp(
        ipc_manager,
        region,
        config,
        creds_providers.first,
        creds_providers.second,
        executor,
        socket_factory);

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
