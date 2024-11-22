#pragma once
#ifndef GITCG_ENVIRONMENT_H
#define GITCG_ENVIRONMENT_H

#include <libplatform/libplatform.h>
#include <v8.h>

#include "game.h"

namespace gitcg {
inline namespace v1_0 {

void initialize();

void cleanup();

class Environment {
  std::unique_ptr<v8::Platform> platform;
  v8::Isolate::CreateParams create_params;
  v8::Isolate* isolate;
  v8::Persistent<v8::Context> context;
  v8::Persistent<v8::Function> game_ctor;

  std::unordered_map<int, std::unique_ptr<Game>> games;
  int next_game_id = 0;

  static thread_local std::unique_ptr<Environment> instance;

public:
  Environment();

  Game* create_game();

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

  ~Environment();
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

  Game* get_game(int gameId) noexcept {
    auto it = games.find(gameId);
    if (it == games.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  static void dispose() {
    auto& _ = get_instance();
    instance.reset();
  }
};

}  // namespace v1_0
}  // namespace gitcg

#endif