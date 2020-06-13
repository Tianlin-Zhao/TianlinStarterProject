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

#include <grpcpp/grpcpp.h>
#include <grpcpp/opencensus.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <list>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "examples/grpc/exporters.h"
#include "examples/grpc/hello.grpc.pb.h"
#include "examples/grpc/hello.pb.h"
#include "opencensus/stats/aggregation.h"
#include "opencensus/stats/bucket_boundaries.h"
#include "opencensus/stats/view_descriptor.h"
#include "opencensus/tags/context_util.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"
#include "opencensus/tags/with_tag_map.h"
#include "opencensus/trace/context_util.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"
#include "opencensus/trace/with_span.h"

namespace {
using namespace std;
using examples::FoodReply;
using examples::FoodRequest;
using examples::FoodFindService;
using examples::FoodVendorService;
using examples::FoodVendorRequest;
using examples::RequestStatus;

opencensus::tags::TagKey MyKey() {
  static const auto key = opencensus::tags::TagKey::Register("my_key");
  return key;
}

opencensus::stats::Aggregation MillisDistributionAggregation() {
  return opencensus::stats::Aggregation::Distribution(
      opencensus::stats::BucketBoundaries::Explicit(
          {0, 0.1, 1, 10, 100, 1000}));
}

ABSL_CONST_INIT const absl::string_view kRpcClientRoundtripLatencyMeasureName =
    "grpc.io/client/roundtrip_latency";

::opencensus::tags::TagKey ClientMethodTagKey() {
  static const auto method_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_client_method");
  return method_tag_key;
}

::opencensus::tags::TagKey ClientStatusTagKey() {
  static const auto status_tag_key =
      ::opencensus::tags::TagKey::Register("grpc_client_status");
  return status_tag_key;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " host:port [name]\n";
    return 1;
  }
  const std::string hostport = argv[1];
  std::string name = "world";
  if (argc >= 3) {
    name = argv[2];
  }
  // Register the OpenCensus gRPC plugin to enable stats and tracing in gRPC.
  grpc::RegisterOpenCensusPlugin();

  // Add a view of client RPC latency broken down by our custom key.
  opencensus::stats::ViewDescriptor()
      .set_name("example/client/roundtrip_latency/cumulative")
      .set_measure(kRpcClientRoundtripLatencyMeasureName)
      .set_aggregation(MillisDistributionAggregation())
      .add_column(ClientMethodTagKey())
      .add_column(ClientStatusTagKey())
      .add_column(MyKey())
      .RegisterForExport();

  
  {
    while (true) {
    
  // Create a span, this will be the parent of the RPC span.
  static opencensus::trace::AlwaysSampler sampler;
  auto span = opencensus::trace::Span::StartSpan(
      "CostumerClient", /*parent=*/nullptr, {&sampler});

  // Extend the current tag map.
  // (in this example, there are no tags currently present, but in real code we
  // wouldn't want to drop tags)
  static const auto key = opencensus::tags::TagKey::Register("my_key");
  std::vector<std::pair<opencensus::tags::TagKey, std::string>> tags(
      opencensus::tags::GetCurrentTagMap().tags());
  tags.emplace_back(key, "my_value");
  opencensus::tags::TagMap tag_map(std::move(tags));
	    
    opencensus::trace::WithSpan ws(span);
    opencensus::tags::WithTagMap wt(tag_map);

    // The client Span ends when ctx falls out of scope.
    grpc::ClientContext ctx;
    ctx.AddMetadata("client_port", hostport);

    // User interface
    string isn;
    bool valid = false;
    string ingredient;
    int ingred;
    string q;
    int quantity;
    string v_port;
    int vendor_port;
    cout<<"Welcome to GoMart, where true programmers buy their food from."<<endl;
    cout<<"(I am planning to make this into a multiplayer minigame if there's time for it)"<<endl;
    cout<<"Enter SearchFood or GetFood"<<endl;
    getline(cin,isn);
    if (isn=="SearchFood") {
      while (!valid) {
        cout<<"Enter the Ingredient you want (Available: Flour, Milk, Egg)"<<endl;
        getline(cin,ingredient);
	valid = true;
        if (ingredient=="flour"||ingredient=="Flour"||ingredient=="FLOUR") {
          ingred = 0;
        } else if (ingredient=="milk"||ingredient=="Milk"||ingredient=="MILK") {
	  ingred = 1;
        } else if (ingredient=="egg"||ingredient=="Egg"||ingredient=="EGG") {
	  ingred = 2;
        } else {
	  valid = false;
	  cout<<"Invalid Ingredient"<<endl;
        }
      }
      valid = false;
      while (!valid) {
	cout<<"Enter the amount you want to buy"<<endl;
	try {
	  getline(cin,q);
	  quantity = stoi(q);
	  if (quantity > 0) {
	    valid = true;
	  }
	  cout<<"Enter a value that is greater than 0"<<endl;
	} catch (exception& e) {
	  cout<<"Invalid Entries, please enter arabic numbers"<<endl;
	}
      }
      // Create a Channel to send RPCs over.
      std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(hostport, grpc::InsecureChannelCredentials());
      std::unique_ptr<FoodFindService::Stub> stub = FoodFindService::NewStub(channel);

      FoodRequest request;
      FoodReply reply;

      request.set_foodtype(ingred);
      request.set_quantity(quantity);
      reply.set_message("");
      std::cout << "Sending request: \"" << request.ShortDebugString() << "\"\n";
      opencensus::trace::GetCurrentSpan().AddAnnotation("Sending request.");

      // Send the RPC.
      grpc::Status status = stub->FindFood(&ctx, request, &reply);

      std::cout << "Got status: " << status.error_code() << ": \""
                << status.error_message() << "\"\n";
      std::cout << "Got reply: \"" << reply.message() << "\"\n";
      opencensus::trace::GetCurrentSpan().AddAnnotation(
          "Got reply.", {{"status", absl::StrCat(status.error_code(), ": ",
                                                 status.error_message())}});
      if (!status.ok()) {
        // TODO: Map grpc::StatusCode to trace::StatusCode.
        opencensus::trace::GetCurrentSpan().SetStatus(
            opencensus::trace::StatusCode::UNKNOWN, status.error_message());
      }  
    } else if (isn=="GetFood") {
      while (!valid) {
        cout<<"Enter the Ingredient you want (Available: Flour, Milk, Egg)"<<endl;
        getline(cin,ingredient);
	valid = true;
        if (ingredient=="flour"||ingredient=="Flour"||ingredient=="FLOUR") {
          ingred = 0;
        } else if (ingredient=="milk"||ingredient=="Milk"||ingredient=="MILK") {
	  ingred = 1;
        } else if (ingredient=="egg"||ingredient=="Egg"||ingredient=="EGG") {
	  ingred = 2;
        } else {
	  valid = false;
	  cout<<"Invalid Ingredient"<<endl;
        }
      }
      valid = false;
      while (!valid) {
	cout<<"Enter the amount you want to buy"<<endl;
	try {
	  getline(cin,q);
	  quantity = stoi(q);
	  if (quantity > 0) {
	    valid = true;
	  } else {
	    cout<<"Enter a value that is greater than 0"<<endl;
	  }
	} catch (exception& e) {
	  cout<<"Invalid Entries, please enter arabic numbers"<<endl;
	}
      }
      valid = false;
      while (!valid) {
	cout<<"Enter the vendor port you want to buy from"<<endl;
	try {
	  getline(cin,v_port);
	  vendor_port = stoi(v_port);
	  if (quantity > 0) {
	    valid = true;
	  }
	  cout<<"Enter a value that is greater than 0"<<endl;
	} catch (exception& e) {
	  cout<<"Invalid Entries, please enter arabic numbers"<<endl;
	}
      }
      // Create a Channel to send RPCs over.
      std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(hostport, grpc::InsecureChannelCredentials());
      std::unique_ptr<FoodFindService::Stub> stub = FoodFindService::NewStub(channel);

      FoodVendorRequest request;
      RequestStatus reply;

      request.set_foodtype(ingred);
      request.set_quantity(quantity);
      request.set_portnumber(vendor_port);
      reply.set_message("");
      reply.set_requeststatus(2);
      std::cout << "Sending request: \"" << request.ShortDebugString() << "\"\n";
      opencensus::trace::GetCurrentSpan().AddAnnotation("Sending request.");

      // Send the RPC.
      grpc::Status status = stub->GetFood(&ctx, request, &reply);

      std::cout << "Got status: " << status.error_code() << ": \""
                << status.error_message() << "\"\n";
      std::cout << "Got reply: \"" << reply.message() << "\"\n";
      opencensus::trace::GetCurrentSpan().AddAnnotation(
          "Got reply.", {{"status", absl::StrCat(status.error_code(), ": ",
                                                 status.error_message())}});
      if (!status.ok()) {
        // TODO: Map grpc::StatusCode to trace::StatusCode.
        opencensus::trace::GetCurrentSpan().SetStatus(
            opencensus::trace::StatusCode::UNKNOWN, status.error_message());
      }  
    } else {
      cout<<"Invalid Entries"<<endl;
      continue;
    }
      // Don't forget to explicitly end the span!
      opencensus::trace::GetCurrentSpan().End();
    }
  }

  // Disable tracing any further RPCs (which will be sent by exporters).
  opencensus::trace::TraceConfig::SetCurrentTraceParams(
      {128, 128, 128, 128, opencensus::trace::ProbabilitySampler(0.0)});

  // Sleep while exporters run in the background.
  std::cout << "Client sleeping, ^C to exit.\n";
  while (true) {
    absl::SleepFor(absl::Seconds(10));
  }
}
