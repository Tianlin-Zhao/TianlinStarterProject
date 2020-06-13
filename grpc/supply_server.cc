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
#include <map>
#include <vector>

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
using examples::Vendor;
using examples::VendorReg;
using examples::FoodVendorRequest;
using examples::RegisterStatus;
using examples::FoodSupplyService;

static map<int,map<int,vector<int>>> port_inventories;
static int type = -1;
static int quantity = -1;

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
void PerformWork(opencensus::trace::Span *parent,grpc::ServerWriter<Vendor> *reply) {
  auto span = opencensus::trace::Span::StartSpan("checking_inventories", parent);
  absl::SleepFor(absl::Milliseconds(20));  // Working hard here.
  for (map<int,map<int,vector<int>>>::iterator it = port_inventories.begin(); it != port_inventories.end(); ++it) {
    int port_num = it->first;
    map<int,vector<int>> inventory = it->second;
    if (inventory.count(type)) {
      int available_quantity = inventory[type][0];
      int price = inventory[type][1];
      if (available_quantity >= quantity) {
	Vendor rep;
	rep.set_portnumber(port_num);
	rep.set_quantity(available_quantity);
	rep.set_price(price);
	reply->Write(rep);
      }
    }
  }
  span.End();
}
void UpdateInventory(opencensus::trace::Span *parent,const VendorReg request,RegisterStatus *reply) {
  auto span = opencensus::trace::Span::StartSpan("updating_inventories", parent);
  span.AddAttribute("modifying_port",to_string(request.portnumber()));
  span.AddAttribute("updating_foodtype",to_string(request.foodtype()));
  span.AddAttribute("new_quantity",to_string(request.quantity()));
  absl::SleepFor(absl::Milliseconds(20));
  port_inventories[request.portnumber()][request.foodtype()] = {request.quantity(),request.price()};
  reply->set_regsuccess(true);
  span.End();
}

class FoodSupplyServiceImpl final : public FoodSupplyService::Service {
  grpc::Status UpdateVendor(grpc::ServerContext *context,
		        const VendorReg *request,
			RegisterStatus *reply) {
    opencensus::trace::Span span = grpc::GetSpanFromServerContext(context);
    span.AddAttribute("modifying_vendor",to_string(request->portnumber()));
    span.AddAttribute("foodtype",to_string(request->foodtype()));
    span.AddAttribute("new_quantity",to_string(request->quantity()));
    absl::SleepFor(absl::Milliseconds(10));
    UpdateInventory(&span,*request,reply);
    span.AddAnnotation("Sleeping.");
    absl::SleepFor(absl::Milliseconds(10));  
    opencensus::stats::Record(
        {{LettersMeasure(), 1}},
	{{ServiceName(),"update_vendor"},
	{FoodType(), to_string(request->foodtype())}});
    cerr << "Update Vendor RPC handler:\n";
    cerr << "  Current context: "
	 << opencensus::trace::GetCurrentSpan().context().ToString()
	 << "\n";
    cerr << "  Current tags: "
	 << opencensus::tags::GetCurrentTagMap().DebugString() << "\n";
    cerr << "  gRPC metadata:\n";
    auto metadata = context->client_metadata();
    for (const auto &mdpair : metadata) {
      cerr << "    \"" << absl::CEscape(ToStringView(mdpair.first))
	   << "\": \"" << absl::CEscape(ToStringView(mdpair.second))
	   << "\"\n";
    }
    cerr << "  (end of metadata)\n";
    return grpc::Status::OK;
  }

  grpc::Status FindPossibleVendors(grpc::ServerContext *context,
                        const FoodRequest *request,
                        grpc::ServerWriter<Vendor> *reply) override {
    opencensus::trace::Span span = grpc::GetSpanFromServerContext(context);
    span.AddAttribute("foodtype", to_string(request->foodtype()));
    span.AddAttribute("quantity",to_string(request->quantity()));
    absl::SleepFor(absl::Milliseconds(10));
    type = request->foodtype();
    quantity = request->quantity();
    PerformWork(&span,reply);
    span.AddAnnotation("Sleeping.");
    absl::SleepFor(absl::Milliseconds(30));
    // Record custom stats.
    opencensus::stats::Record(
        {{LettersMeasure(), 1}},
	{{ServiceName(),"find_vendors"},
        {FoodType(), to_string(request->foodtype())}});
    // Give feedback on stderr.
    std::cerr << "Search Vendor RPC handler:\n";
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
  if (argc == 2) {
    if (!absl::SimpleAtoi(argv[1], &port)) {
      std::cerr << "Invalid port number: \"" << argv[1] << "\"";
      return 1;
    }
  }

  // Register the OpenCensus gRPC plugin to enable stats and tracing in gRPC.
  grpc::RegisterOpenCensusPlugin();
  // Register the gRPC views (latency, error count, etc).
  grpc::RegisterOpenCensusViewsForExport();
  RegisterExporters();
  // Keep a shared pointer to the Prometheus exporter.
  auto exporter =
      std::make_shared<opencensus::exporters::stats::PrometheusExporter>();
  // Expose a Prometheus endpoint.
  prometheus::Exposer exposer("127.0.0.1:8081");
  exposer.RegisterCollectable(exporter);
  // Init custom measure.
  LettersMeasure();

  // Add a View for custom stats.
  const opencensus::stats::ViewDescriptor letters_view =
      opencensus::stats::ViewDescriptor()
          .set_name("starter_proj.org/view/servers")
          .set_description("paramters for hello_server")
          .set_measure(kLettersMeasureName)
          .set_aggregation(opencensus::stats::Aggregation::Sum())
	  .add_column(ServiceName())
          .add_column(FoodType());
  opencensus::stats::View view(letters_view);
  assert(view.IsValid());
  letters_view.RegisterForExport();

  // Start the RPC server. You shouldn't see any output from gRPC before this.
  std::cerr << "gRPC starting.\n";
  FoodSupplyServiceImpl service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(absl::StrCat("[::]:", port),
                           grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on [::]:" << port << "\n";
  server->Wait();
}
