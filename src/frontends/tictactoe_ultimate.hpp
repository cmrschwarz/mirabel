#pragma once

#include <cstdint>

#include "SDL.h"

#include "games/game_catalogue.hpp"

#include "frontends/frontend_catalogue.hpp"

namespace Frontends {

    class TicTacToe_Ultimate : public Frontend {

        private:

            float button_size = 55;
            float local_padding = 10;
            float global_padding = 40;

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

            sbtn board_buttons[9][9]; // board_buttons[y][x] origin is bottom left

        public:

            TicTacToe_Ultimate();

            ~TicTacToe_Ultimate();

            void process_event(SDL_Event event) override;

            void update() override;

            void render() override;

            void draw_options() override;

    };

    class TicTacToe_Ultimate_FEW : public FrontendWrap {
        public:
            TicTacToe_Ultimate_FEW();
            ~TicTacToe_Ultimate_FEW();
            bool base_game_variant_compatible(Games::BaseGameVariant* base_game_variant) override;
            Frontend* new_frontend() override;
    };

}
