#include <cctype>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

// protoc --plugin=protoc-gen-brpc=bazel-bin/brpc/codegen --brpc_out=. brpc/testing/echo_service.proto

#include "brpc/options.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/compiler/plugin.pb.h"

namespace pb = ::google::protobuf;
namespace comp = pb::compiler;

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
  guard += "_H";
  return guard;
}

struct Method {
  std::string name;
  std::uint32_t code;
  std::string req_type;
  bool req_stream;
  std::string resp_type;
  bool resp_stream;
};

bool ParseMethod(const pb::MethodDescriptorProto& method, Method* out, std::ostringstream& err) {
  out->name = method.name();

  if (!method.options().HasExtension(brpc::brpc)) {
    err << out->name << ": missing { option (brpc).code = N; }\n";
    return false;
  }
  out->code = method.options().GetExtension(brpc::brpc).code();

  out->req_type = FormatNamespace(method.input_type());
  out->req_stream = method.server_streaming();

  out->resp_type = FormatNamespace(method.output_type());
  out->resp_stream = method.client_streaming();

  if (out->req_stream != out->resp_stream) {
    err << out->name << ": must be either bidirectionally or not at all streaming\n";
    return false;
  }

  return true;
}

void GenerateService(const pb::ServiceDescriptorProto& service, std::ostringstream& out, std::ostringstream& err) {
  // parse

  std::vector<Method> methods;
  bool failed_methods = false;

  for (const auto& method : service.method()) {
    methods.emplace_back();
    Method& m = methods.back();
    if (!ParseMethod(method, &m, err))
      failed_methods = true;
  }
  if (failed_methods)
    return;

  // generate

  out << "// service " << service.name() << "\n\n";

  out << "struct " << service.name() << "Method {\n";
  out << "  enum Code : std::uint32_t {\n";
  for (const Method& m : methods)
    out << "    k" << m.name << " = " << m.code << ",\n";
  out << "  };\n";
  out << "};\n\n";

  out << "struct " << service.name() << "Interface {\n";
  for (const Method& m : methods) {
    out << "  // rpc " << m.name << " (" << (m.req_stream ? "stream " : "") << m.req_type << ") returns (" << (m.resp_stream ? "stream " : "") << m.resp_type << ");\n";
    if (m.req_stream && m.resp_stream) {
      out << "  class " << m.name << "Call;\n";
      out << "  struct " << m.name << "Handler {\n";
      out << "    virtual void " << m.name << "Open(" << m.name << "Call* call) = 0;\n";
      out << "    virtual void " << m.name << "Message(" << m.name << "Call* call, const " << m.req_type << "& req) = 0;\n";
      out << "    virtual void " << m.name << "Close(" << m.name << "Call* call, ::std::unique_ptr<::base::error> error) = 0;\n";
      out << "    virtual ~" << m.name << "Handler() = default;\n";
      out << "  };\n";
      out << "  class " << m.name << "Call : public ::brpc::RpcEndpoint {\n";
      out << "   public:\n";
      out << "    " << m.name << "Call(::base::optional_ptr<" << m.name << "Handler> handler) : handler_(::std::move(handler)) {}\n";
      out << "    void Send(const " << m.resp_type << "& resp) { call_->Send(resp); }\n";
      out << "    void Close() { call_->Close(); }\n";
      out << "   private:\n";
      out << "    ::brpc::RpcCall* call_;\n";
      out << "    ::base::optional_ptr<" << m.name << "Handler> handler_;\n";
      out << "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall* call) override;\n";
      out << "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n";
      out << "    void RpcClose(::brpc::RpcCall* call, ::std::unique_ptr<::base::error> error) override;\n";
      out << "  };\n";
      out << "  virtual ::base::optional_ptr<" << m.name << "Handler> " << m.name << "() = 0;\n\n";
    } else {
      out << "  virtual bool " << m.name << "(const " << m.req_type << "& req, " << m.resp_type << "* resp) = 0;\n\n";
    }
  }
  out << "  virtual void " << service.name() << "Error(::std::unique_ptr<::base::error> error) = 0;\n\n";
  out << "  virtual ~" << service.name() << "Interface() = default;\n";
  out << "};\n\n";
  for (const Method& m : methods) {
    if (!m.req_stream && !m.resp_stream)
      continue;
    out << "inline ::std::unique_ptr<::google::protobuf::Message> " << service.name() << "Interface::" << m.name << "Call::RpcOpen(::brpc::RpcCall* call) {\n";
    out << "  call_ = call;\n";
    out << "  handler_->" << m.name << "Open(this);\n";
    out << "  return ::std::make_unique<" << m.req_type << ">();\n";
    out << "}\n\n";
    out << "inline void " << service.name() << "Interface::" << m.name << "Call::RpcMessage(::brpc::RpcCall*, const ::google::protobuf::Message& message) {\n";
    out << "  handler_->" << m.name << "Message(this, static_cast<const " << m.req_type << "&>(message));\n";
    out << "}\n\n";
    out << "inline void " << service.name() << "Interface::" << m.name << "Call::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n";
    out << "  handler_->" << m.name << "Close(this, ::std::move(error));\n";
    out << "}\n\n";
  }

  out << "class " << service.name() << "Server : public ::brpc::RpcDispatcher {\n";
  out << " public:\n";
  out << "  " << service.name() << "Server(::base::optional_ptr<" << service.name() << "Interface> impl) : server_(this), impl_(::std::move(impl)) {}\n";
  out << "  ::std::unique_ptr<::base::error> Start(::event::Loop* loop, const ::std::string& path) { return server_.Start(loop, path); }\n";
  out << " private:\n";
  for (const Method& m : methods) {
    if (m.req_stream && m.resp_stream)
      continue;
    out << "  class " << m.name << "Endpoint : public ::brpc::RpcEndpoint {\n";
    out << "   public:\n";
    out << "    " << m.name << "Endpoint(" << service.name() << "Interface* impl) : impl_(impl) {}\n";
    out << "   private:\n";
    out << "    " << service.name() << "Interface* impl_;\n";
    out << "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall*) override;\n";
    out << "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n";
    out << "    void RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) override;\n";
    out << "  };\n\n";
  }
  out << "  ::brpc::RpcServer server_;\n";
  out << "  ::base::optional_ptr<" << service.name() << "Interface> impl_;\n";
  out << "  ::std::unique_ptr<::brpc::RpcEndpoint> RpcOpen(::std::uint32_t method) override;\n";
  out << "  void RpcError(::std::unique_ptr<::base::error> error) override;\n";
  out << "};\n\n";
  for (const Method& m : methods) {
    if (m.req_stream && m.resp_stream)
      continue;
    out << "inline ::std::unique_ptr<::google::protobuf::Message> " << service.name() << "Server::" << m.name << "Endpoint::RpcOpen(::brpc::RpcCall*) {\n";
    out << "  return ::std::make_unique<" << m.req_type << ">();\n";
    out << "}\n\n";
    out << "inline void " << service.name() << "Server::" << m.name << "Endpoint::RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) {\n";
    out << "  " << m.resp_type << " resp;\n";
    out << "  if (impl_->" << m.name << "(static_cast<const " << m.req_type << "&>(message), &resp))\n";
    out << "    call->Send(resp);\n";
    out << "  call->Close();\n";
    out << "}\n\n";
    out << "inline void " << service.name() << "Server::" << m.name << "Endpoint::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n";
    out << "  if (error) impl_->" << service.name() << "Error(::std::move(error));\n";
    out << "}\n\n";
  }
  out << "inline ::std::unique_ptr<::brpc::RpcEndpoint> " << service.name() << "Server::RpcOpen(::std::uint32_t method) {\n";
  out << "  switch (method) {\n";
  for (const Method& m : methods) {
    out << "    case " << service.name() << "Method::k" << m.name << ":\n";
    if (m.req_stream && m.resp_stream) {
      out << "      {\n";
      out << "        auto handler = impl_->" << m.name << "();\n";
      out << "        if (handler)\n";
      out << "          return ::std::make_unique<" << service.name() << "Interface::" << m.name << "Call>(std::move(handler));\n";
      out << "        else\n";
      out << "          return nullptr;\n";
      out << "      }\n";
    } else {
      out << "      return ::std::make_unique<" << m.name << "Endpoint>(impl_.get());\n";
    }
  }
  out << "  }\n";
  out << "  return nullptr;\n";
  out << "}\n\n";
  out << "inline void " << service.name() << "Server::RpcError(::std::unique_ptr<::base::error> error) {\n";
  out << "  impl_->" << service.name() << "Error(::std::move(error));\n";
  out << "}\n\n";

  out << "class " << service.name() << "Client {\n";
  out << " public:\n";
  out << "  " << service.name() << "Client() = default;\n";
  out << "  ::event::Socket::Builder& target() { return client_.target(); }\n\n";
  for (const Method& m : methods) {
    if (m.req_stream && m.resp_stream) {
      out << "  class " << m.name << "Call;\n";
      out << "  struct " << m.name << "Receiver {\n";
      out << "    virtual void " << m.name << "Open(" << m.name << "Call* call) = 0;\n";
      out << "    virtual void " << m.name << "Message(" << m.name << "Call* call, const " << m.resp_type << "& req) = 0;\n";
      out << "    virtual void " << m.name << "Close(" << m.name << "Call* call, ::std::unique_ptr<::base::error> error) = 0;\n";
      out << "    virtual ~" << m.name << "Receiver() = default;\n";
      out << "  };\n";
      out << "  class " << m.name << "Call : public ::brpc::RpcEndpoint {\n";
      out << "   public:\n";
      out << "    " << m.name << "Call(::base::optional_ptr<" << m.name << "Receiver> receiver) : receiver_(::std::move(receiver)) {}\n";
      out << "    void Send(const " << m.req_type << "& req) { call_->Send(req); }\n";
      out << "    void Close() { call_->Close(); }\n";
      out << "   private:\n";
      out << "    ::brpc::RpcCall* call_;\n";
      out << "    ::base::optional_ptr<" << m.name << "Receiver> receiver_;\n";
      out << "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall* call) override;\n";
      out << "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n";
      out << "    void RpcClose(::brpc::RpcCall* call, ::std::unique_ptr<::base::error> error) override;\n";
      out << "  };\n";
      out << "  " << m.name << "Call* " << m.name << "(::base::optional_ptr<" << m.name << "Receiver> receiver);\n\n";
    } else {
      out << "  struct " << m.name << "Receiver {\n";
      out << "    virtual void " << m.name << "Done(const " << m.resp_type << "& resp) = 0;\n";
      out << "    virtual void " << m.name << "Failed(::std::unique_ptr<::base::error> error) = 0;\n";
      out << "    virtual ~" << m.name << "Receiver() = default;\n";
      out << "  };\n";
      out << "  void " << m.name << "(const " << m.req_type << "& req, ::base::optional_ptr<" << m.name << "Receiver> receiver);\n\n";
    }
  }
  out << " private:\n";
  for (const Method& m : methods) {
    if (m.req_stream && m.resp_stream)
      continue;
    out << "  class " << m.name << "Endpoint : public ::brpc::RpcEndpoint {\n";
    out << "   public:\n";
    out << "    " << m.name << "Endpoint(::base::optional_ptr<" << m.name << "Receiver> receiver) : receiver_(::std::move(receiver)) {}\n";
    out << "   private:\n";
    out << "    ::base::optional_ptr<" << m.name << "Receiver> receiver_;\n";
    out << "    bool received_ = false;\n";
    out << "    ::std::unique_ptr<::google::protobuf::Message> RpcOpen(::brpc::RpcCall*) override;\n";
    out << "    void RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) override;\n";
    out << "    void RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) override;\n";
    out << "  };\n\n";
  }
  out << "  ::brpc::RpcClient client_;\n";
  out << "};\n\n";
  for (const Method& m : methods) {
    if (m.req_stream && m.resp_stream) {
      out << "inline ::std::unique_ptr<::google::protobuf::Message> " << service.name() << "Client::" << m.name << "Call::RpcOpen(::brpc::RpcCall* call) {\n";
      out << "  call_ = call;\n";
      out << "  receiver_->" << m.name << "Open(this);\n";
      out << "  return ::std::make_unique<" << m.resp_type << ">();\n";
      out << "}\n\n";
      out << "inline void " << service.name() << "Client::" << m.name << "Call::RpcMessage(::brpc::RpcCall*, const ::google::protobuf::Message& message) {\n";
      out << "  receiver_->" << m.name << "Message(this, static_cast<const " << m.resp_type << "&>(message));\n";
      out << "}\n\n";
      out << "inline void " << service.name() << "Client::" << m.name << "Call::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n";
      out << "  receiver_->" << m.name << "Close(this, ::std::move(error));\n";
      out << "}\n\n";
      out << "inline " << service.name() << "Client::" << m.name << "Call* " << service.name() << "Client::" << m.name << "(::base::optional_ptr<" << m.name << "Receiver> receiver) {\n";
      out << "  ::std::unique_ptr<" << m.name << "Call> call = ::std::make_unique<" << m.name << "Call>(::std::move(receiver));\n";
      out << "  " << m.name << "Call* call_ref = call.get();\n";
      out << "  client_.Call(::std::move(call), " << service.name() << "Method::k" << m.name << ", nullptr);\n";
      out << "  return call_ref;\n";
      out << "}\n\n";
    } else {
      out << "inline ::std::unique_ptr<::google::protobuf::Message> " << service.name() << "Client::" << m.name << "Endpoint::RpcOpen(::brpc::RpcCall*) {\n";
      out << "  return ::std::make_unique<" << m.resp_type << ">();\n";
      out << "}\n\n";
      out << "inline void " << service.name() << "Client::" << m.name << "Endpoint::RpcMessage(::brpc::RpcCall* call, const ::google::protobuf::Message& message) {\n";
      out << "  if (!received_) {\n";
      out << "    receiver_->" << m.name << "Done(static_cast<const " << m.resp_type << "&>(message));\n";
      out << "    received_ = true;\n";
      out << "  }\n";
      out << "  call->Close();\n";
      out << "}\n\n";
      out << "inline void " << service.name() << "Client::" << m.name << "Endpoint::RpcClose(::brpc::RpcCall*, ::std::unique_ptr<::base::error> error) {\n";
      out << "  if (!received_) {\n";
      out << "    receiver_->" << m.name << "Failed(error ? ::std::move(error) : ::base::make_error(\"no answer\"));\n";
      out << "    received_ = true;\n";
      out << "  }\n";
      out << "}\n\n";
      out << "inline void " << service.name() << "Client::" << m.name << "(const " << m.req_type << "& req, ::base::optional_ptr<" << m.name << "Receiver> receiver) {\n";
      out << "  client_.Call(::std::make_unique<" << m.name << "Endpoint>(::std::move(receiver)), " << service.name() << "Method::k" << m.name << ", &req);\n";
      out << "}\n\n";
    }
  }
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

  std::string header_guard = FormatHeaderGuard(base_filename);
  std::string package_ns = FormatNamespace(desc.package());

  // generate

  auto* file = resp->add_file();

  file->set_name(base_filename + ".brpc.h");

  std::ostringstream out;
  std::ostringstream err;

  out << "// Generated by the brpc compiler.  DO NOT EDIT!\n";
  out << "// source: " << desc.name() << "\n\n";

  out << "#ifndef " << header_guard << "\n";
  out << "#define " << header_guard << "\n\n";

  out << "#include \"base/common.h\"\n";
  out << "#include \"base/exc.h\"\n";
  out << "#include \"brpc/brpc.h\"\n";
  out << "#include \"" << base_filename << ".pb.h\"\n\n";

  out << "namespace " << package_ns << " {\n\n";

  for (const auto& service : desc.service())
    GenerateService(service, out, err);

  out << "} // namespace " << package_ns << "\n\n";

  out << "#endif // " << header_guard << "\n";
  out << "\n// Local Variables:\n// mode: c++\n// End:\n";

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
