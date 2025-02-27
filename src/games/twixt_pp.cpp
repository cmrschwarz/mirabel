#include <cstdint>
#include <cstdlib>

#include "imgui.h"
#include "surena/games/twixt_pp.h"
#include "surena/game.h"

#include "mirabel/event_queue.h"
#include "mirabel/event.h"
#include "mirabel/game_wrap.h"
#include "control/client.hpp"

#include "games/game_catalogue.hpp"

namespace {

    struct twixt_pp_options_gw {
        twixt_pp_options o;
        bool square;
    };

    error_code opts_create(void** options_struct)
    {
        twixt_pp_options_gw* opts = (twixt_pp_options_gw*)malloc(sizeof(twixt_pp_options_gw));
        *opts = (twixt_pp_options_gw){
            .o = (twixt_pp_options){
                .wx = 24,
                .wy = 24,
                .pie_swap = true,
            },
            .square = true,
        };
        *options_struct = opts;
        return ERR_OK;
    }

    error_code opts_display(void* options_struct)
    {
        twixt_pp_options_gw* opts = (twixt_pp_options_gw*)options_struct;
        ImGui::TextDisabled("typical sizes: 24, 30, 48");
        const uint8_t min = 10;
        const uint8_t max = 48;
        ImGui::Checkbox("square", &opts->square);
        if (opts->square) {
            ImGui::SliderScalar("size", ImGuiDataType_U8, &opts->o.wx, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);
            opts->o.wy = opts->o.wx;
        } else {
            ImGui::SliderScalar("width", ImGuiDataType_U8, &opts->o.wx, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderScalar("height", ImGuiDataType_U8, &opts->o.wy, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::Checkbox("pie swap", &opts->o.pie_swap);
        return ERR_OK;
    }

    error_code opts_destroy(void* options_struct)
    {
        free(options_struct);
        return ERR_OK;
    }

    error_code opts_bin_to_str(void* options_struct, char* str_buf, size_t* ret_size)
    {
        if (str_buf == NULL) {
            *ret_size = 9;
            return ERR_OK;
        }
        twixt_pp_options* opts = (twixt_pp_options*)options_struct;
        if (opts->wx == opts->wy) {
            *ret_size = sprintf(str_buf, "%hhu%c", opts->wx, opts->pie_swap ? '+' : '\0');
        } else {
            *ret_size = sprintf(str_buf, "%hhu/%hhu%c", opts->wy, opts->wx, opts->pie_swap ? '+' : '\0');
        }
        return ERR_OK;
    }

    error_code runtime_create(game* rgame, void** runtime_struct)
    {
        //HACK
        *runtime_struct = rgame->data1; // fill runtime struct with a spoofed pointer
        return ERR_OK;
    }

    error_code runtime_display(game* rgame, void* runtime_struct)
    {
        //TODO proper state editor
        // temporary state str display
        //TODO expose state string in a proper way
        static char* state_str = NULL;
        static uint64_t state_step = 0;
        static bool changed = false;
        if (state_step != Control::main_client->game_step) {
            free(state_str);
            state_str = (char*)malloc(rgame->sizer.state_str);
            size_t _len;
            rgame->methods->export_state(rgame, &_len, state_str);
            state_step = Control::main_client->game_step;
            changed = false;
        }
        if (state_str) {
            if (ImGui::InputText("state", state_str, rgame->sizer.state_str)) {
                changed |= true;
            }
            if (changed) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(154, 58, 58, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(212, 81, 81, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(226, 51, 51, 255));
                if (ImGui::Button("S")) {
                    changed = false;
                    event_any es;
                    event_create_game_state(&es, EVENT_CLIENT_NONE, state_str);
                    event_queue_push(&Control::main_client->inbox, &es);
                }
                ImGui::PopStyleColor(3);
            }
        }
        if (ImGui::Button("reload")) {
            event_any es;
            event_create_game_state(&es, EVENT_CLIENT_NONE, state_str);
            event_queue_push(&Control::main_client->inbox, &es);
        }

        const char* move_options[4] = {"-", "WHITE", "BLACK", "-"}; // needs 2 dashes for none AND invalid
        const char* result_options[4] = {"DRAW", "WHITE", "BLACK", "-"}; // needs 2 dashes for none AND invalid
        player_id pbuf;
        uint8_t pbuf_c;
        rgame->methods->players_to_move(rgame, &pbuf_c, &pbuf);
        if (pbuf_c == 0) {
            pbuf = TWIXT_PP_PLAYER_NONE;
        }
        ImGui::Text("player to move: %s", move_options[pbuf]);
        rgame->methods->get_results(rgame, &pbuf_c, &pbuf);
        if (pbuf_c == 0) {
            pbuf = TWIXT_PP_PLAYER_INVALID;
        }
        ImGui::Text("result: %s", result_options[pbuf]);
        return ERR_OK;
    }

    error_code runtime_destroy(void* runtime_struct)
    {
        //TODO
        return ERR_OK;
    }

} // namespace

const game_wrap twixt_pp_gw{
    .game_api_version = SURENA_GAME_API_VERSION,
    .backend = &twixt_pp_gbe,
    .features = (game_wrap_feature_flags){
        .options = true,
        .runtime = true,
    },

    .opts_create = opts_create,
    .opts_display = opts_display,
    .opts_destroy = opts_destroy,

    .opts_bin_to_str = opts_bin_to_str,

    .runtime_create = runtime_create,
    .runtime_display = runtime_display,
    .runtime_destroy = runtime_destroy,
};
