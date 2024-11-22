#include <libplatform/libplatform.h>
#include <v8.h>

#include <cstring>

constexpr const char MODULE_SRC[] = R"js(
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
inline namespace v1_0 {

using RpcHandler = void (*)(void* player_data, const char* request_data,
                            std::size_t request_len, char* response_data,
                            std::size_t* response_len) noexcept;

using NotificationHandler = void (*)(void* player_data,
                                     const char* notification_data,
                                     std::size_t notification_len) noexcept;

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

class Environment;

class Game {
  Environment* const environment;
  const int game_id;
  v8::UniquePersistent<v8::Object> instance;

  void* player_data[2]{};
  RpcHandler rpc_handler[2]{};
  NotificationHandler notification_handler[2]{};

public:
  Game(Environment* environment, int game_id, v8::Local<v8::Object> instance);

  void* get_player_data(int who) const {
    return player_data[who];
  }
  void set_player_data(int who, void* data) {
    player_data[who] = data;
  }
  RpcHandler get_rpc_handler(int who) const {
    return rpc_handler[who];
  }
  void set_rpc_handler(int who, RpcHandler handler) {
    rpc_handler[who] = handler;
  }
  NotificationHandler get_notification_handler(int who) const {
    return notification_handler[who];
  }
  void set_notification_handler(int who, NotificationHandler handler) {
    notification_handler[who] = handler;
  }

  void test() const;
};

class Environment {
  std::unique_ptr<v8::Platform> platform;
  v8::Isolate::CreateParams create_params;
  v8::Isolate* isolate;
  v8::Persistent<v8::Context> context;
  v8::Persistent<v8::Function> game_ctor;

  std::unordered_map<int, std::unique_ptr<Game>> games;
  int next_game_id = 0;

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
        auto game = environment->games.at(gameId).get();
        auto player_data = game->get_player_data(who);
        if (ioType == static_cast<int>(IoType::RPC)) {
          auto response =
              static_cast<char*>(std::malloc(DEFAULT_RESPONSE_SIZE));
          auto response_len = DEFAULT_RESPONSE_SIZE;
          auto handler = game->get_rpc_handler(who);
          if (!handler) {
            auto error_message = v8::String::NewFromUtf8Literal(
                isolate, "RPC handler not set");
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
    module->SetSyntheticModuleExport(isolate, io_str, io_fn_instance)
        .FromJust();
    auto undefined = v8::Undefined(isolate);
    auto promise_resolver =
        v8::Promise::Resolver::New(context).ToLocalChecked();
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

  static thread_local std::unique_ptr<Environment> instance;

public:
  Environment() {
    platform = v8::platform::NewDefaultPlatform();
    create_params.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    isolate = v8::Isolate::New(create_params);
    {
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
      auto main_module_maybe =
          v8::ScriptCompiler::CompileModule(isolate, &source);
      v8::Local<v8::Module> main_module;
      if (!main_module_maybe.ToLocal(&main_module)) {
        throw std::runtime_error("Failed to compile module");
      }
      main_module->InstantiateModule(context, resolve_module_callback)
          .FromJust();
      main_module->Evaluate(context).ToLocalChecked();

      auto main_namespace = main_module->GetModuleNamespace().As<v8::Object>();
      auto game_str = v8::String::NewFromUtf8Literal(isolate, "Game");
      auto game_ctor = main_namespace->Get(context, game_str).ToLocalChecked();
      this->game_ctor.Reset(isolate, game_ctor.As<v8::Function>());
    }
  }

  Game* create_game() {
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

  v8::Isolate* get_isolate() {
    return isolate;
  }
  /**
   * Get v8::Local<v8::Context> of current execution context.
   * MUST be called under a handle_scope.
   */
  v8::Local<v8::Context> get_context() const {
    return context.Get(isolate);
  }

  ~Environment() {
    games.clear();
    context.Reset();
    isolate->Dispose();
    delete create_params.array_buffer_allocator;
  }
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;

  static Environment& get_instance() {
    if (!instance) {
      throw std::runtime_error(
          "Context instance does not exist on this thread");
    }
    return *instance;
  }

public:
  static Environment& create() {
    if (instance) {
      throw std::runtime_error(
          "Context instance already exists on this thread");
    }
    instance = std::make_unique<Environment>();
    return *instance;
  }
  static void dispose() {
    auto& _ = get_instance();
    instance.reset();
  }
};

Game::Game(Environment* environment, int game_id,
           v8::Local<v8::Object> instance)
    : environment{environment}, game_id{game_id} {
  this->instance.Reset(environment->get_isolate(), instance);
}

void Game::test() const {
  auto isolate = environment->get_isolate();
  auto handle_scope = v8::HandleScope(isolate);
  auto context = environment->get_context();
  auto instance = this->instance.Get(isolate);
  auto test_str = v8::String::NewFromUtf8Literal(isolate, "test");
  auto test_fn =
      instance->Get(context, test_str).ToLocalChecked().As<v8::Function>();
  auto test_result = test_fn->Call(context, instance, 0, nullptr)
                         .ToLocalChecked()
                         .As<v8::String>();
  auto test_result_str = v8::String::Utf8Value{isolate, test_result};
  std::printf("Test result: %s\n", *test_result_str);
}

thread_local std::unique_ptr<Environment> Environment::instance;

}  // namespace v1_0
}  // namespace gitcg

int main(int argc, char** argv) {
  gitcg::initialize();
  {
    auto& env = gitcg::Environment::create();
    std::printf("11111\n");
    auto game = env.create_game();
    game->set_rpc_handler(0, [](void* player_data, const char* request_data,
                                std::size_t request_len, char* response_data,
                                std::size_t* response_len) noexcept {
      for (std::size_t i = 0; i < request_len; ++i) {
        std::printf("%d ", static_cast<int>(request_data[i]));
      }
      std::printf("RPC handler called\n");
      std::memcpy(response_data, "Hello, I'm response!", 20);
      *response_len = 20;
    });
    game->test();
    std::printf("22222\n");
    gitcg::Environment::dispose();
  }
  gitcg::cleanup();
}