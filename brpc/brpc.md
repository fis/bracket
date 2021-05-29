# brpc

## Defining a service

```protobuf
syntax = "proto3";

package some.example;

import "brpc/options.proto";

service Example {
  rpc Simple (SimpleRequest) returns (SimpleResponse) { option (brpc.brpc).code = 0; }
  rpc Stream (stream StreamRequest) returns (stream StreamResponse) { option (brpc.brpc).code = 1; }
}
```

As an implementation limitation, all methods must be either one-shot or fully
bidirectionally streaming: there is no support for client-streaming or
server-streaming calls. Of course they can be emulated with a bidirectional call
where you just observe the proprieties, there just isn't any API conveniences to
abstract that away.

## Generated code

For the example service in the previous section, the brpc code generator
generates the following items into the `some::example` namespace. Some boring
detail (like default virtual destructors) has been omitted for brevity.

### Client stub

```c++
class ExampleClient {
 public:
  ExampleClient() = default;
  event::Socket::Builder& target();
  // rpc Simple (SimpleRequest) returns (SimpleResponse);
  struct SimpleReceiver {
    virtual void SimpleDone(const SimpleResponse& resp) = 0;
    virtual void SimpleFailed(base::error_ptr error) = 0;
  };
  void Simple(const SimpleRequest& req, base::optional_ptr<SimpleReceiver> receiver);
  // rpc Stream (stream StreamRequest) returns (stream StreamResponse);
  class StreamCall;
  struct StreamReceiver {
    virtual void StreamOpen(StreamCall* call) = 0;
    virtual void StreamMessage(StreamCall* call, const StreamRequest& req) = 0;
    virtual void StreamClose(StreamCall* call, base::error_ptr error) = 0;
  };
  class StreamCall /* : ... */ {
   public:
    void Send(const StreamRequest& req);
    void Close();
   /* private: ... */
  };
  StreamCall* Stream(base::optional_ptr<StreamReceiver> receiver);
 /* private: ... */
};
```

To use the client, configure the `event::Socket::Builder` object returned by the
`target()` method to connect to a suitable server endpoint. You must also set
the event loop, which will be used for all the callbacks as well. Both simple
and streaming calls define a corresponding interface to receive the results
asynchronously. The method call itself always returns immediately.

Simple calls are one-shot, and either the `...Done` or `...Failed` method will
be called once.

For streaming calls, the `...Open` method is called once, after the low-level
socket connection has been established. After that, the `...Message` method will
be called once for each received message sent by the server. At any time, the
`...Close` method may be called, to indicate either an orderly connection close
requested by either endpoint (`error` will not be set) or an error condition
(`error` will be set).

Once one of the `...Done`, `...Failed` or `...Close` method has been called, no
further calls will be made by the system on the receiver object. It may
self-destruct if it wants to. If it was passed as an owned pointer, it will be
destroyed right after the terminal callback returns.

### Server implementation interface

```c++
struct ExampleInterface {
  // rpc Simple (SimpleRequest) returns (SimpleResponse);
  virtual bool Simple(const SimpleRequest& req, SimpleResponse* resp) = 0;
  // rpc Stream (stream StreamRequest) returns (stream StreamResponse);
  class StreamCall;
  struct StreamHandler {
    virtual void StreamOpen(StreamCall* call) = 0;
    virtual void StreamMessage(StreamCall* call, const StreamRequest& req) = 0;
    virtual void StreamClose(StreamCall* call, base::error_ptr error) = 0;
  };
  class StreamCall /* : ... */ {
   public:
    void Send(const StreamResponse& resp);
    void Close();
   /* private: ... */
  };
  virtual base::optional_ptr<StreamHandler> Stream(StreamCall* call) = 0;
  // non-call-specific error handler
  virtual void ExampleError(base::error_ptr error) = 0;
};
```

For simple calls, the implementation of the interface must populate the response
synchronously, and then return `true`. If it decides not to answer the call, it
may also return `false`, in which case the client will receive a failure signal.

For streaming calls, the implementation must return a handler to process the
call asynchronously. The handler's `...Open` method will be called exactly once
to signal the call has been established. Then, the `...Message` method is called
once for each message sent by the client. If the connection terminates (either
by request or abruptly), the `...Close` method is called once, after which no
new calls to the object will be made. The handler may self-destruct in the
`...Close` callback. If it was returned as an owned pointer, it will be
destroyed when it returns from the callback.

The interface-level `...Error` method will be called to report any unexpected
happenings that are not related to any specific call, such as a failure to
complete a handshake with a prospective client.

### Server class

```c++
class ExampleServer /* : ... */ {
 public:
  ExampleServer(event::Loop* loop, base::optional_ptr<ExampleInterface> impl);
  base::error_ptr Start(const std::string& path);
 /* private: ... */
};
```

The server class implements a listening server for `ExampleClient` clients to
connect to, invoking the interface methods on the given implementation.

To use the server, construct it on top of an event loop (which must outlive the
server) and an implementation (which must do otherwise, if it's not owned by the
server). Then call the `Start` method to open a listening Unix domain stream
socket on the given path to accept connections.

TODO: serving on other kinds of listening sockets.

### Method constants

```c++
struct ExampleMethod {
  enum Code : std::uint32_t {
    kSimple = 0,
    kStream = 1,
  };
};
```

This enumeration holds the method codes defined in the RPC service. There should
in general be no need to care about the codes.

## Bazel integration

TODO: improve and then document. Currently there is a `brpc_library` macro
defined in the `brpc_library.bzl` file, but it is implemented as a `genrule`
that calls `protoc` (yeah, I know), and the implementation leaves a lot to be
desired. It should just be scrapped and rewritten as a proper Starlark rule.
