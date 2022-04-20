#pragma once
//TODO maybe put client and server+this into specific directories, then control holds only general control logic

#include <cstdint>

#include "surena/game.hpp"

#include "control/event_queue.hpp"
#include "control/event.hpp"

namespace Control {

    class Lobby {
        public:
            event_queue* send_queue;

            // uint64_t id;
            char* base_game;
            char* game_variant;
            surena::Game* game;
            // bool game_trusted; // true if full game has only ever been on the server, i.e. no hidden state leaked, false if game is loaded from a user 
            uint16_t max_users;
            uint32_t* user_client_ids; //TODO should use some user struct, for now just stores client ids of connected clients

            uint32_t lobby_msg_id_ctr = 1;

            Lobby(event_queue* send_queue, uint16_t max_users);
            ~Lobby();

            void AddUser(uint32_t client_id);
            void RemoveUser(uint32_t client_id);

            void HandleEvent(event e); // handle events that are specifically assigned to this lobby

            void SendToAllButOne(event e, uint32_t excluded_client_id);
    };

}
