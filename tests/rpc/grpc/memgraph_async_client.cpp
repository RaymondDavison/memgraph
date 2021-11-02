// Copyright 2021 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

#include "memgraph.grpc.pb.h"

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using memgraph::PropertyRequest;
using memgraph::PropertyValue;
using memgraph::Storage;

class StorageClient {
 public:
  explicit StorageClient(std::shared_ptr<Channel> channel) : stub_(Storage::NewStub(channel)) {}

  // Assembles the client's payload and sends it to the server.
  void GetProperty(const std::string &name, const int64_t count) {
    // Data we are sending to the server.
    PropertyRequest request;
    request.set_name(name);
    request.set_count(count);

    // Call object to store rpc data
    auto *call = new AsyncClientCall;

    // stub_->PrepareAsyncSayHello() creates an RPC object, returning
    // an instance to store in "call" but does not actually start the RPC
    // Because we are using the asynchronous API, we need to hold on to
    // the "call" instance in order to get updates on the ongoing RPC.
    call->response_reader = stub_->PrepareAsyncGetProperty(&call->context, request, &cq_);

    // StartCall initiates the RPC call
    call->response_reader->StartCall();

    // Request that, upon completion of the RPC, "reply" be updated with the
    // server's response; "status" with the indication of whether the operation
    // was successful. Tag the request with the memory address of the call
    // object.
    call->response_reader->Finish(&call->reply, &call->status, (void *)call);
  }

  // Loop while listening for completed responses.
  // Prints out the response from the server.
  void AsyncCompleteRpc() {
    void *got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (cq_.Next(&got_tag, &ok)) {
      // The tag in this example is the memory location of the call object
      AsyncClientCall *call = static_cast<AsyncClientCall *>(got_tag);

      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      GPR_ASSERT(ok);

      if (call->status.ok()) {
#if !defined(NDEBUG)
        std::cout << "Client received: " << call->reply.string_v() << std::endl;
#endif
      } else {
        std::cout << "RPC failed" << std::endl;
      }

      // Once we're complete, deallocate the call object.
      delete call;
    }
  }

 private:
  // struct for keeping state and data information
  struct AsyncClientCall {
    // Container for the data we expect from the server.
    PropertyValue reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // Storage for the status of the RPC upon completion.
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<PropertyValue>> response_reader;
  };

  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<Storage::Stub> stub_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;
};

int main(int argc, char **argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  StorageClient storage(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));

  // Spawn reader thread that loops indefinitely
  std::vector<std::jthread> threads{};
  for (auto i = 0; i < 4; ++i) {
    threads.emplace_back(&StorageClient::AsyncCompleteRpc, &storage);
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));

  for (int i = 0; i < 500; i++) {
    std::string name("world " + std::to_string(i));
    storage.GetProperty(name, 2);  // The actual RPC call!
  }

  std::cout << "Press control-c to quit" << std::endl << std::endl;

  return 0;
}
