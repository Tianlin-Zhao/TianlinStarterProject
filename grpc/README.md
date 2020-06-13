#How to Run

If this program cannot be built, this means that this project has be brought to a workspace. For example, copy this over to opencensus/examples/grpc.

##Running Scheme

* To setup client: env STACKDRIVER\_PROJECT\_ID=tianlinzhao-2020-starter-proj ../../bazel-bin/examples/grpc/hello\_client 9008

* To setup food finder service: env STACKDRIVER\_PROJECT\_ID=tianlinzhao-2020-starter-proj ../../bazel-bin/examples/grpc/hello\_server 9008 9001

* To setup supplier inventories: env STACKDRIVER\_PROJECT\_ID=tianlinzhao-2020-starter-proj ../../bazel-bin/examples/grpc/supply\_server 9001

* To setup vendors: env STACKDRIVER\_PROJECT\_ID=tianlinzhao-2020-starter-proj ../../bazel-bin/examples/grpc/vendors\_server 9001
