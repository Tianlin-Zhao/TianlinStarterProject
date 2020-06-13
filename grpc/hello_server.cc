// Copyright 2018, OpenCensus Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Example RPC server using gRPC.

#include <grpcpp/grpcpp.h>
#include <grpcpp/opencensus.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "examples/grpc/exporters.h"
#include "examples/grpc/hello.grpc.pb.h"
#include "examples/grpc/hello.pb.h"
#include "opencensus/exporters/stats/prometheus/prometheus_exporter.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/context_util.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/trace/context_util.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/trace_config.h"
#include "prometheus/exposer.h"

namespace {

using namespace std;
using examples::FoodReply;
using examples::FoodRequest;
using examples::FoodFindService;
using examples::FoodSupplyService;
using examples::Vendor;
using examples::FoodVendorRequest;
using examples::RequestStatus;
using examples::FoodVendorService;
using examples::VendorReg;

std::string supplier_port = "127.0.0.1:";
vector<Vendor> vendors;


ABSL_CONST_INIT const char kLettersMeasureName[] =
    "example.org/measure/letters";

opencensus::stats::MeasureInt64 LettersMeasure() {
  static const opencensus::stats::MeasureInt64 measure =
      opencensus::stats::MeasureInt64::Register(
          kLettersMeasureName, "Number of letters in processed names.", "By");
  return measure;
}

opencensus::tags::TagKey ServiceName() {
  static const opencensus::tags::TagKey key =
      opencensus::tags::TagKey::Register("service_name");
  return key;
}

opencensus::tags::TagKey FoodType() {
  static const opencensus::tags::TagKey key =
      opencensus::tags::TagKey::Register("food_type");
  return key;
}

absl::string_view ToStringView(const ::grpc::string_ref &s) {
  return absl::string_view(s.data(), s.size());
}

// A helper function that performs some work in its own Span.
void PerformWork(opencensus::trace::Span *parent,const FoodRequest request) {
  auto span = opencensus::trace::Span::StartSpan("find_food_work", parent);
  span.AddAttribute("food_type", to_string(request.foodtype()));
  span.AddAnnotation("Performing work.");
  absl::SleepFor(absl::Milliseconds(20));  // Working hard here.
  grpc::ClientContext ctx;
  ctx.AddMetadata("find_food_type",to_string(request.foodtype()));
  Vendor vendor;
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(supplier_port,grpc::InsecureChannelCredentials());
  std::unique_ptr<FoodSupplyService::Stub> stub = FoodSupplyService::NewStub(channel);
  std::unique_ptr<grpc::ClientReader<Vendor> > reader(stub->FindPossibleVendors(&ctx,request));
  while (reader->Read(&vendor)) {
    vendors.push_back(vendor);
  }
  grpc::Status status = reader->Finish();
  span.End();
}

void AcquireFood(opencensus::trace::Span *parent,const FoodVendorRequest request,RequestStatus *reply) {
  auto span = opencensus::trace::Span::StartSpan("Get_Food_Work", parent);
  span.AddAttribute("food_type", to_string(request.foodtype()));
  absl::SleepFor(absl::Milliseconds(20));  // Working hard here.
  grpc::ClientContext ctx;
  ctx.AddMetadata("get_food_type",to_string(request.foodtype()));
  Vendor vendor;
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("127.0.0.1:"+to_string(request.portnumber()),grpc::InsecureChannelCredentials());
  std::unique_ptr<FoodVendorService::Stub> stub = FoodVendorService::NewStub(channel);
  grpc::Status status = stub->TryProvideFood(&ctx,request,reply);
  absl::SleepFor(absl::Milliseconds(30));
  int stat = reply->requeststatus();
  string message = reply->message();
  if (stat == 0) {
    message = "Request Success:\nTransaction: "+message+"\n";
  } else if (stat == 1) {
    message = "Request Failed:\nCause: "+message+"\n";
  } else {
    message = "Request Failed:\nCause: Unkown, possible connection failure.\n";
  }
  reply->set_message(message);
  span.End();
}

class FoodFindServiceImpl final : public FoodFindService::Service {
  grpc::Status GetFood(grpc::ServerContext *context,
		        const FoodVendorRequest *request,
			RequestStatus *reply) override {
    opencensus::trace::Span span = grpc::GetSpanFromServerContext(context);
    span.AddAttribute("food_type",request->foodtype());
    absl::SleepFor(absl::Milliseconds(10));
    reply->set_requeststatus(2);
    AcquireFood(&span,*request,reply);
    span.AddAnnotation("Sleeping.");
    absl::SleepFor(absl::Milliseconds(30));
    opencensus::stats::Record(
		    {{LettersMeasure(), 1}},
		    {{ServiceName(),"get_food"},
		    {FoodType(), to_string(request->foodtype())}});
    std::cerr << "GetFood RPC handler:\n";
    std::cerr << "  Current context: "
              << opencensus::trace::GetCurrentSpan().context().ToString()
              << "\n";
    std::cerr << "  Current tags: "
              << opencensus::tags::GetCurrentTagMap().DebugString() << "\n";
    std::cerr << " gRPC metadata:\n";
    auto metadata = context->client_metadata();
    for (const auto &mdpair : metadata) {
      std::cerr << "    \"" << absl::CEscape(ToStringView(mdpair.first))
                << "\": \"" << absl::CEscape(ToStringView(mdpair.second))
                << "\"\n";
    }
    std::cerr << "  (end of metadata)\n";
    return grpc::Status::OK;
  }
	
  grpc::Status FindFood(grpc::ServerContext *context,
                        const FoodRequest *request,
                        FoodReply *reply) override {
    opencensus::trace::Span span = grpc::GetSpanFromServerContext(context);
    span.AddAttribute("food_type",to_string(request->foodtype()));
    absl::SleepFor(absl::Milliseconds(10));
    vendors.clear();
    PerformWork(&span,*request);
    string message = "With your requested amount, the following vendors are available:\r\n";
    for (vector<Vendor>::iterator it = vendors.begin(); it != vendors.end(); ++it) {
      message += "Port No.: "+ to_string(it->portnumber())+" Amount: "+to_string(it->quantity())+" Price: "+to_string(it->price())+"\r\n";
    }
    reply->set_message(message);
    span.AddAnnotation("Sleeping.");
    absl::SleepFor(absl::Milliseconds(30));
    // Record custom stats.
    opencensus::stats::Record(
        {{LettersMeasure(), 1}},
	{{ServiceName(),"find_food"},
        {FoodType(), to_string(request->foodtype())}});
    // Give feedback on stderr.
    std::cerr << "Find Food RPC handler:\n";
    std::cerr << "  Current context: "
              << opencensus::trace::GetCurrentSpan().context().ToString()
              << "\n";
    std::cerr << "  Current tags: "
              << opencensus::tags::GetCurrentTagMap().DebugString() << "\n";
    std::cerr << "  gRPC metadata:\n";
    auto metadata = context->client_metadata();
    for (const auto &mdpair : metadata) {
      std::cerr << "    \"" << absl::CEscape(ToStringView(mdpair.first))
                << "\": \"" << absl::CEscape(ToStringView(mdpair.second))
                << "\"\n";
    }
    std::cerr << "  (end of metadata)\n";
    return grpc::Status::OK;
  }
};

}  // namespace

int main(int argc, char **argv) {
  // Handle port argument.
  int port = 0;
  int spl_port = 0;
  if (argc == 3) {
    if (!absl::SimpleAtoi(argv[1], &port)) {
      std::cerr << "Invalid port number: \"" << argv[1] << "\"";
      return 1;
    }
    if (!absl::SimpleAtoi(argv[2], &spl_port)) {
      std::cerr << "Invalid supplier port number: \"" << argv[2] <<std::endl;
      return 1;
    }
  }

  supplier_port += argv[2];

  // Register the OpenCensus gRPC plugin to enable stats and tracing in gRPC.
  grpc::RegisterOpenCensusPlugin();

  // Register the gRPC views (latency, error count, etc).
  grpc::RegisterOpenCensusViewsForExport();

  RegisterExporters();

  // Keep a shared pointer to the Prometheus exporter.
  auto exporter =
      std::make_shared<opencensus::exporters::stats::PrometheusExporter>();

  // Expose a Prometheus endpoint.
  prometheus::Exposer exposer("127.0.0.1:8080");
  exposer.RegisterCollectable(exporter);

  // Init custom measure.
  LettersMeasure();

  // Add a View for custom stats.
  const opencensus::stats::ViewDescriptor letters_view =
      opencensus::stats::ViewDescriptor()
          .set_name("starter_proj.org/view/servers")
          .set_description("checking the parameters for requests in GoMart")
          .set_measure(kLettersMeasureName)
          .set_aggregation(opencensus::stats::Aggregation::Sum())
	  .add_column(ServiceName())
          .add_column(FoodType());
  opencensus::stats::View view(letters_view);
  assert(view.IsValid());
  letters_view.RegisterForExport();

  // Start the RPC server. You shouldn't see any output from gRPC before this.
  std::cerr << "gRPC starting.\n";
  FoodFindServiceImpl service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(absl::StrCat("[::]:", port),
                           grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on [::]:" << port << "\n";
  server->Wait();
}
