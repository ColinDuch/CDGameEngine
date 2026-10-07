#include <engine/core/Application.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <string>

/*
-----------------------------------------------------------------
Panneau Controles du jeu : les actions du projet (input.json)
-----------------------------------------------------------------
*/

namespace {
    // axes de manette proposes : un sens de strick ou de gachette
    struct AxisChoice { int axis; float sign; const char* label; };
    const AxisChoice AXES[] = {
        { -1, 1.0f, "(none)" },
        { GLFW_GAMEPAD_AXIS_LEFT_X, -1.0f, "Left stick left"},
        { GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f, "Left stick right"},
        { GLFW_GAMEPAD_AXIS_LEFT_Y, -1.0f, "Left stick up"},
        { GLFW_GAMEPAD_AXIS_LEFT_Y, 1.0f, "Left stick down" },
        { GLFW_GAMEPAD_AXIS_RIGHT_X, -1.0f, "Right stick left" },
        { GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f, "Right stick right" },
        { GLFW_GAMEPAD_AXIS_RIGHT_Y, -1.0f, "Right stick up" },
        { GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, "Right stick down" },
        { GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f, "Left trigger" },
        { GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f, "Right trigger" },
    };

    const char* axisLabel(const InputBinding& b) {
        for (const auto& c : AXES) {
            if (c.axis == b.padAxis && (c.axis < 0 || c.sign == b.axisSign)) return Lang::tr(c.label);
        }
        return "?";
    }

    enum Slot { SLOT_KEY = 0, SLOT_ALT_KEY, SLOT_PAD_BUTTON, SLOT_KEY_MOD };
    
    bool hasPadMod(const InputBinding& b) { return b.padModButton >= 0 || b.padModAxis >= 0; }

    std::string padModLabel(const InputBinding& b) {
        if (b.padModButton >= 0) return InputMap::padButtonName(b.padModButton);
        for (const auto& c : AXES) {
            if (c.axis >= 0 && c.axis == b.padModAxis && c.sign == b.padModSign) return Lang::tr(c.label);
        }
        return Lang::tr("(none)");
    }
}

void Application::drawGameInputPanel() {
    if (!showGameInputPanel) {
        gameRebindAction = -1;      // fermer le panneau annule une capture en cours
        return;
    }
    ImGui::Begin(trWin("Game Controls", "gamecontrols").c_str(), &showGameInputPanel, panelFlags);

    std::vector<GameAction>& actions = gameInput.getActions();

    if (input.gamepadConnected()) ImGui::TextDisabled(Lang::tr("Gamepad: %s"), input.gamepadName().c_str());
    else ImGui::TextDisabled("%s", Lang::tr("No gamepad detected"));
    ImGui::TextDisabled("%s", Lang::tr("Scripts use the action name: Engine.isActionDown(\"name\")"));
    ImGui::Separator();

    // ------------------------------------------ Capture
    if (gameRebindAction >= static_cast<int>(actions.size())) gameRebindAction = -1;
    if (gameRebindAction >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", Lang::tr("Press a key or a button... (Escape to cancel)"));
        
        const int k = InputMap::pollAnyKey(window->getHandle());
        const int b = InputMap::pollAnyPadButton();
        InputBinding& bind = actions[gameRebindAction].binding;

        if (k == GLFW_KEY_ESCAPE) {
            gameRebindAction = -1;
        } else if (gameRebindSlot == SLOT_PAD_BUTTON) {
            if (b >= 0) { pushUndo(); bind.padButton = b; gameRebindAction = -1; }
        } else if (k >= 0) {
            pushUndo();
            int& field = (gameRebindSlot == SLOT_KEY) ? bind.key : (gameRebindSlot == SLOT_ALT_KEY) ? bind.altKey : bind.keyModifier;
            field = k;
            gameRebindAction = -1;
        }
        ImGui::Separator();
    }

    // ------------------------------------------ Actions
    int removeIndex = -1;
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##gameactions", 8, tf)) {
        ImGui::TableSetupColumn(Lang::tr("Action"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(Lang::tr("Key"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(Lang::tr("Alt key"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(Lang::tr("+ Key"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(Lang::tr("Gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(Lang::tr("Stick / trigger"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(Lang::tr("+ Gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i= 0; i < static_cast<int>(actions.size()); ++i) {
            GameAction& a = actions[i];
            InputBinding& b = a.binding;
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // nom : c'est lui que les scripts utilisent
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##name", &a.name);
            if (ImGui::IsItemActivated()) pushUndo();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                // vide ou en double : l'action deviendrait introuvable depuis Lua
                std::string base = a.name.empty() ? std::string("Action") : a.name;
                std::string unique = base;
                for (int n = 1; ; ++n) {
                    const int found = gameInput.find(unique);
                    if (found < 0 || found == i) break;
                    unique = base + "_" + std::to_string(n);
                }
                a.name = unique;
            }

            // un bouton par liaison :clic = capturer, clic droit = effacer
            auto bindButton = [&](const char* id, int slot, const std::string& text, int& field) {
                ImGui::TableNextColumn();
                const bool waiting = gameRebindAction == i && gameRebindSlot == slot;
                const std::string label = (waiting ? std::string("...") : text) + id;
                if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) {
                    gameRebindAction = i;
                    gameRebindSlot = slot;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && field >= 0) {
                    pushUndo();
                    field = -1;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::tr("Click to assign, right-click to clear"));
            };
            bindButton("##key", SLOT_KEY, InputMap::keyName(b.key), b.key);
            bindButton("##alt", SLOT_ALT_KEY, InputMap::keyName(b.altKey), b.altKey);
            bindButton("##keymod", SLOT_KEY_MOD, InputMap::keyName(b.keyModifier), b.keyModifier);
            bindButton("##pad", SLOT_PAD_BUTTON, InputMap::padButtonName(b.padButton), b.padButton);

            // axe : choisi dans une liste, pas capture (un stick bouge tout seul)
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##axis", axisLabel(b))) {
                for (const auto& c : AXES) {
                    const bool selected = c.axis == b.padAxis && (c.axis < 0 || c.sign == b.axisSign);
                    if (ImGui::Selectable(Lang::tr(c.label), selected) && !selected) {
                        pushUndo();
                        b.padAxis = c.axis;
                        b.axisSign = c.sign;
                    }
                }
                ImGui::EndCombo();
            }

            // modificateur manette : bouton ou axe a maintenir en plus
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##padmod", padModLabel(b).c_str())) {
                if (ImGui::Selectable(Lang::tr("(none)"), !hasPadMod(b)) && hasPadMod(b)) {
                    pushUndo();
                    b.padModButton = -1;
                    b.padModAxis = -1;
                }
                for (int btn = 0; btn <= GLFW_GAMEPAD_BUTTON_LAST; ++btn) {
                    const bool selected = b.padModButton == btn;
                    if (ImGui::Selectable(InputMap::padButtonName(btn).c_str(), selected) && !selected) {
                        pushUndo();
                        b.padModButton = btn;
                        b.padModAxis = -1;
                    }
                }
                ImGui::Separator();
                for (const auto& c : AXES) {
                    if (c.axis < 0) continue;
                    const bool selected = b.padModButton < 0 && c.axis == b.padModAxis && c.sign == b.padModSign;
                    if (ImGui::Selectable(Lang::tr(c.label), selected) && !selected) {
                        pushUndo();
                        b.padModButton = -1;
                        b.padModAxis = c.axis;
                        b.padModSign = c.sign;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::tr("Hold this too (combination)"));

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X")) removeIndex = i;

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // modifications de la liste apres la boucle 
    if (removeIndex >= 0) {
        pushUndo();
        actions.erase(actions.begin() + removeIndex);
        gameRebindAction = -1;
    }

    if (ImGui::Button(Lang::tr("Add action"))) {
        pushUndo();
        GameAction a;
        a.name = "action";
        for (int n = 1; gameInput.find(a.name) >= 0; ++n) a.name = "action_" + std::to_string(n);
        actions.push_back(a);
    }
    ImGui::SameLine();
    if (ImGui::Button(Lang::tr("Reset to defaults"))) {
        pushUndo();
        gameInput.resetDefaults();
        gameRebindAction = -1;
    }

    ImGui::End();
}