#pragma once

#include <cstdint>

#include "SDL.h"

#include "games/game_catalogue.hpp"

#include "frontends/frontend_catalogue.hpp"

namespace Frontends {

    class TicTacToe : public Frontend {

        private:

            uint32_t log;

            struct sbtn {
                float x;
                float y;
                float w;
                float h;
                bool hovered;
                bool mousedown;
                void update(float mx, float my);
            };

            int mx;
            int my;

            //TODO size and color variables for frontend config
            sbtn board_buttons[3][3]; // board_buttons[y][x] origin is bottom left

        public:

            TicTacToe();

            ~TicTacToe();

            void process_event(SDL_Event event) override;

            void update() override;

            void render() override;

            void draw_options() override;

    };

    class TicTacToe_FEW : public FrontendWrap {
        public:
            TicTacToe_FEW();
            ~TicTacToe_FEW();
            bool base_game_variant_compatible(Games::BaseGameVariant* base_game_variant) override;
            Frontend* new_frontend() override;
    };

}