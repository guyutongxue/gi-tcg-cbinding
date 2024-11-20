#include <libplatform/libplatform.h>
#include <v8.h>

constexpr const char MODULE_SRC[] = R"js(
// import { io } from "@gi-tcg/cbinding";

export class Game {
  test() {
    return "Hello, world!";
  }
}
)js";

namespace gitcg {
inline namespace v1 {

void initialize() {
  static auto platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
}

void cleanup() {
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
}

class Context {
  std::unique_ptr<v8::Platform> platform;
  v8::Isolate::CreateParams create_params;
  v8::Isolate* isolate;
  v8::Persistent<v8::Context> context;

  static constexpr v8::Module::ResolveModuleCallback resolve_module_callback =
      [](v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
         v8::Local<v8::FixedArray> import_assertions,
         v8::Local<v8::Module> referrer) {
        return v8::MaybeLocal<v8::Module>{};
      };

  Context() {
    platform = v8::platform::NewDefaultPlatform();
    create_params.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    isolate = v8::Isolate::New(create_params);
    isolate->Enter();
    auto handle_scope = v8::HandleScope(isolate);
    auto context = v8::Context::New(isolate);
    this->context.Reset(isolate, context);
    context->Enter();

    v8::Local<v8::String> source_string =
        v8::String::NewFromUtf8Literal(isolate, MODULE_SRC);
    v8::ScriptCompiler::Source source(source_string);
    auto main_module_maybe =
        v8::ScriptCompiler::CompileModule(isolate, &source);
    v8::Local<v8::Module> main_module;
    if (!main_module_maybe.ToLocal(&main_module)) {
      throw std::runtime_error("Failed to compile module");
    }
    main_module->InstantiateModule(context, resolve_module_callback);
    main_module->Evaluate(context);

    auto main_namespace = main_module->GetModuleNamespace().As<v8::Object>();
    auto game_str = v8::String::NewFromUtf8Literal(isolate, "Game");
    auto game_ctor = main_namespace->Get(context, game_str).ToLocalChecked();
    auto game_instance = game_ctor.As<v8::Function>()->NewInstance(context, 0, nullptr).ToLocalChecked();
    
    auto test_str = v8::String::NewFromUtf8Literal(isolate, "test");
    auto test_fn = game_instance->Get(context, test_str).ToLocalChecked().As<v8::Function>();
    auto test_result = test_fn->Call(context, game_instance, 0, nullptr).ToLocalChecked().As<v8::String>();
    auto test_result_str = v8::String::Utf8Value{isolate, test_result};
    std::printf("Test result: %s\n", *test_result_str);
  }
  ~Context() {
    context.Get(isolate)->Exit();
    context.Reset();
    isolate->Exit();
    isolate->Dispose();
    delete create_params.array_buffer_allocator;
  }
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

public:
  static Context& get_instance() {
    thread_local Context context;
    return context;
  }
};

}  // namespace v1
}  // namespace gitcg

int main() {
  gitcg::initialize();
  {
    auto& context = gitcg::Context::get_instance();
    std::printf("Hello, World!\n");
  }
  gitcg::cleanup();
}