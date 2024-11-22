#pragma once
#ifndef GITCG_GAME_H
#define GITCG_GAME_H

#include <v8.h>

namespace gitcg {
inline namespace v1_0 {

class Environment;

using RpcHandler = void (*)(void* player_data, const char* request_data,
                            std::size_t request_len, char* response_data,
                            std::size_t* response_len) noexcept;

using NotificationHandler = void (*)(void* player_data,
                                     const char* notification_data,
                                     std::size_t notification_len) noexcept;

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

}  // namespace v1_0
}  // namespace gitcg

#endif