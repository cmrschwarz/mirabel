#include <cstdint>
#include <cstdio>

#include <SDL2/SDL.h>
#include "SDL_net.h"
#include "nanovg.h"
#include <openssl/ssl.h>
#include "imgui.h"
#include "surena/engine.h"
#include "surena/game.h"
#include "surena/move_history.h"

#include "mirabel/engine_wrap.h"
#include "mirabel/frontend.h"
#include "mirabel/game_wrap.h"
#include "control/client.hpp"
#include "generated/git_commit_hash.h"

#include "meta_gui/meta_gui.hpp"

namespace MetaGui {

    SDL_version sdl_version_compiled;
    SDL_version sdl_version_linked;
    bool init_sdl_version = false;

    SDLNet_version sdl_net_version_compiled;
    SDLNet_version sdl_net_version_linked;
    bool init_sdl_net_version = false;

    void about_window(bool* p_open)
    {
        ImGui::SetNextWindowSize(ImVec2(475, 300), ImGuiCond_FirstUseEver);
        bool window_contents_visible = ImGui::Begin("About", p_open);
        if (!window_contents_visible) {
            ImGui::End();
            return;
        }

        ImGui::Text("MIRABEL"); // make bigger
        ImGui::Text("client version: %u.%u.%u", Control::client_version.major, Control::client_version.minor, Control::client_version.patch);
        ImGui::Text("mirabel api versions: frontend(%lu) gamewrap(%lu) enginewrap(%lu)", MIRABEL_FRONTEND_API_VERSION, MIRABEL_GAME_WRAP_API_VERSION, MIRABEL_ENGINE_WRAP_API_VERSION);
        ImGui::Text("git commit hash: %s%s", GIT_COMMIT_HASH == NULL ? "<no commit info available>" : GIT_COMMIT_HASH, GIT_COMMIT_DIRTY ? " (dirty)" : "");
        ImGui::Separator();
        {
            // SDL
            if (init_sdl_version == false) {
                SDL_VERSION(&sdl_version_compiled);
                SDL_GetVersion(&sdl_version_linked);
                init_sdl_version = true;
            }
            ImGui::Text("SDL version: %u.%u.%u", sdl_version_linked.major, sdl_version_linked.minor, sdl_version_linked.patch);
            if (memcmp(&sdl_version_linked, &sdl_version_compiled, sizeof(SDL_version)) != 0) {
                ImGui::SameLine();
                ImGui::Text("(compiled %u.%u.%u)", sdl_version_compiled.major, sdl_version_compiled.minor, sdl_version_compiled.patch);
            }
        }
        //TODO opengl ?
        {
            // SDL_net
            if (init_sdl_net_version == false) {
                SDL_NET_VERSION(&sdl_net_version_compiled);
                const SDLNet_version* lv = SDLNet_Linked_Version();
                memcpy(&sdl_net_version_linked, lv, sizeof(SDLNet_version));
                init_sdl_net_version = true;
            }
            ImGui::Text("SDL_net version: %u.%u.%u", sdl_net_version_linked.major, sdl_net_version_linked.minor, sdl_net_version_linked.patch);
            if (memcmp(&sdl_net_version_linked, &sdl_net_version_compiled, sizeof(SDLNet_version)) != 0) {
                ImGui::SameLine();
                ImGui::Text("(compiled %u.%u.%u)", sdl_net_version_compiled.major, sdl_net_version_compiled.minor, sdl_net_version_compiled.patch);
            }
        }
        {
            // OpenSSL
            ImGui::Text("OpenSSL version: %s", OPENSSL_FULL_VERSION_STR);
        }
        {
            // dear imgui
            ImGui::Text("dear imgui version: %s", IMGUI_VERSION);
        }
        {
            // surena
            ImGui::Text("surena version: game(%lu) engine(%lu) movehistory(%lu)", SURENA_GAME_API_VERSION, SURENA_ENGINE_API_VERSION, SURENA_MOVE_HISTORY_API_VERSION);
        }
        //TODO separator and button with repo link

        ImGui::End();
    }

} // namespace MetaGui
