#include <cctype>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

#include "brpc/options.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/compiler/plugin.pb.h"

// templates for code generation

const char kFileHeader[] =
    "// Generated by the brpc compiler.  DO NOT EDIT!\n"
    "// source: $src$\n"
    "\n"
    "#ifndef $guard$\n"
    "#define $guard$\n"
    "\n"
    "#include \"base/common.h\"\n"
    "#include \"base/exc.h\"\n"
    "#include \"brpc/brpc.h\"\n"
    "#include \"$base$.pb.h\"\n"
    "\n"
    "namespace $ns$ {\n\n";
const char kFileFooter[] =
    "} // namespace $ns$\n"
    "\n"
    "#endif // $guard$\n"
    "\n// Local Variables:\n// mode: c++\n// End:\n";

const char kMethodCodeHeader[] =
    "struct $service$Method {\n"
    "  enum Code : std::uint32_t {\n";
const char kMethodCodeEntry[] =
    "    k$method$ = $code$,\n";
const char kMethodCodeFooter[] =
    "  };\n"
    "};\n\n";

const char kInterfaceHeader[] =
    "struct $service$Interface {\n";
const char kInterfaceMethodHeader[] =
    "  // rpc $method$ ($reqStream$$reqType$) returns ($respStream$$respType$);\n";
const char kInterfaceMethodSimple[] =
    "  virtual bool $method$(const $reqType$& req, $respType$* resp) = 0;\n\n";
const char kInterfaceMethodBidi[] =
    "  class $method$Call;\n"
    "  struct $method$Handler {\n"
    "    virtual void $method$Open($method$Call* call) = 0;\n"
    "    virtual void $method$Message($method$Call* call, const $reqType$& req) = 0;\n"
    "    virtual void $method$Close($method$Call* call, ::std::unique_ptr<::base::error> error) = 0;\n"
    "    virtual ~$method$Handler() = default;\n"
    "  };\n"
    "  class $method$Call : public ::brpc::RpcEndpoint {\n"
    "   public:\n"
    "    $method$Call(::base::optional_ptr<$method$Handler> handler) : handler_(::std::move(handler)) {}\n"
    "    void Send(const $respType$& resp) { call_->Send(resp); }\n"
    "    void Close() { call_->Close(); }\n"
    "   private:\n"
    "    ::brpc::RpcCall* call_;\n"
    "    ::base::optional_ptr<$method$Handler> handler_;\n"
    "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall* call) override;\n"
    "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n"
    "    void RpcClose(::brpc::RpcCall* call, ::std::unique_ptr<::base::error> error) override;\n"
    "  };\n"
    "  virtual ::base::optional_ptr<$method$Handler> $method$() = 0;\n\n";
const char kInterfaceFooter[] =
    "  virtual void $service$Error(::std::unique_ptr<::base::error> error) = 0;\n\n"
    "  virtual ~$service$Interface() = default;\n"
    "};\n\n";
const char kInterfaceOutlineBidi[] =
    "inline ::std::unique_ptr<::google::protobuf::Message> $service$Interface::$method$Call::RpcOpen(::brpc::RpcCall* call) {\n"
    "  call_ = call;\n"
    "  handler_->$method$Open(this);\n"
    "  return ::std::make_unique<$reqType$>();\n"
    "}\n\n"
    "inline void $service$Interface::$method$Call::RpcMessage(::brpc::RpcCall*, const ::google::protobuf::Message& message) {\n"
    "  handler_->$method$Message(this, static_cast<const $reqType$&>(message));\n"
    "}\n\n"
    "inline void $service$Interface::$method$Call::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n"
    "  handler_->$method$Close(this, ::std::move(error));\n"
    "}\n\n";

const char kServerHeader[] =
    "class $service$Server : public ::brpc::RpcDispatcher {\n"
    " public:\n"
    "  $service$Server(::event::Loop* loop, ::base::optional_ptr<$service$Interface> impl) : server_(loop, this), impl_(::std::move(impl)) {}\n"
    "  ::std::unique_ptr<::base::error> Start(const ::std::string& path) { return server_.Start(path); }\n"
    " private:\n";
const char kServerEndpointSimple[] =
    "  class $method$Endpoint : public ::brpc::RpcEndpoint {\n"
    "   public:\n"
    "    $method$Endpoint($service$Interface* impl) : impl_(impl) {}\n"
    "   private:\n"
    "    $service$Interface* impl_;\n"
    "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall*) override;\n"
    "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n"
    "    void RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) override;\n"
    "  };\n\n";
const char kServerBody[] =
    "  ::brpc::RpcServer server_;\n"
    "  ::base::optional_ptr<$service$Interface> impl_;\n"
    "  ::std::unique_ptr<::brpc::RpcEndpoint> RpcOpen(::std::uint32_t method) override;\n"
    "  void RpcError(::std::unique_ptr<::base::error> error) override;\n"
    "};\n\n";
const char kServerOutlineSimple[] =
    "inline ::std::unique_ptr<::google::protobuf::Message> $service$Server::$method$Endpoint::RpcOpen(::brpc::RpcCall*) {\n"
    "  return ::std::make_unique<$reqType$>();\n"
    "}\n\n"
    "inline void $service$Server::$method$Endpoint::RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) {\n"
    "  $respType$ resp;\n"
    "  if (impl_->$method$(static_cast<const $reqType$&>(message), &resp))\n"
    "    call->Send(resp);\n"
    "  call->Close();\n"
    "}\n\n"
    "inline void $service$Server::$method$Endpoint::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n"
    "  if (error) impl_->$service$Error(::std::move(error));\n"
    "}\n\n";
const char kServerDispatcherHeader[] =
    "inline ::std::unique_ptr<::brpc::RpcEndpoint> $service$Server::RpcOpen(::std::uint32_t method) {\n"
    "  switch (method) {\n";
const char kServerDispatcherMethodHeader[] =
    "    case $service$Method::k$method$:\n";
const char kServerDispatcherMethodSimple[] =
    "      return ::std::make_unique<$method$Endpoint>(impl_.get());\n";
const char kServerDispatcherMethodBidi[] =
    "      {\n"
    "        auto handler = impl_->$method$();\n"
    "        if (handler)\n"
    "          return ::std::make_unique<$service$Interface::$method$Call>(std::move(handler));\n"
    "        else\n"
    "          return nullptr;\n"
    "      }\n";
const char kServerDispatcherFooter[] =
    "  }\n"
    "  return nullptr;\n"
    "}\n\n"
    "inline void $service$Server::RpcError(::std::unique_ptr<::base::error> error) {\n"
    "  impl_->$service$Error(::std::move(error));\n"
    "}\n\n";

const char kClientHeader[] =
    "class $service$Client {\n"
    " public:\n"
    "  $service$Client() = default;\n"
    "  ::event::Socket::Builder& target() { return client_.target(); }\n\n";
const char kClientPublicMethodSimple[] =
    "  struct $method$Receiver {\n"
    "    virtual void $method$Done(const $respType$& resp) = 0;\n"
    "    virtual void $method$Failed(::std::unique_ptr<::base::error> error) = 0;\n"
    "    virtual ~$method$Receiver() = default;\n"
    "  };\n"
    "  void $method$(const $reqType$& req, ::base::optional_ptr<$method$Receiver> receiver);\n\n";
const char kClientPublicMethodBidi[] =
    "  class $method$Call;\n"
    "  struct $method$Receiver {\n"
    "    virtual void $method$Open($method$Call* call) = 0;\n"
    "    virtual void $method$Message($method$Call* call, const $respType$& req) = 0;\n"
    "    virtual void $method$Close($method$Call* call, ::std::unique_ptr<::base::error> error) = 0;\n"
    "    virtual ~$method$Receiver() = default;\n"
    "  };\n"
    "  class $method$Call : public ::brpc::RpcEndpoint {\n"
    "   public:\n"
    "    $method$Call(::base::optional_ptr<$method$Receiver> receiver) : receiver_(::std::move(receiver)) {}\n"
    "    void Send(const $reqType$& req) { call_->Send(req); }\n"
    "    void Close() { call_->Close(); }\n"
    "   private:\n"
    "    ::brpc::RpcCall* call_;\n"
    "    ::base::optional_ptr<$method$Receiver> receiver_;\n"
    "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall* call) override;\n"
    "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n"
    "    void RpcClose(::brpc::RpcCall* call, ::std::unique_ptr<::base::error> error) override;\n"
    "  };\n"
    "  $method$Call* $method$(::base::optional_ptr<$method$Receiver> receiver);\n\n";
const char kClientPrivateHeader[] =
    " private:\n";
const char kClientPrivateMethodSimple[] =
    "  class $method$Endpoint : public ::brpc::RpcEndpoint {\n"
    "   public:\n"
    "    $method$Endpoint(::base::optional_ptr<$method$Receiver> receiver) : receiver_(::std::move(receiver)) {}\n"
    "   private:\n"
    "    ::base::optional_ptr<$method$Receiver> receiver_;\n"
    "    bool received_ = false;\n"
    "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall*) override;\n"
    "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n"
    "    void RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) override;\n"
    "  };\n\n";
const char kClientFooter[] =
    "  ::brpc::RpcClient client_;\n"
    "};\n\n";
const char kClientOutlineSimple[] =
    "inline ::std::unique_ptr<::google::protobuf::Message> $service$Client::$method$Endpoint::RpcOpen(::brpc::RpcCall*) {\n"
    "  return ::std::make_unique<$respType$>();\n"
    "}\n\n"
    "inline void $service$Client::$method$Endpoint::RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) {\n"
    "  if (!received_) {\n"
    "    receiver_->$method$Done(static_cast<const $respType$&>(message));\n"
    "    received_ = true;\n"
    "  }\n"
    "  call->Close();\n"
    "}\n\n"
    "inline void $service$Client::$method$Endpoint::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n"
    "  if (!received_) {\n"
    "    receiver_->$method$Failed(error ? ::std::move(error) : ::base::make_error(\"no answer\"));\n"
    "    received_ = true;\n"
    "  }\n"
    "}\n\n"
    "inline void $service$Client::$method$(const $reqType$& req, ::base::optional_ptr<$method$Receiver> receiver) {\n"
    "  client_.Call(::std::make_unique<$method$Endpoint>(::std::move(receiver)), $service$Method::k$method$, &req);\n"
    "}\n\n";
const char kClientOutlineBidi[] =
    "inline ::std::unique_ptr<::google::protobuf::Message> $service$Client::$method$Call::RpcOpen(::brpc::RpcCall* call) {\n"
    "  call_ = call;\n"
    "  receiver_->$method$Open(this);\n"
    "  return ::std::make_unique<$respType$>();\n"
    "}\n\n"
    "inline void $service$Client::$method$Call::RpcMessage(::brpc::RpcCall*, const ::google::protobuf::Message& message) {\n"
    "  receiver_->$method$Message(this, static_cast<const $respType$&>(message));\n"
    "}\n\n"
    "inline void $service$Client::$method$Call::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n"
    "  receiver_->$method$Close(this, ::std::move(error));\n"
    "}\n\n"
    "inline $service$Client::$method$Call* $service$Client::$method$(::base::optional_ptr<$method$Receiver> receiver) {\n"
    "  ::std::unique_ptr<$method$Call> call = ::std::make_unique<$method$Call>(::std::move(receiver));\n"
    "  $method$Call* call_ref = call.get();\n"
    "  client_.Call(::std::move(call), $service$Method::k$method$, nullptr);\n"
    "  return call_ref;\n"
    "}\n\n";

// generator

namespace pb = ::google::protobuf;
namespace comp = pb::compiler;

void WriteTemplate(const char* tmpl, const std::unordered_map<std::string, std::string>& params, std::ostringstream& out) {
  for (; *tmpl; ++tmpl) {
    if (*tmpl == '$') {
      const char* end = tmpl + 1;
      for (; *end && *end != '$'; ++end)
        ;
      if (*end != '$') { std::fprintf(stderr, "bad template: no $\n"); std::exit(1); }
      std::string key(tmpl + 1, (end - tmpl) - 1);
      auto repl = params.find(key);
      if (repl == params.end()) { std::fprintf(stderr, "bad template param: %.s\n", key.c_str()); std::exit(1); }
      out << repl->second;
      tmpl = end;
    } else {
      out.put(*tmpl);
    }
  }
}

std::string FormatNamespace(const std::string& proto_package) {
  std::string cpp_namespace;
  for (char c : proto_package) {
    if (c == '.')
      cpp_namespace += "::";
    else
      cpp_namespace += c;
  }
  return cpp_namespace;
}

std::string FormatHeaderGuard(const std::string& filename) {
  std::string guard("BRPC_GUARD_");
  for (char c : filename) {
    if (std::isalnum((unsigned char) c))
      guard += (char) std::toupper((unsigned char) c);
    else
      guard += '_';
  }
  guard += "_H_";
  return guard;
}

struct Method {
  std::unordered_map<std::string, std::string> params;
  bool req_stream;
  bool resp_stream;
};

bool ParseMethod(const pb::MethodDescriptorProto& method, Method* out, std::ostringstream& err) {
  out->params["method"] = method.name();

  if (!method.options().HasExtension(brpc::brpc)) {
    err << method.name() << ": missing { option (brpc).code = N; }\n";
    return false;
  }
  out->params["code"] = std::to_string(method.options().GetExtension(brpc::brpc).code());

  out->params["reqType"] = FormatNamespace(method.input_type());
  out->params["reqStream"] = method.client_streaming() ? "stream " : "";
  out->req_stream = method.client_streaming();

  out->params["respType"] = FormatNamespace(method.output_type());
  out->params["respStream"] = method.server_streaming() ? "stream " : "";
  out->resp_stream = method.server_streaming();

  if (out->req_stream != out->resp_stream) {
    err << method.name() << ": must be either bidirectionally or not at all streaming\n";
    return false;
  }

  return true;
}

void GenerateService(const pb::ServiceDescriptorProto& service, std::ostringstream& out, std::ostringstream& err) {
  // parse

  std::unordered_map<std::string, std::string> params;
  params["service"] = service.name();

  std::vector<Method> methods;
  bool failed_methods = false;

  for (const auto& method : service.method()) {
    methods.emplace_back();
    Method& m = methods.back();
    m.params["service"] = service.name();
    if (!ParseMethod(method, &m, err))
      failed_methods = true;
  }
  if (failed_methods)
    return;

  // generate

  out << "// service " << service.name() << "\n\n";

  WriteTemplate(kMethodCodeHeader, params, out);
  for (const Method& m : methods)
    WriteTemplate(kMethodCodeEntry, m.params, out);
  WriteTemplate(kMethodCodeFooter, params, out);

  WriteTemplate(kInterfaceHeader, params, out);
  for (const Method& m : methods) {
    WriteTemplate(kInterfaceMethodHeader, m.params, out);
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kInterfaceMethodSimple, m.params, out);
    else
      WriteTemplate(kInterfaceMethodBidi, m.params, out);
  }
  WriteTemplate(kInterfaceFooter, params, out);
  for (const Method& m : methods)
    if (m.req_stream && m.resp_stream)
      WriteTemplate(kInterfaceOutlineBidi, m.params, out);

  WriteTemplate(kServerHeader, params, out);
  for (const Method& m : methods)
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kServerEndpointSimple, m.params, out);
  WriteTemplate(kServerBody, params, out);
  for (const Method& m : methods)
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kServerOutlineSimple, m.params, out);
  WriteTemplate(kServerDispatcherHeader, params, out);
  for (const Method& m : methods) {
    WriteTemplate(kServerDispatcherMethodHeader, m.params, out);
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kServerDispatcherMethodSimple, m.params, out);
    else
      WriteTemplate(kServerDispatcherMethodBidi, m.params, out);
  }
  WriteTemplate(kServerDispatcherFooter, params, out);

  WriteTemplate(kClientHeader, params, out);
  for (const Method& m : methods) {
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kClientPublicMethodSimple, m.params, out);
    else
      WriteTemplate(kClientPublicMethodBidi, m.params, out);
  }
  WriteTemplate(kClientPrivateHeader, params, out);
  for (const Method& m : methods)
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kClientPrivateMethodSimple, m.params, out);
  WriteTemplate(kClientFooter, params, out);
  for (const Method& m : methods)
    if (!m.req_stream && !m.resp_stream)
      WriteTemplate(kClientOutlineSimple, m.params, out);
    else
      WriteTemplate(kClientOutlineBidi, m.params, out);
}

void GenerateFile(const pb::FileDescriptorProto& desc, comp::CodeGeneratorResponse* resp) {
  // parse

  if (desc.service_size() == 0) {
    *resp->mutable_error() += "no services in file: " + desc.name() + "\n";
    return;
  }

  std::string base_filename(desc.name());
  if (base_filename.length() > 6 && base_filename.compare(base_filename.length() - 6, 6, ".proto") == 0)
    base_filename.resize(base_filename.length() - 6);

  std::unordered_map<std::string, std::string> params;
  params["src"] = desc.name();
  params["base"] = base_filename;
  params["guard"] = FormatHeaderGuard(base_filename);
  params["ns"] = FormatNamespace(desc.package());

  // generate

  auto* file = resp->add_file();

  file->set_name(base_filename + ".brpc.h");

  std::ostringstream out;
  std::ostringstream err;

  WriteTemplate(kFileHeader, params, out);
  for (const auto& service : desc.service())
    GenerateService(service, out, err);
  WriteTemplate(kFileFooter, params, out);

  if (!err.str().empty()) {
    *resp->mutable_error() += err.str();
    return;
  }

  file->set_content(out.str());
}

void Generate(const comp::CodeGeneratorRequest& req, comp::CodeGeneratorResponse* resp) {
  std::unordered_set<std::string> files;
  for (const auto& file : req.file_to_generate())
    files.insert(file);

  for (const auto& desc : req.proto_file()) {
    if (!files.count(desc.name()))
      continue;
    GenerateFile(desc, resp);
  }

  if (!resp->error().empty())
    resp->clear_file();
}

int main(void) {
  comp::CodeGeneratorRequest req;
  comp::CodeGeneratorResponse resp;

  if (!req.ParseFromFileDescriptor(0)) {
    std::fprintf(stderr, "CodeGeneratorRequest read error\n");
    return 1;
  }

  Generate(req, &resp);

  if (!resp.SerializeToFileDescriptor(1)) {
    std::fprintf(stderr, "CodeGeneratorResponse write error\n");
    return 1;
  }
}
