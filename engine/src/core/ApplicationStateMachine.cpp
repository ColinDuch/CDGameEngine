#include <engine/core/Application.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>

#include <engine/scene/AnimStateMachineSerializer.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <string>

/*
--------------------------------------------------------------------------
Panneau StateMachine : edition de l asset de l objet selectionne
les modifications touchent l asset donc tout les objets qui le partagent
--------------------------------------------------------------------------
*/

namespace {
    // base, base_1, base_2, ... husqu a un nom libre
    template <typename Exists>
    std::string makeUnique(std::string base, const char* fallback, Exists exists) {
        if (base.empty()) base = fallback;
        if (!exists(base)) return base;
        for (int n = 1; ; ++n) {
            std::string candidate = base + "_" + std::to_string(n);
            if (!exists(candidate)) return candidate;
        }
    }

    const char* typeLabel(AnimParamType t) {
        switch (t) {
            case AnimParamType::Bool: return "Bool";
            case AnimParamType::Float: return "Float";
            case AnimParamType::Trigger: return "Trigger";
        }
        return "Bool";
    }

    const char* opLabel(AnimCondition::Op op) {
        switch (op) {
            case AnimCondition::Op::IsTrue: return Lang::tr("is true");
            case AnimCondition::Op::IsFalse: return Lang::tr("is false");
            case AnimCondition::Op::Greater: return ">";
            case AnimCondition::Op::Less: return "<";
        }
        return "?";
    }

    const ImVec4 COLOR_ERROR = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    const ImVec4 COLOR_CURRENT = ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
}

void Application::renameAnimMachine(AnimStateMachineAsset& sm, const std::string& wanted) {
    // unicite sur le nom de fichier : "a b" et "a_b" donneraient le meme fichier
    const std::string newName = makeUnique(wanted, "StateMachine", [&](const std::string& n) {
        for (const auto& m : animMachines) {
            if (m.get() != &sm && AnimStateMachineSerializer::fileNameFor(m->name) == AnimStateMachineSerializer::fileNameFor(n))
                return true;
        }
        return false;
    });
    if (newName == sm.name) return;

    // l ancien fichier n est efface qu au prochain save, comme une suppression
    pendingMachineDeletes.push_back(AnimStateMachineSerializer::fileNameFor(sm.name));
    sm.name= newName;
}

void Application::deleteAnimMachine(AnimStateMachineAsset* sm) {
    // les objets qui l utilisent n ont plus de machine
    for (auto& o : sceneObjects) {
        if (o->stateMachine.asset.get() == sm) {
            o->stateMachine.asset = nullptr;
            o-> stateMachine.reset();
        }
    }
    // le fichier n est efface qu prochain save : quitter sans enregistrer ne perd rien
    pendingMachineDeletes.push_back(AnimStateMachineSerializer::fileNameFor(sm->name));
    animMachines.erase(std::remove_if(animMachines.begin(), animMachines.end(), [sm](const auto& m) {return m.get() == sm;}), animMachines.end());
}

void Application::drawStateMachinePanel() {
    if (!showStateMachinePanel) return;
    ImGui::Begin(trWin("State machine", "statemachine").c_str(), &showStateMachinePanel, panelFlags);

    // le play vide la selection : on, garde le dernier objet edite pour le debug en direct
    if (selectedObjectIndex >= 0) smLastObject = selectedObjectIndex;
    const int objIndex = (selectedObjectIndex >= 0) ? selectedObjectIndex : (isPlaying ? smLastObject : -1);

    if (objIndex < 0 || objIndex >= static_cast<int>(sceneObjects.size())) {
        ImGui::TextDisabled("%s", Lang::tr("Select an object."));
        ImGui::End();
        return;
    }

    auto& obj = sceneObjects[objIndex];
    const int meshIdx= obj->meshIndex;
    if (meshIdx < 0 || meshIdx >= static_cast<int>(availableMeshes.size()) || !availableMeshes[meshIdx]->isSkinned()) {
        ImGui::TextDisabled("%s", Lang::tr("This object has no skeleton."));
        ImGui::End();
        return;
    }
    const auto& clips = availableMeshes[meshIdx]->getClips();
    AnimStateMachineInstance& inst = obj->stateMachine;

    // l instance ne voit l asset qu en const : on retrouve la version modifiable
    AnimStateMachineAsset* sm = nullptr;
    for (const auto& m : animMachines) {
        if (m == inst.asset) sm = m.get();
    }

    // choix
    ImGui::SetNextItemWidth(-80.0f);
    if (ImGui::BeginCombo("##machine", sm ? sm->name.c_str() : Lang::tr("(no machine)"))) {
        if (ImGui::Selectable(Lang::tr("(no machine)"), sm == nullptr)) {
            if (sm) pushUndo();
            inst.asset = nullptr;
            inst.reset();
        }
        for (const auto& m : animMachines) {
            if (ImGui::Selectable(m->name.c_str(), m.get() == sm)) {
                if (m.get() != sm) pushUndo();
                inst.asset = m;
                inst.reset();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(Lang::tr("New"), ImVec2(-1.0f, 0.0f))) {
        pushUndo();
        auto m= std::make_shared<AnimStateMachineAsset>();
        m->name = makeUnique("StateMachine", "StateMachine", [&](const std::string& n) {
            for (const auto& o : animMachines) {
                if (AnimStateMachineSerializer::fileNameFor(o->name) == AnimStateMachineSerializer::fileNameFor(n)) return true;
            }
            return false;
        });
        animMachines.push_back(m);
        inst.asset = m;
        inst.reset();
        sm = m.get();
    }

    if (!sm) {
        ImGui::TextDisabled("%s", Lang::tr("No machine. Create one with New."));
        ImGui::End();
        return;
    }

    // --- annuler / retablir ---
    // une edition est terminee quand plus aucun widget n est actif :
    // si le contenu a change depuis la copie, on empile l etat d avant
    if (!ImGui::IsAnyItemActive()) {
        if (smUndoRef != sm) {
            smUndoRef = sm;
            smUndoCopy = *sm;
        } else if (!sm->sameContent(smUndoCopy)) {
            Snapshot before = captureState();
            for (auto& ms : before.machines) {
                if (ms.asset.get() == sm) ms.content = smUndoCopy;
            }
            pushUndo(std::move(before));
            smUndoCopy = *sm;
        }
    }

    // le tampon garde la saisie : le renommage (et celui du fichier) se fait a la validation
    if (smRenameTarget != sm) {
        smRenameTarget = sm;
        smRenameBuf = sm->name;
    }
    ImGui::SetNextItemWidth(-80.0f);
    ImGui::InputText("##smname", & smRenameBuf);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        renameAnimMachine(*sm, smRenameBuf);
        smRenameBuf = sm->name;
    }
    ImGui::SameLine();
    if (ImGui::Button(Lang::tr("Remove"), ImVec2(-1.0f, 0.0f))) {
        pushUndo();
        deleteAnimMachine(sm);
        ImGui::End();
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::tr("Delete this machine (Ctrl+Z to undo)"));

    int users = 0;
    for (const auto& o : sceneObjects) {
        if (o->stateMachine.asset == inst.asset) ++users;
    }
    if (users > 1) ImGui::TextDisabled("%s", Lang::fmt("Shared by %d objects", users).c_str());

    const bool live = isPlaying && inst.values.size() == sm->params.size();

    // parametres
    if(ImGui::CollapsingHeader(Lang::tr("Parameters"), ImGuiTreeNodeFlags_DefaultOpen)) {
        int removeIndex = -1;
        const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;

        if (!sm->params.empty() && ImGui::BeginTable("##parmas", live ? 5 : 4, tf)) {
            ImGui::TableSetupColumn(Lang::tr("Name"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(Lang::tr("Type"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(Lang::tr("Default"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            if (live) ImGui::TableSetupColumn(Lang::tr("Value"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(sm->params.size()); ++i) {
                AnimParamDef& p = sm->params[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##name", &p.name);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    p.name = makeUnique(p.name, "param", [&](const std::string& n) {
                        int found = sm->findParam(n);
                        return found >= 0 && found != i;
                    });
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##type", typeLabel(p.type))) {
                    for (AnimParamType t : {AnimParamType::Bool, AnimParamType::Float, AnimParamType::Trigger }) {
                        if (ImGui::Selectable(typeLabel(t), p.type == t) && p.type != t) {
                            p.type = t;
                            p.defaultValue = 0.0f;
                            sm->touch();        // les valeurs des instances changent de sens
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableNextColumn();
                if (p.type == AnimParamType::Float) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::DragFloat("##def", &p.defaultValue, 0.01f);
                } else if (p.type == AnimParamType::Bool) {
                    bool b  = p.defaultValue != 0.0f;
                    if (ImGui::Checkbox("##def", &b)) p.defaultValue = b ? 1.0f : 0.0f;
                } else {
                    ImGui::TextDisabled("-");   // un trigger part toujours de faux
                } 

                if (live) {
                    ImGui::TableNextColumn();
                    const float v = inst.values[i];
                    if (p.type == AnimParamType::Float) ImGui::TextColored(COLOR_CURRENT, "%.2f", v);
                    else ImGui::TextColored(v != 0.0f ? COLOR_CURRENT : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), 
                        "%s", v != 0.0f ? "true" : "false");
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X")) removeIndex = i;

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeIndex >= 0) sm->removeParam(removeIndex);

        if (ImGui::Button(Lang::tr("Add parameter"))) {
            AnimParamDef p;
            p.name = makeUnique("param", "param", [&](const std::string& n) {return sm->findParam(n) >= 0;});
            p.type = AnimParamType::Float;
            sm->params.push_back(p);
            sm->touch();
        }
    }

    // etats
    if (ImGui::CollapsingHeader(Lang::tr("States"), ImGuiTreeNodeFlags_DefaultOpen)) {
        int removeIndex = -1;
        const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;

        if (!sm->states.empty() && ImGui::BeginTable("##states", 6, tf)) {
            ImGui::TableSetupColumn(Lang::tr("Entry"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(Lang::tr("Name"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn(Lang::tr("Clip"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(Lang::tr("Loop"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(Lang::tr("Speed"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(sm->states.size()); ++i) {
                AnimState& st = sm->states[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                // l etat joue en ce moment ressort en vert
                if (isPlaying && inst.current == i)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 0.25f, 0.45f)));

                ImGui::TableNextColumn();
                if (ImGui::RadioButton("##entry", sm->entry == i)) sm->entry = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::tr("Entry state"));

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##name", &st.name);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    st.name = makeUnique(st.name, "State", [&](const std::string& n) {
                        int found = sm->findState(n);
                        return found >= 0 && found != i;
                    });
                }

                ImGui::TableNextColumn();
                bool clipFound = false;
                for (const auto& c : clips) {
                    if (c.name == st.clipName) clipFound = true;
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (!clipFound) ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
                const char* preview = st.clipName.empty() ? Lang::tr("(none)") : st.clipName.c_str();
                const bool open = ImGui::BeginCombo("##clip", preview);
                if (!clipFound) ImGui::PopStyleColor();
                if (!clipFound && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::tr("Clip not found on this character"));
                if (open) {
                    for (const auto& c : clips) {
                        if (ImGui::Selectable(c.name.c_str(), c.name == st.clipName)) st.clipName = c.name;
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableNextColumn();
                ImGui::Checkbox("##loop", &st.loop);

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##speed", &st.speed, 0.01f, 0.0f, 10.0f, "x%.2f");

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X")) removeIndex = i;

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeIndex >= 0) sm->removeState(removeIndex);

        if (ImGui::Button(Lang::tr("Add state"))) {
            AnimState st;
            st.name = makeUnique("State", "State", [&](const std::string& n) { return sm->findState(n) >= 0; });
            if (!clips.empty()) st.clipName = clips[0].name;
            sm->states.push_back(st);
            sm->touch();
        }
    }

    // transitions
    if (ImGui::CollapsingHeader(Lang::tr("Transitions"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (sm->states.empty()) {
            ImGui::TextDisabled("%s", Lang::tr("Add at least one state first."));
        } else {
            ImGui::TextDisabled("%s", Lang::tr("Checked from top to bottom: the first valid one wins."));

            int removeIndex = -1;
            int moveFrom = -1, moveTo = -1;
            const int stateCount = static_cast<int>(sm->states.size());
            auto stateName = [&](int s) -> const char* {
                if (s < 0) return Lang::tr("Any state");
                return (s < stateCount) ? sm->states[s].name.c_str() : "?";
            };

            for (int t = 0; t < static_cast<int>(sm->transitions.size()); ++t) {
                AnimTransition& tr = sm->transitions[t];
                ImGui::PushID(t);

                // l ID apres ### reste fixe : renommer un etat ne referme pas le noeud
                const std::string label = std::string(stateName(tr.from)) + "  ->  " + stateName(tr.to) + "###tr";
                const bool open = ImGui::TreeNode(label.c_str());

                // boutons cales a droite de la ligne
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 75.0f);
                if (ImGui::SmallButton("^") && t > 0) { moveFrom = t; moveTo = t - 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && t + 1 < static_cast<int>(sm->transitions.size())) { moveFrom = t; moveTo = t + 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeIndex = t;

                if (open) {
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo(Lang::tr("From"), stateName(tr.from))) {
                        if (ImGui::Selectable(Lang::tr("Any state"), tr.from < 0)) tr.from = -1;
                        for (int s = 0; s < stateCount; ++s) {
                            if (ImGui::Selectable(sm->states[s].name.c_str(), tr.from == s)) tr.from = s;
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo(Lang::tr("To"), stateName(tr.to))) {
                        for (int s = 0; s < stateCount; ++s) {
                            if (ImGui::Selectable(sm->states[s].name.c_str(), tr.to == s)) tr.to = s;
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::DragFloat(Lang::tr("Fade"), &tr.fade, 0.01f, 0.0f, 5.0f, "%.2f s");
                    ImGui::Checkbox(Lang::tr("Wait for clip end"), &tr.waitEnd);

                    ImGui::TextUnformatted(Lang::tr("Conditions (all must be true)"));
                    if (tr.conditions.empty())
                        ImGui::TextDisabled("%s", Lang::tr("No condition: fires immediately."));

                    int removeCond = -1;
                    for (int c = 0; c < static_cast<int>(tr.conditions.size()); ++c) {
                        AnimCondition& cond = tr.conditions[c];
                        ImGui::PushID(c);

                        const bool valid = cond.param >= 0 && cond.param < static_cast<int>(sm->params.size());
                        const char* paramPreview = valid ? sm->params[cond.param].name.c_str() : Lang::tr("missing parameter");

                        ImGui::SetNextItemWidth(130.0f);
                        if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
                        const bool openParam = ImGui::BeginCombo("##param", paramPreview);
                        if (!valid) ImGui::PopStyleColor();
                        if (openParam) {
                            for (int p = 0; p < static_cast<int>(sm->params.size()); ++p) {
                                if (ImGui::Selectable(sm->params[p].name.c_str(), cond.param == p)) {
                                    cond.param = p;
                                    // operateur coherent avec le type choisi
                                    const bool isFloat = sm->params[p].type == AnimParamType::Float;
                                    const bool opIsFloat = cond.op == AnimCondition::Op::Greater || cond.op == AnimCondition::Op::Less;
                                    if (isFloat != opIsFloat) cond.op = isFloat ? AnimCondition::Op::Greater : AnimCondition::Op::IsTrue;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        const bool isFloat = valid && sm->params[cond.param].type == AnimParamType::Float;
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(80.0f);
                        if (ImGui::BeginCombo("##op", opLabel(cond.op))) {
                            if (isFloat) {
                                for (auto op : { AnimCondition::Op::Greater, AnimCondition::Op::Less })
                                    if (ImGui::Selectable(opLabel(op), cond.op == op)) cond.op = op;
                            } else {
                                for (auto op : { AnimCondition::Op::IsTrue, AnimCondition::Op::IsFalse })
                                    if (ImGui::Selectable(opLabel(op), cond.op == op)) cond.op = op;
                            }
                            ImGui::EndCombo();
                        }

                        if (isFloat) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(70.0f);
                            ImGui::DragFloat("##value", &cond.value, 0.01f);
                        }

                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) removeCond = c;

                        ImGui::PopID();
                    }
                    if (removeCond >= 0) tr.conditions.erase(tr.conditions.begin() + removeCond);

                    if (ImGui::SmallButton(Lang::tr("Add condition"))) {
                        AnimCondition cond;
                        if (!sm->params.empty()) {
                            cond.param = 0;
                            cond.op = sm->params[0].type == AnimParamType::Float ? AnimCondition::Op::Greater
                                                                                 : AnimCondition::Op::IsTrue;
                        }
                        tr.conditions.push_back(cond);
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            // modifications de la liste APRES la boucle : jamais pendant qu on la parcourt
            if (removeIndex >= 0) sm->transitions.erase(sm->transitions.begin() + removeIndex);
            else if (moveFrom >= 0) std::swap(sm->transitions[moveFrom], sm->transitions[moveTo]);

            if (ImGui::Button(Lang::tr("Add transition"))) {
                AnimTransition tr;
                tr.from = 0;
                tr.to = stateCount > 1 ? 1 : 0;
                sm->transitions.push_back(tr);
            }
        }
    }

    ImGui::End();
}