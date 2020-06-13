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
#include "opencensus/stats/aggregation.h"
#include "opencensus/stats/bucket_boundaries.h"
#include "opencensus/stats/view_descriptor.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/context_util.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/tags/with_tag_map.h"
#include "opencensus/trace/context_util.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/span.h"
#include "opencensus/trace/with_span.h"
#include "opencensus/trace/trace_config.h"
#include "prometheus/exposer.h"

namespace {

using namespace std;
using examples::FoodReply;
using examples::FoodRequest;
using examples::RegisterStatus;
using examples::VendorReg;
using examples::RequestStatus;
using examples::FoodVendorRequest;
using examples::FoodVendorService;
using examples::FoodFindService;
using examples::FoodSupplyService;
using examples::Vendor;

string supplier_port = "127.0.0.1:";
static map<int,map<int,vector<int>>> prepared_port_inventories = 
{
	{9002,{{0,{1,20}},{1,{2,10}},{2,{1,15}}}},
	{9003,{{0,{2,30}},{1,{2,10}},{2,{5,15}}}},
	{9004,{{0,{2,15}},{1,{6,10}},{2,{2,10}}}}
};

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
void ProvideFood(opencensus::trace::Span *parent,const FoodVendorRequest request,RequestStatus *reply) {
  auto span = opencensus::trace::Span::StartSpan("contact_vendor", parent);
  span.AddAttribute("vendor", to_string(request.portnumber()));
  span.AddAttribute("foodtype",to_string(request.foodtype()));
  span.AddAttribute("quantity",to_string(request.quantity()));
  absl::SleepFor(absl::Milliseconds(20));  // Working hard here.
  std::cout<<"Try to get the wanted goods"<<std::endl;
  reply->set_requeststatus(1);
  string message = "Current Vendor of Port "+to_string(request.portnumber())+" have no ingredient type "+to_string(request.foodtype())+". You demanded "+to_string(request.quantity())+".";
  if (prepared_port_inventories.count(request.portnumber())) {
    if (prepared_port_inventories[request.portnumber()].count(request.foodtype())) {
      vector<int> QP = prepared_port_inventories[request.portnumber()][request.foodtype()];
      message = "Current Vendor of Port "+to_string(request.portnumber())+" only have "+to_string(prepared_port_inventories[request.portnumber()][request.foodtype()][0])+" of ingredient type "+to_string(request.foodtype())+". You demanded "+to_string(request.quantity())+".";
      if (QP[0]>=request.quantity()) {
	prepared_port_inventories[request.portnumber()][request.foodtype()][0] -= request.quantity();
	grpc::ClientContext ctx;
	VendorReg vendorinfo;
	vendorinfo.set_portnumber(request.portnumber());
	vendorinfo.set_foodtype(request.foodtype());
	vendorinfo.set_quantity(QP[0]-request.quantity());
	vendorinfo.set_price(QP[1]);
	RegisterStatus regstat;
	shared_ptr<grpc::Channel> channel = grpc::CreateChannel(supplier_port,grpc::InsecureChannelCredentials());
	unique_ptr<FoodSupplyService::Stub> stub = FoodSupplyService::NewStub(channel);
	grpc::Status status = stub->UpdateVendor(&ctx,vendorinfo,&regstat);
	cout << "Got status: " << status.error_code() << ": \"" << status.error_message() << "\"\n";
	cout << "Got replay: \"" << reply->ShortDebugString() << "\"\n";
        reply->set_requeststatus(0);
	message = "You have purchased "+to_string(request.quantity())+" of ingredient type "+to_string(request.foodtype())+" at price "+to_string(QP[1])+". Total: "+to_string(request.quantity()*QP[1]);
      }
    }
  }
  reply->set_message(message);
  span.End();
}

class FoodVendorServiceImpl final : public FoodVendorService::Service {
  grpc::Status TryProvideFood(grpc::ServerContext *context,
                        const FoodVendorRequest *request,
                        RequestStatus *reply) override {
    opencensus::trace::Span span = grpc::GetSpanFromServerContext(context);
    absl::SleepFor(absl::Milliseconds(10));
    ProvideFood(&span,*request,reply);
    span.AddAnnotation("Sleeping.");
    absl::SleepFor(absl::Milliseconds(30));
    // Record custom stats.
    opencensus::stats::Record(
        {{LettersMeasure(), 1}},
	{{ServiceName(),"try_provide_food"},
        {FoodType(), to_string(request->foodtype())}});
    // Give feedback on stderr.
    std::cerr << "Provide Food RPC handler:\n";
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
  int spl_port = 0;
  if (argc == 2) {
    if (!absl::SimpleAtoi(argv[1], &spl_port)) {
      std::cerr << "Invalid supplier port number: \"" << argv[1] << "\"";
      return 1;
    }
  }

  supplier_port += argv[1];

  // Register the OpenCensus gRPC plugin to enable stats and tracing in gRPC.
  grpc::RegisterOpenCensusPlugin();

  // Register the gRPC views (latency, error count, etc).
  grpc::RegisterOpenCensusViewsForExport();

  RegisterExporters();

  // Keep a shared pointer to the Prometheus exporter.
  auto exporter =
      std::make_shared<opencensus::exporters::stats::PrometheusExporter>();

  // Expose a Prometheus endpoint.
  prometheus::Exposer exposer("127.0.0.1:8085");
  exposer.RegisterCollectable(exporter);

  // Init custom measure.
  LettersMeasure();

  // Add a View for custom stats.
  const opencensus::stats::ViewDescriptor letters_view =
      opencensus::stats::ViewDescriptor()
          .set_name("starter_proj.org/view/servers")
          .set_description("parameters for vendor servers")
          .set_measure(kLettersMeasureName)
          .set_aggregation(opencensus::stats::Aggregation::Sum())
	  .add_column(ServiceName())
          .add_column(FoodType());
  opencensus::stats::View view(letters_view);
  assert(view.IsValid());
  letters_view.RegisterForExport();
  vector<int> ports;
  // Mimic the initial registration of vendors to supplier_service
  {
    for (map<int,map<int,vector<int>>>::iterator it = prepared_port_inventories.begin(); it != prepared_port_inventories.end(); ++it) {
      int pt_number = it->first;
      ports.push_back(pt_number);
      static opencensus::trace::AlwaysSampler sampler;
      auto span = opencensus::trace::Span::StartSpan("VendorClient",nullptr,{&sampler});
      static const auto key = opencensus::tags::TagKey::Register("vendor_reg");
      vector<pair<opencensus::tags::TagKey,string>> tags(
		      opencensus::tags::GetCurrentTagMap().tags());
      tags.emplace_back(key,"port number: "+to_string(pt_number));
      opencensus::tags::TagMap tag_map(move(tags));

      opencensus::trace::WithSpan ws(span);
      opencensus::tags::WithTagMap wt(tag_map);

      shared_ptr<grpc::Channel> channel = grpc::CreateChannel(supplier_port,grpc::InsecureChannelCredentials());
      for (map<int,vector<int>>::iterator fdtype = (it->second).begin(); fdtype != (it->second).end(); ++fdtype) {
	
	grpc::ClientContext ctx;
	string ptnbr = to_string(pt_number);
	ctx.AddMetadata("vendor",ptnbr);
	unique_ptr<FoodSupplyService::Stub> stub = FoodSupplyService::NewStub(channel);
	VendorReg vendorreg;
	vendorreg.set_portnumber(pt_number);
	vendorreg.set_foodtype(fdtype->first);
	vendorreg.set_quantity((fdtype->second)[0]);
	vendorreg.set_price((fdtype->second)[1]);
        RegisterStatus regstatus;
	cout << "Sending request: \"" << vendorreg.ShortDebugString() << "\"\n";

	opencensus::trace::GetCurrentSpan().AddAnnotation("Registering Vendor");
	// Send the RPC
	grpc::Status status = stub->UpdateVendor(&ctx,vendorreg,&regstatus);
	absl::SleepFor(absl::Milliseconds(10));
	cout<< "Vendor Init: Got status: " << status.error_code() << ": \""
	    << status.error_message() << "\"\n";
	cout<< "Vendor Init: Got reply: " << regstatus.ShortDebugString() << "\"\n";
	opencensus::trace::GetCurrentSpan().AddAnnotation(
			"Registration Complete.", {{"status",absl::StrCat(status.error_code(),
					": ",status.error_message())}});
      }
    }
  }

  // Start the RPC server. You shouldn't see any output from gRPC before this.
  FoodVendorServiceImpl service;
  grpc::ServerBuilder builder;
  string pts = "";
  for (int i = 0; i < ports.size(); i++) {
    pts += to_string(ports[i])+"; ";
    builder.AddListeningPort(absl::StrCat("[::]:", ports[i]),
		    	   grpc::InsecureServerCredentials(), &(ports[i]));
  }
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on port [::]:" << pts << "\n";
  server->Wait();
}
