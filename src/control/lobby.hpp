#pragma once
//TODO maybe put client and server+this into specific directories, then control holds only general control logic

#include <cstdint>

#include "surena/game.h"

#include "mirabel/event_queue.h"
#include "mirabel/event.h"
#include "control/plugins.hpp"

namespace Control {

    class Lobby {
      public:

        PluginManager* plugin_mgr;
        event_queue* send_queue;

        // uint64_t id;
        char* game_base;
        char* game_variant;
        char* game_impl;
        char* game_options;
        game* the_game;
        // bool game_trusted; // true if full game has only ever been on the server, i.e. no hidden state leaked, false if game is loaded from a user
        uint16_t max_users;
        uint32_t* user_client_ids; //TODO should use some user struct, for now just stores client ids of connected clients

        uint32_t lobby_msg_id_ctr = 1;

        Lobby(PluginManager* plugin_mgr, event_queue* send_queue, uint16_t max_users);
        ~Lobby();

        void AddUser(uint32_t client_id);
        void RemoveUser(uint32_t client_id);

        void HandleEvent(event_any e); // handle events that are specifically assigned to this lobby

        void SendToAllButOne(event_any e, uint32_t excluded_client_id);
    };

} // namespace Control
