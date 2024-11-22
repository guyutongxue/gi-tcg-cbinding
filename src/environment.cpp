#include "environment.h"

#include <cstring>

#include "game.h"

static constexpr const char MODULE_SRC[] = R"js(
import { io } from "@gi-tcg/cbinding-io";

const RPC = 1;

export class Game {
  constructor(gameId) {
    this.gameId = gameId;
  }
  test() {
    const data = [42, 56, 127];
    const response = io(this.gameId, RPC, 0, new Uint8Array(data));
    return String.fromCodePoint(...response);
  }
}
)js";

namespace gitcg {
namespace v1_0 {

void initialize() {
  // v8::V8::InitializeICUDefaultLocation(argv[0]);
  // v8::V8::InitializeExternalStartupData(argv[0]);
  static auto platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
}

void cleanup() {
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
}

thread_local std::unique_ptr<Environment> Environment::instance;

namespace {

static constexpr int ENVIRONMENT_THIS_SLOT = 1;
enum class IoType { RPC = 1, NOTIFICATION = 2 };
static constexpr std::size_t DEFAULT_RESPONSE_SIZE = 128;

static constexpr v8::FunctionCallback io_fn_callback =
    [](const v8::FunctionCallbackInfo<v8::Value>& args) {
      auto isolate = args.GetIsolate();
      auto context = isolate->GetCurrentContext();
      auto data = context->GetEmbedderData(ENVIRONMENT_THIS_SLOT);
      auto environment =
          static_cast<Environment*>(data.As<v8::External>()->Value());
      auto gameId = args[0].As<v8::Number>()->Value();
      int ioType = args[1].As<v8::Number>()->Value();
      auto who = args[2].As<v8::Number>()->Value();
      auto request = args[3].As<v8::Uint8Array>()->Buffer();
      auto buf_len = request->ByteLength();
      auto buf_data = static_cast<char*>(request->Data());
      auto game = environment->get_game(gameId);
      auto player_data = game->get_player_data(who);
      if (ioType == static_cast<int>(IoType::RPC)) {
        auto response = static_cast<char*>(std::malloc(DEFAULT_RESPONSE_SIZE));
        auto response_len = DEFAULT_RESPONSE_SIZE;
        auto handler = game->get_rpc_handler(who);
        if (!handler) {
          auto error_message =
              v8::String::NewFromUtf8Literal(isolate, "RPC handler not set");
          isolate->ThrowError(error_message);
          return;
        }
        while (true) {
          auto required_response_len = response_len;
          handler(player_data, buf_data, buf_len, response,
                  &required_response_len);
          if (required_response_len > response_len) {
            response = static_cast<char*>(
                std::realloc(response, required_response_len));
            response_len = required_response_len;
          } else {
            response_len = required_response_len;
            break;
          }
        }
        auto response_buf = v8::ArrayBuffer::New(isolate, response_len);
        std::memcpy(response_buf->Data(), response, response_len);
        auto response_array =
            v8::Uint8Array::New(response_buf, 0, response_len);
        args.GetReturnValue().Set(response_array);
      } else if (ioType == static_cast<int>(IoType::NOTIFICATION)) {
        auto handler = game->get_notification_handler(who);
        if (!handler) {
          auto error_message = v8::String::NewFromUtf8Literal(
              isolate, "Notification handler not set");
          isolate->ThrowError(error_message);
          return;
        }
        handler(player_data, buf_data, buf_len);
      }
    };

static constexpr v8::Module::SyntheticModuleEvaluationSteps
    io_module_eval_callback =
        [](v8::Local<v8::Context> context,
           v8::Local<v8::Module> module) -> v8::MaybeLocal<v8::Value> {
  auto isolate = context->GetIsolate();
  auto io_str = v8::String::NewFromUtf8Literal(isolate, "io");
  auto io_fn = v8::FunctionTemplate::New(isolate, io_fn_callback);
  auto io_fn_instance = io_fn->GetFunction(context).ToLocalChecked();
  module->SetSyntheticModuleExport(isolate, io_str, io_fn_instance).FromJust();
  auto undefined = v8::Undefined(isolate);
  auto promise_resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
  promise_resolver->Resolve(context, undefined).FromJust();
  return promise_resolver->GetPromise();
};

static constexpr v8::Module::ResolveModuleCallback resolve_module_callback =
    [](v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
       v8::Local<v8::FixedArray> import_assertions,
       v8::Local<v8::Module> referrer) -> v8::MaybeLocal<v8::Module> {
  auto isolate = context->GetIsolate();
  auto expected_specifier =
      v8::String::NewFromUtf8Literal(isolate, "@gi-tcg/cbinding-io");
  if (!specifier->StringEquals(expected_specifier)) {
    auto error_message =
        v8::String::NewFromUtf8Literal(isolate, "Module not found");
    isolate->ThrowError(error_message);
    return v8::MaybeLocal<v8::Module>{};
  }
  std::vector<v8::Local<v8::String>> export_names = {
      v8::String::NewFromUtf8Literal(isolate, "io")};
  auto io_module = v8::Module::CreateSyntheticModule(
      isolate, specifier, export_names, io_module_eval_callback);
  return io_module;
};

}  // namespace

Environment::Environment() {
  platform = v8::platform::NewDefaultPlatform();
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  isolate = v8::Isolate::New(create_params);

  auto isolate_scope = v8::Isolate::Scope(isolate);
  auto handle_scope = v8::HandleScope(isolate);
  auto context = v8::Context::New(isolate);
  this->context.Reset(isolate, context);
  context->Enter();
  context->SetEmbedderData(ENVIRONMENT_THIS_SLOT,
                           v8::External::New(isolate, this));

  v8::Local<v8::String> source_string =
      v8::String::NewFromUtf8Literal(isolate, MODULE_SRC);
  v8::Local<v8::String> resource_name =
      v8::String::NewFromUtf8Literal(isolate, "main.js");
  v8::ScriptOrigin origin(isolate, resource_name, 0, 0, false, -1,
                          v8::Local<v8::Value>{}, false, false, true);
  v8::ScriptCompiler::Source source(source_string, origin);
  auto main_module_maybe = v8::ScriptCompiler::CompileModule(isolate, &source);
  v8::Local<v8::Module> main_module;
  if (!main_module_maybe.ToLocal(&main_module)) {
    throw std::runtime_error("Failed to compile module");
  }
  main_module->InstantiateModule(context, resolve_module_callback).FromJust();
  main_module->Evaluate(context).ToLocalChecked();

  auto main_namespace = main_module->GetModuleNamespace().As<v8::Object>();
  auto game_str = v8::String::NewFromUtf8Literal(isolate, "Game");
  auto game_ctor = main_namespace->Get(context, game_str).ToLocalChecked();
  this->game_ctor.Reset(isolate, game_ctor.As<v8::Function>());
}

Game* Environment::create_game() {
  auto handle_scope = v8::HandleScope(isolate);
  auto context = get_context();
  auto game_ctor = this->game_ctor.Get(isolate);
  auto game_id = next_game_id++;
  auto game_id_value = v8::Number::New(isolate, game_id);
  std::vector<v8::Local<v8::Value>> game_ctor_args = {game_id_value};
  auto game_instance =
      game_ctor
          ->NewInstance(context, game_ctor_args.size(), game_ctor_args.data())
          .ToLocalChecked();
  auto [it, _] = games.emplace(
      game_id, std::make_unique<Game>(this, game_id, game_instance));
  return it->second.get();
}

Environment::~Environment() {
  games.clear();
  context.Reset();
  isolate->Dispose();
  delete create_params.array_buffer_allocator;
}

}  // namespace v1_0
}  // namespace gitcg
