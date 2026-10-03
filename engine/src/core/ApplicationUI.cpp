#include <engine/core/Application.hpp>
#include <engine/core/Config.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>

#include <engine/renderer/UniformBufferObject.hpp>
#include <engine/renderer/GriGeometry.hpp>
#include <engine/renderer/CheckerTexture.hpp>
#include <engine/renderer/DefaultNormal.hpp>
#include <engine/renderer/Frustum.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <chrono>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <cmath>
#include <algorithm>
#include <cctype>

void Application::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        drawFileMenu();
        
        if (ImGui::BeginMenu(Lang::tr("Edit"))) {
            if (ImGui::MenuItem(Lang::tr("Undo"), "Ctrl+Z", false, !undoStack.empty())) undo();
            if (ImGui::MenuItem(Lang::tr("Redo"), "Ctrl+Y", false, !redoStack.empty())) redo();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (isPlaying) {
            if (ImGui::MenuItem(Lang::tr("Stop"))) stopPlay();
            ImGui::TextDisabled("%s", Lang::tr("  [PLAY MODE]"));
        } else {
            if (ImGui::MenuItem(Lang::tr("Play"))) startPlay();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(Lang::tr("Scene"), nullptr, editorMode == EditorMode::Scene))
            setEditorMode(EditorMode::Scene);
        if (ImGui::MenuItem(Lang::tr("Material editor"), nullptr, editorMode == EditorMode::Material))
            setEditorMode(EditorMode::Material);
        if (ImGui::MenuItem(Lang::tr("Animation editor"), nullptr, editorMode == EditorMode::Animation))
            setEditorMode(EditorMode::Animation); 

        if (ImGui::BeginMenu(Lang::tr("Options"))) {

            if (ImGui::BeginMenu(Lang::tr("Windows"))) {
                ImGui::MenuItem(Lang::tr("Assets"), nullptr, &showAssetsPanel);
                ImGui::MenuItem(Lang::tr("Hierarchy"), nullptr, &showHierarchyPanel);
                ImGui::MenuItem(Lang::tr("Inspector"), nullptr, &showInspectorPanel);
                ImGui::MenuItem(Lang::tr("Lighting"), nullptr, &showLightPanel);
                ImGui::MenuItem(Lang::tr("Sun gizmo"), nullptr, &showSunGizmo);
                ImGui::MenuItem(Lang::tr("Camera"), nullptr, &showCameraPanel);
                ImGui::MenuItem(Lang::tr("FPS counter"), nullptr, &showFpsCounter);
                ImGui::MenuItem(Lang::tr("World axes"), nullptr, &showAxisGizmo);
                ImGui::MenuItem(Lang::tr("Script console"), nullptr, &showScriptConsole);
                ImGui::MenuItem(Lang::tr("Animation"), nullptr, &showAnimationPanel);
                ImGui::MenuItem(Lang::tr("Materials"), nullptr, &showMaterialPanel);
                ImGui::MenuItem(Lang::tr("Controls"), nullptr, &showInputPanel);
                ImGui::MenuItem(Lang::tr("Settings"), nullptr, &showSettingsPanel);
                ImGui::MenuItem(Lang::tr("State machine"), nullptr, &showStateMachinePanel);
                ImGui::EndMenu();
            }

            ImGui::MenuItem(Lang::tr("Lock the interface"), nullptr, &uiLocked);

            ImGui::Separator();

            if (ImGui::BeginMenu(Lang::tr("Theme"))) {
                
                if (ImGui::MenuItem(Lang::tr("Dark"), nullptr, currentTheme == 0)) {
                    currentTheme = 0;
                    ImGui::StyleColorsDark();
                }
                if (ImGui::MenuItem(Lang::tr("Light"), nullptr, currentTheme == 1)) {
                    currentTheme = 1;
                    ImGui::StyleColorsLight();
                }
                if (ImGui::MenuItem(Lang::tr("Classic"), nullptr, currentTheme == 2)) {
                    currentTheme = 2;
                    ImGui::StyleColorsClassic();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(Lang::tr("Language"))) {
                if (ImGui::MenuItem("English", nullptr, languageIndex == 0)) {
                    languageIndex = 0;
                    Lang::load("");
                    savePrefs();
                }
                if (ImGui::MenuItem("Français", nullptr, languageIndex == 1)) {
                    languageIndex = 1;
                    Lang::load("lang/fr.txt");
                    savePrefs();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
        // --- Popup de confirmation d'ouverture ---
    if (requestOpenConfirm) {
        ImGui::OpenPopup(trWin("Open a scene?", "openscene").c_str());
        requestOpenConfirm = false;
    }

    if (ImGui::BeginPopupModal(trWin("Open a scene?", "openscene").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", Lang::tr("Opening a scene will replace the current one."));
        ImGui::Text("%s", Lang::tr("Unsaved changes will be lost."));
        ImGui::Separator();

        if (ImGui::Button(Lang::tr("Save then open"))) {
            saveScene();
            loadScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Open without saving"))) {
            loadScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Cancel"))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::drawAssetsPanel() {
    if (!showAssetsPanel) return;

    ImGui::Begin(trWin("Assets", "assets").c_str(), &showAssetsPanel, panelFlags);

    if (meshNames.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           Lang::tr("No model loaded — check the log."));
        ImGui::End();
        return;
    }

    // L'index peut survivre a un echec de chargement : on le ramene dans les bornes.
    if (selectedMeshIndex < 0 || selectedMeshIndex >= static_cast<int>(meshNames.size())) {
        selectedMeshIndex = 0;
    }

    if (ImGui::BeginCombo(Lang::tr("Model"), meshNames[selectedMeshIndex].c_str())) {
        for (size_t i = 0; i < meshNames.size(); i++) {
            bool isSelected = (selectedMeshIndex == static_cast<int>(i));
            if (ImGui::Selectable(meshNames[i].c_str(), isSelected)) {
                selectedMeshIndex = static_cast<int>(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button(Lang::tr("Add object"))) {
        auto newObject = std::make_shared<SceneObject>(availableMeshes[selectedMeshIndex]);
        newObject->position = glm::vec3(static_cast<float>(sceneObjects.size()) * 2.0f, 0.0f, 0.0f);
        newObject->meshIndex = selectedMeshIndex;
        newObject->name = meshNames[selectedMeshIndex] + " " + std::to_string(sceneObjects.size());
        newObject->metallic  = availableMeshes[newObject->meshIndex]->getMetallic();
        newObject->roughness = availableMeshes[newObject->meshIndex]->getRoughness();
        // Les primitives de blockout sont en projection monde par defaut.
        newObject->worldUvScale = availableMeshes[selectedMeshIndex]->isSkinned() ? 0.0f : 1.0f;
        
        sceneObjects.push_back(newObject);
        commandBuffer->setSceneObjects(sceneObjects);
    }

    ImGui::End();
}

void Application::drawObjectNode(int i) {
    const int n = static_cast<int>(sceneObjects.size());

    bool hasChild = false;
    for (int k = 0; k < n; ++k) {
        if (sceneObjects[k]->parent == i) { hasChild = true; break; }
    }

    const bool sel    = isSelected(i);
    const bool active = (selectedObjectIndex == i);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (sel) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChild) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const std::string& objName = sceneObjects[i]->name;
    std::string display = objName.empty() ? ("Object " + std::to_string(i)) : objName;

    // Le fond de ImGuiTreeNodeFlags_Selected depend du theme et peut etre
    // invisible : on force en plus une couleur de TEXTE et un fond lisible.
    int pushed = 0;
    if (sel) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(1.00f, 0.78f, 0.30f, 1.0f)    // objet ACTIF
                   : ImVec4(1.00f, 0.62f, 0.20f, 1.0f));  // autres selectionnes
        ImGui::PushStyleColor(ImGuiCol_Header,       ImVec4(0.35f, 0.26f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.45f, 0.33f, 0.10f, 1.0f));
        pushed = 3;
    }

    bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(i)),
                                  flags, "%s", display.c_str());

    if (pushed) ImGui::PopStyleColor(pushed);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        selectObject(i, io.KeyCtrl, io.KeyShift);
    }

    // Source : on tire l'objet.
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &i, sizeof(int));
        ImGui::TextUnformatted(display.c_str());
        ImGui::EndDragDropSource();
    }
    // Cible : on le lache sur un autre pour le parenter.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            setParent(*static_cast<const int*>(pl->Data), i);
        }
        ImGui::EndDragDropTarget();
    }

    if (open && hasChild) {
        for (int k = 0; k < static_cast<int>(sceneObjects.size()); ++k) {
            if (sceneObjects[k]->parent == i) drawObjectNode(k);
        }
        ImGui::TreePop();
    }
}

void Application::drawHierarchyPanel() {
    if (!showHierarchyPanel) return;

    ImGui::Begin(trWin("Hierarchy", "hierarchy").c_str(), &showHierarchyPanel, panelFlags);

    if (ImGui::Selectable(Lang::tr("Terrain (ground)"), terrainSelected)) {
        terrainSelected = true;
        selectedObjectIndex = -1;
        selectedObjects.clear();
    }

    if (ImGui::Selectable(Lang::tr("Sun (light)"), sunSelected)) {
        sunSelected = true;
        terrainSelected = false;
        selectedObjectIndex = -1;
        selectedObjects.clear();
    }

    ImGui::Separator();

    if (ImGui::Button(Lang::tr("Deselect"))) {
        selectedObjectIndex = -1;
        terrainSelected = false;
        sunSelected = false;
    }
    ImGui::Separator();

    ImGui::Text(Lang::tr("Objects (%zu):"), sceneObjects.size());

    for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
        if (sceneObjects[i]->parent < 0) drawObjectNode(i);
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsAnyItemHovered()) {
        selectedObjects.clear();
        selectedObjectIndex = -1;
    }

    // Zone de depot "racine" : lacher un objet ici le detache de son parent.
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            setParent(*static_cast<const int*>(pl->Data), -1);
        }
        ImGui::EndDragDropTarget();
    }

    if (!sceneMessage.empty() || !exportMessage.empty()) {
        ImGui::Separator();
        if (!sceneMessage.empty())  ImGui::TextWrapped("%s", sceneMessage.c_str());
        if (!exportMessage.empty()) ImGui::TextWrapped("%s", exportMessage.c_str());
    }

    ImGui::End();
}

void Application::drawSettingsPanel() {
    if (!showSettingsPanel) return;

    ImGui::Begin(trWin("Settings", "settings").c_str(), &showSettingsPanel, panelFlags);

    ImGui::Checkbox(Lang::tr("Orbit around selection"), &orbitAroundSelection);

    ImGui::Checkbox(Lang::tr("Frustum culling"), &frustumCullingEnabled);
    ImGui::TextDisabled(Lang::tr("Culled: %d / %d"), culledCount,
                        static_cast<int>(sceneObjects.size()));

    if (ImGui::CollapsingHeader(Lang::tr("Advanced colors"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ShowStyleEditor();
    }

    if (ImGui::CollapsingHeader(Lang::tr("Snap / Alignment (Shift)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("%s", Lang::tr("Hold Shift while manipulating."));
        ImGui::TextDisabled("%s", Lang::tr("Ctrl+click a slider to type a value."));
        ImGui::SliderFloat(Lang::tr("Translation (tiles)"), &snapTranslate, 0.1f, 20.0f, "%.2f");
        ImGui::SliderFloat(Lang::tr("Rotation (degrees)"), &snapRotate, 1.0f, 180.0f, "%.0f");
        ImGui::SliderFloat(Lang::tr("Scale (step)"), &snapScale, 0.05f, 5.0f, "%.2f");
    }

    ImGui::End();
}

void Application::drawInspectorPanel() {
    if (!showInspectorPanel) return;

    ImGui::Begin(trWin("Inspector", "inspector").c_str(), &showInspectorPanel, panelFlags);

    if (selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(sceneObjects.size())) {
        auto& selected = sceneObjects[selectedObjectIndex];

        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->name.c_str());
        if (ImGui::InputText(Lang::tr("Name"), nameBuf, sizeof(nameBuf))) {
            selected->name = nameBuf;
        }
        if (ImGui::IsItemActivated()) pushUndo();
        ImGui::Separator();

        ImGui::TextDisabled("materialIndex = %d  |  parent = %d",
                            selected->materialIndex, selected->parent);

        ImGui::Text("%s", Lang::tr("Manipulation:"));
        if (ImGui::RadioButton(Lang::tr("Translation"), gizmoOperation == 0)) gizmoOperation = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton(Lang::tr("Rotation"), gizmoOperation == 1)) gizmoOperation = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton(Lang::tr("Scale"), gizmoOperation == 2)) gizmoOperation = 2;

        ImGui::Separator();
        ImGui::Text(Lang::tr("Transform of object %d"), selectedObjectIndex);

        ImGui::SliderFloat3(Lang::tr("Position"), &selected->position.x, -10.0f, 10.0f);
        if (ImGui::IsItemActivated()) pushUndo();

        glm::vec3 euler = selected->getEulerDegrees();
        if (ImGui::SliderFloat3(Lang::tr("Rotation"), &euler.x, -180.0f, 180.0f)) {
            selected->setEulerDegrees(euler);
        }
        if (ImGui::IsItemActivated()) pushUndo();

        ImGui::SliderFloat3(Lang::tr("Scale"), &selected->scale.x, 0.1f, 3.0f);
        if (ImGui::IsItemActivated()) pushUndo();

        ImGui::Separator();

        bool worldUV = selected->worldUvScale > 0.0f;
        if (ImGui::Checkbox(Lang::tr("World-space texture (blockout)"), &worldUV)) {
            pushUndo();
            selected->worldUvScale = worldUV ? 1.0f : 0.0f;
        }
        if (worldUV) {
            ImGui::DragFloat(Lang::tr("Tile size"), &selected->worldUvScale,
                             0.05f, 0.05f, 50.0f, "%.2f");
            if (ImGui::IsItemActivated()) pushUndo();
        }

        // ---- Script ----
        ImGui::Separator();
        ImGui::Text("%s", Lang::tr("Script"));
        if (selected->scriptPath.empty()) ImGui::TextDisabled("%s", Lang::tr("(none)"));
        else                              ImGui::TextWrapped("%s", selected->scriptPath.c_str());

        if (ImGui::Button(Lang::tr("Create a script for this object"))) {
            if (currentProjectDir.empty()) {
                sceneMessage = Lang::tr("Open or create a project first.");
            } else {
                std::string safe = selected->name.empty() ? "object" : selected->name;
                for (char& ch : safe) {
                    if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '_';
                }
                std::string rel ="code/" + safe + ".lua";
                std::filesystem::path p = std::filesystem::path(currentProjectDir) / rel;
                std::filesystem::create_directories(p.parent_path());
                if (!std::filesystem::exists(p)) {
                    std::ofstream f(p);
                    f << "-- " << Lang::tr("Script attached to an object. 'self' is the index of this object.") << "\n"
                      << "-- " << Lang::tr("start(): once on play. update(dt): every frame.") << "\n"
                      << "--\n"
                      << "-- " << Lang::tr("Engine API: log, count,") << "\n"
                         "--    getPosition(self) -> x, y, z        setPostion(self, x, y, z)\n"
                      << "--    getRotation(self) -> rx, ry, rz     setRotation(self, rx, ry, rz) -- "
                      << Lang::tr("degrees, Z-up") << "\n"
                         "--    isKeyDown(\"W\") / (\"up\") / (\"space\")\n"
                         "--    setBool(self, name, v)  setFloat(self, name, v) setTrigger(self, name)\n"
                      << "--    getAnimState(self) -> " << Lang::tr("name of the current state") << "\n\n"
                         "function start()\n"
                         "end\n\n"
                         "function update(dt)\n"
                         "end\n";
                }
                selected->scriptPath = rel;
                sceneMessage = Lang::fmt("Script assigned: %s", rel.c_str());
            }
        }

        if (ImGui::Button(Lang::tr("Link a script..."))) {
            if (currentProjectDir.empty()) {
                sceneMessage = Lang::tr("Open or create a project first.");
            } else {
                IGFD::FileDialogConfig config;
                config.path = (std::filesystem::path(currentProjectDir) / "code").string();
                ImGuiFileDialog::Instance()->OpenDialog("LinkScriptDlg", Lang::tr("Link a script..."), ".lua", config);
            }
        }

        if (!selected->scriptPath.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(Lang::tr("Remove"))) selected->scriptPath.clear();
        }

        // ---- Suppression (un seul bouton) ----
        ImGui::Separator();
        if (ImGui::Button(Lang::tr("Delete this object"))) {
            if (!isSelected(selectedObjectIndex)) selectedObjects.assign(1, selectedObjectIndex);
            deleteSelection();
        }

    } else if (terrainSelected) {
        ImGui::Text("%s", Lang::tr("Terrain (ground)"));
        ImGui::SliderFloat3(Lang::tr("Position"), &terrain->position.x, -100.0f, 100.0f);
        if (ImGui::IsItemActivated()) pushUndo();
        glm::vec3 eulerDeg = terrain->getEulerDegrees();
        if (ImGui::SliderFloat3(Lang::tr("Rotation"), &eulerDeg.x, -180.0f, 180.0f)) terrain->setEulerDegrees(eulerDeg);
        if (ImGui::IsItemActivated()) pushUndo();
        ImGui::SliderFloat3(Lang::tr("Scale"), &terrain->scale.x, 0.1f, 10.0f);
        if (ImGui::IsItemActivated()) pushUndo();
    } else {
        ImGui::TextDisabled("%s", Lang::tr("No object selected"));
    }

    ImGui::End();
}

void Application::drawMaterialPanel() {
    if (!showMaterialPanel) return;

    ImGui::Begin(trWin("Materials", "materials").c_str(), &showMaterialPanel, panelFlags);

    // --- Bibliotheque ---
    ImGui::InputText("##matname", materialNameBuf, sizeof(materialNameBuf));
    ImGui::SameLine();
    if (ImGui::Button(Lang::tr("New"))) {
        pushUndo();
        selectedMaterial = createMaterial(materialNameBuf);
    }

    ImGui::BeginChild("##matlist", ImVec2(0, 110), true);
    for (size_t i = 0; i < materials.size(); ++i) {
        std::string label = materials[i]->getName() + "##m" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), selectedMaterial == static_cast<int>(i))) {
            selectedMaterial = static_cast<int>(i);
        }
    }
    ImGui::EndChild();

    if (selectedMaterial < 0 || selectedMaterial >= static_cast<int>(materials.size())) {
        ImGui::TextDisabled("%s", Lang::tr("Select or create a material."));
        ImGui::End();
        return;
    }

    MaterialAsset& m = *materials[selectedMaterial];
    MaterialParams& p = m.params();
    bool changed = false;

    ImGui::Separator();

    // --- Assignation a l'objet selectionne ---
    // --- Objets utilisant ce materiau (cochable, marche dans les deux modes) ---
    ImGui::Text("%s", Lang::tr("Applied to"));

    ImGui::BeginChild("##matobjs", ImVec2(0, 130), true);
    if (sceneObjects.empty()) {
        ImGui::TextDisabled("%s", Lang::tr("No object in the scene."));
    }
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        int matIdx = sceneObjects[i]->materialIndex;
        bool uses = (matIdx == selectedMaterial);

        const std::string& objName = sceneObjects[i]->name;
        std::string lbl = (objName.empty() ? ("Object " + std::to_string(i)) : objName)
                        + "##mo" + std::to_string(i);

        if (ImGui::Checkbox(lbl.c_str(), &uses)) {
            pushUndo();
            sceneObjects[i]->materialIndex = uses ? selectedMaterial : -1;
        }
        // Signale l'objet qui porte DEJA un autre materiau.
        if (!uses && matIdx >= 0 && matIdx < static_cast<int>(materials.size())) {
            ImGui::SameLine();
            ImGui::TextDisabled("(-> %s)", materials[matIdx]->getName().c_str());
        }
    }
    ImGui::EndChild();

    if (ImGui::Button(Lang::tr("Apply to all"))) ImGui::OpenPopup("##applyall");
    if (ImGui::BeginPopupModal("##applyall", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(Lang::tr("Assign this material to all %d objects?"),
                    static_cast<int>(sceneObjects.size()));
        if (ImGui::Button(Lang::tr("Confirm"))) {
            pushUndo();
            for (auto& o : sceneObjects) o->materialIndex = selectedMaterial;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Lang::tr("Remove from all"))) {
        pushUndo();
        for (auto& o : sceneObjects)
            if (o->materialIndex == selectedMaterial) o->materialIndex = -1;
    }

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Factors"));

    changed |= ImGui::ColorEdit3(Lang::tr("Base color"), &p.baseColor.x);
    if (ImGui::IsItemActivated()) pushUndo();
    changed |= ImGui::SliderFloat(Lang::tr("Opacity"), &p.baseColor.a, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat(Lang::tr("Metallic"), &p.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat(Lang::tr("Roughness"), &p.roughness, 0.04f, 1.0f);
    changed |= ImGui::SliderFloat(Lang::tr("AO strength"), &p.aoStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat(Lang::tr("Normal strength"), &p.normalStrength, 0.0f, 3.0f);
    changed |= ImGui::ColorEdit3(Lang::tr("Emissive"), &p.emissive.x);
    changed |= ImGui::SliderFloat(Lang::tr("Emissive intensity"), &p.emissive.a, 0.0f, 10.0f);
    changed |= ImGui::SliderFloat(Lang::tr("Height scale"), &p.heightScale, 0.0f, 0.3f);
    changed |= ImGui::SliderFloat(Lang::tr("UV tiling"), &p.uvTiling, 0.1f, 50.0f);

    if (changed) m.uploadParams();

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Texture maps"));

    for (int i = 0; i < MAP_COUNT; ++i) {
        ImGui::PushID(i);
        ImGui::Text("%s", Lang::tr(MapSlotNames[i]));
        ImGui::SameLine(200);
        if (ImGui::Button(Lang::tr("Load..."))) {
            mapToLoad = i;
            IGFD::FileDialogConfig config;
            config.path = "assets";
            ImGuiFileDialog::Instance()->OpenDialog("LoadMapDlg",
                Lang::tr("Choose a texture"), ".png,.jpg,.jpeg,.tga,.bmp", config);
        }
        if (m.hasMap(i)) {
            ImGui::SameLine();
            if (ImGui::Button(Lang::tr("Clear"))) m.clearMap(i);
            ImGui::TextDisabled("   %s", m.getMapPath(i).c_str());
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void Application::buildUI() {
    updateWorldMatrices();
    imguiLayer->beginFrame();

    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    if (uiLocked) {
        dockFlags |= ImGuiDockNodeFlags_NoUndocking
                   | ImGuiDockNodeFlags_NoDockingSplit
                   | ImGuiDockNodeFlags_NoResize;
    }

    // Un ID de dockspace PAR MODE : chaque mode garde sa propre disposition.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);

    ImGuiIO& undoIo = ImGui::GetIO();
    if (undoIo.KeyCtrl && !undoIo.WantTextInput && !isPlaying) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { undoIo.KeyShift ? redo() : undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection();
    }

    if (!undoIo.WantTextInput && !isPlaying && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        deleteSelection();
    }

    drawMenuBar();

    if (editorMode == EditorMode::Scene) {
        drawAssetsPanel();
        drawHierarchyPanel();
        drawInspectorPanel();
        drawLightPanel();
        drawTerrainPanel();
        drawCameraPanel();
        drawScriptConsole();
        updateBrush();
        drawSunGizmo();
        drawGizmo();
        drawInputPanel();
        drawSettingsPanel();
    } else if (editorMode == EditorMode::Animation) {
        drawHierarchyPanel();
        drawAnimationPanel();
        drawTimelinePanel();
        drawCameraPanel();
        drawStateMachinePanel();

    } else {
        drawMaterialPanel();
        drawCameraPanel();
    }

    // Etat transmis au rendu, chaque frame et dans les deux modes.
    commandBuffer->setTerrainModel(terrain->getModelMatrix());
    
    //zone d'ombre qui suit le reagard 
    glm::mat4 camView = camera->getViewMatrix();
    glm::vec3 camFwd = -glm::vec3(camView[0][2], camView[1][2], camView[2][2]);
    glm::vec3 camPos = camera->getPosition();

    float effRadius = shadowRadius;
    if(terrain && shadowAutoRadius) {
        float ground = terrain->heightAtWorld(camPos.x, camPos.y);
        float alt = std::max(0.0f, camPos.z - ground);
        effRadius = std::clamp(shadowRadius + alt * 1.2f, shadowRadius, 1500.0f);
    }

    glm::vec3 shadowCenter = camPos + camFwd * (effRadius * 0.6f);
    if (terrain) {
        shadowCenter.z = terrain->heightAtWorld(shadowCenter.x, shadowCenter.y);
    }

    //ancrage sur la grille des texels
    {
        glm::vec3 sdir = glm::normalize(lightDirection);
        if (glm::length(lightDirection) < 0.0001f) sdir = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 sup = (std::abs(sdir.z) > 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                   : glm::vec3(0.0f, 0.0f, 1.0f);
        
        glm::mat4 lightView = glm::lookAt(shadowCenter -sdir * (shadowRadius * 2.0f),
                                         shadowCenter, sup);
        const float texel = 2.0f * shadowRadius /2048.0f; // taille de la shadow map

        glm::vec3 varLightSpacePose = glm::vec3(lightView * glm::vec4(shadowCenter,1.0f));
        varLightSpacePose.x = std::floor(varLightSpacePose.x / texel) * texel;
        varLightSpacePose.y = std::floor(varLightSpacePose.y / texel) * texel;
        shadowCenter = glm::vec3(glm::inverse(lightView) * glm::vec4(varLightSpacePose, 1.0f));

    }

    lightSpaceMatrix = ShadowMap::computeLightSpace(lightDirection, shadowCenter, effRadius);

    commandBuffer->setShadow(shadowMap.get(), lightSpaceMatrix);

    VkDescriptorSet layerSets[4];
    for (int i = 0; i < 4; ++i) {
        int layerMatIdx = terrain->layerMaterial[i];
        layerSets[i] = (layerMatIdx >= 0 && layerMatIdx < static_cast<int>(materials.size()))
                     ? materials[layerMatIdx]->getSet() : defaultMaterial->getSet();
    }

    commandBuffer->setTerrainLayerSets(layerSets);
    std::vector<int> opaqueOrder, transparentOrder;

    Frustum frustum = Frustum::fromMatrix(projectionMatrix * camera->getViewMatrix());
    culledCount = 0;

    for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
        // --- Frustum culling : boite locale transformee dans le monde. ---
        if (frustumCullingEnabled) {
            const auto& mesh = sceneObjects[i]->getMesh();
            if (mesh) {
                glm::mat4 varModel = sceneObjects[i]->getModelMatrix();
                glm::vec3 lo = mesh->getBoundsMin();
                glm::vec3 hi = mesh->getBoundsMax();

                glm::vec3 wmin( 1e30f), wmax(-1e30f);
                for (int c = 0; c < 8; ++c) {
                    glm::vec3 corner((c & 1) ? hi.x : lo.x,
                                     (c & 2) ? hi.y : lo.y,
                                     (c & 4) ? hi.z : lo.z);
                    glm::vec3 w = glm::vec3(varModel * glm::vec4(corner, 1.0f));
                    wmin = glm::min(wmin, w);
                    wmax = glm::max(wmax, w);
                }

                if (!frustum.intersectsAABB(wmin, wmax)) { ++culledCount; continue; }
            }
        }

        int layerMatIdx = sceneObjects[i]->materialIndex;
        bool transp = false;
        if (layerMatIdx >= 0 && layerMatIdx < static_cast<int>(materials.size())) {
            transp = (materials[layerMatIdx]->params().baseColor.a < 0.999f)
                  || materials[layerMatIdx]->hasMap(MAP_OPACITY);
        }
        (transp ? transparentOrder : opaqueOrder).push_back(i);
    }

    std::sort(transparentOrder.begin(), transparentOrder.end(), [&](int a, int b) {
        glm::vec3 da = sceneObjects[a]->position - camPos;
        glm::vec3 db = sceneObjects[b]->position - camPos;
        return glm::dot(da, da) > glm::dot(db, db);   // du plus loin au plus proche
    });

    // mode anim, perso seul pas de terrain
    if (editorMode == EditorMode::Animation) {
        opaqueOrder.clear();
        transparentOrder.clear();
        if (animTargetIndex >= 0 && animTargetIndex <static_cast<int>(sceneObjects.size()))
            opaqueOrder.push_back(animTargetIndex);
    }
    commandBuffer->setTerrainVisible(editorMode != EditorMode::Animation && terrainVisible);

    commandBuffer->setDrawOrder(opaqueOrder, transparentOrder);

    commandBuffer->setSkinned(skinnedPipeline->getHandle(), skinnedPipeline->getLayout());
    
    // Le ciel est a l'infini : on retire la TRANSLATION de la vue avant d'inverser.
    // Sinon, camera loin de l'origine (ou grand far plane) = annulation catastrophique
    // en float32 dans sky.frag, et le ciel se pixellise en gros carres.
    glm::mat4 skyView = glm::mat4(glm::mat3(camera->getViewMatrix()));
    commandBuffer->setSkyMatrix(glm::inverse(projectionMatrix * skyView));
    commandBuffer->setSkyExposure(exposure);

    drawStartupPanel();
    drawFileDialogs();
    drawFpsCounter();
    drawQuitPopup();
}

void Application::drawLightPanel() {
    if (!showLightPanel) return;

    ImGui::Begin(trWin("Lighting", "lighting").c_str(), &showLightPanel, panelFlags);

    ImGui::Text("%s", Lang::tr("Directional light"));
    ImGui::SliderFloat3(Lang::tr("Direction"), &lightDirection.x, -1.0f, 1.0f);
    ImGui::ColorEdit3(Lang::tr("Color"), &lightColor.x);
    ImGui::SliderFloat(Lang::tr("Intensity"), &lightIntensity, 0.0f, 3.0f);
    ImGui::SliderFloat(Lang::tr("Ambient"), &ambientStrength, 0.0f, 1.0f);
    ImGui::SliderFloat(Lang::tr("Shadow strength"), &shadowStrength, 0.0f, 1.0f);
    ImGui::SliderFloat(Lang::tr("Environment intensity"), &shininess, 0.0f, 3.0f);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Environment"));
    if (environment->getPath().empty()) ImGui::TextDisabled("%s", Lang::tr("(default sky)"));
    else                                ImGui::TextWrapped("%s", environment->getPath().c_str());
    if (ImGui::Button(Lang::tr("Load an environment..."))) {
        IGFD::FileDialogConfig config; config.path = "assets/textures";
        ImGuiFileDialog::Instance()->OpenDialog("LoadEnvDlg",
            Lang::tr("Choose an environment image (equirectangular)"), ".hdr,.jpg,.jpeg,.png", config);
    }
    if (ImGui::Checkbox(Lang::tr("Show sky"), &showSky)) {
        commandBuffer->setSky(skyRenderer.get(), showSky);
    }

    ImGui::End();
}

void Application::drawFpsCounter() {
    if (!showFpsCounter) return;

    ImGuiIO& io = ImGui::GetIO();

    // Positionne l'overlay en haut a droite.
    const float pad = 10.0f;
    ImVec2 workPos = ImGui::GetMainViewport()->WorkPos;
    ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
    ImVec2 windowPos = ImVec2(workPos.x + workSize.x - pad, workPos.y + pad);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);  // fond semi-transparent

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoDocking
                           | ImGuiWindowFlags_AlwaysAutoResize
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("FPS Overlay", nullptr, flags)) {
        ImGui::Text(Lang::tr("%.1f FPS"), io.Framerate);
        ImGui::Text(Lang::tr("%.2f ms/frame"), 1000.0f / io.Framerate);
    }
    ImGui::End();
}

void Application::drawScriptConsole() {
    if (!showScriptConsole) return;

    ImGui::Begin(trWin("Script console", "scriptconsole").c_str(), &showScriptConsole, panelFlags);

    ImGui::TextWrapped(Lang::tr("Camera: %s"), camDebug.c_str());
    ImGui::Separator();

    ImGui::TextWrapped(Lang::tr("Status: %s"),
                       scriptStatus.empty() ? Lang::tr("(not started yet)") : scriptStatus.c_str());
    ImGui::Text(Lang::tr("Active script: %s"),
                (scripts && scripts->hasScript()) ? Lang::tr("yes") : Lang::tr("no"));
    ImGui::Separator();

    if (!scripts || scripts->getLog().empty()) {
        ImGui::TextDisabled("%s", Lang::tr("(no message)"));
    } else {
        ImGui::TextUnformatted(scripts->getLog().c_str());
    }

    ImGui::Separator();
    if (ImGui::Button(Lang::tr("Clear")) && scripts) scripts->clearLog();

    ImGui::End();
}

void Application::drawInputPanel() {
    if (!showInputPanel) return;

    ImGui::Begin(trWin("Controls", "controls").c_str(), &showInputPanel, panelFlags);

    if (input.gamepadConnected()) {
        ImGui::TextDisabled(Lang::tr("Gamepad: %s"), input.gamepadName().c_str());
    } else {
        ImGui::TextDisabled("%s", Lang::tr("No gamepad detected"));
    }
    ImGui::Separator();

    // Capture de la nouvelle touche / du nouveau bouton.
    if (rebindAction >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           Lang::tr("Press a key or a button... (Escape to cancel)"));

        int k = InputMap::pollAnyKey(window->getHandle());
        int b = InputMap::pollAnyPadButton();

        if (k == GLFW_KEY_ESCAPE) {
            rebindAction = -1;
        } else if (rebindPad) {
            if (b >= 0) { input.binding(rebindAction).padButton = b; rebindAction = -1; savePrefs(); }
        } else {
            if (k >= 0) { input.binding(rebindAction).key = k; rebindAction = -1; savePrefs(); }
        }
        ImGui::Separator();
    }

    if (ImGui::BeginTable("##bindings", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn(Lang::tr("Action"),  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(Lang::tr("Key"),     ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(Lang::tr("Gamepad"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < ACTION_COUNT; ++i) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Lang::tr(input.binding(i).label));

            ImGui::TableSetColumnIndex(1);
            std::string kb = InputMap::keyName(input.binding(i).key) + "##k" + std::to_string(i);
            if (ImGui::Button(kb.c_str(), ImVec2(-1.0f, 0.0f))) { rebindAction = i; rebindPad = false; }

            ImGui::TableSetColumnIndex(2);
            std::string pb = InputMap::padButtonName(input.binding(i).padButton) + "##p" + std::to_string(i);
            if (ImGui::Button(pb.c_str(), ImVec2(-1.0f, 0.0f))) { rebindAction = i; rebindPad = true; }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button(Lang::tr("Reset to defaults"))) {
        input.resetDefaults();
        savePrefs();
    }

    ImGui::End();
}

void Application::drawCameraPanel() {
    if (!showCameraPanel) return;

    ImGui::Begin(trWin("Camera", "camera").c_str(), &showCameraPanel, panelFlags);

    ImGui::Text("%s", Lang::tr("Exposure"));
    ImGui::SliderFloat("##exposition", &exposure, 0.01f, 5.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemActivated()) pushUndo();

    ImGui::Separator();

    glm::vec3 p = camera->getPosition();
    ImGui::Text(Lang::tr("Position: %.2f  %.2f  %.2f"), p.x, p.y, p.z);
    ImGui::Text(Lang::tr("Yaw / Pitch: %.1f  %.1f"), camera->getYaw(), camera->getPitch());
    ImGui::Text(Lang::tr("Field of view: %.1f"), camera->getFov());
    ImGui::TextDisabled(Lang::tr("Orbit distance: %.2f"), orbitDistance);

    ImGui::Separator();
    ImGui::DragFloat(Lang::tr("Near plane"), &cameraNear, 0.01f, 0.01f, 100.0f, "%.3f");
    ImGui::DragFloat(Lang::tr("Far plane"),  &cameraFar,  10.0f,  10.0f, 200000.0f, "%.0f");
    ImGui::Checkbox(Lang::tr("Auto shadow radius"), &shadowAutoRadius);
    ImGui::DragFloat(Lang::tr("Shadow radius"), &shadowRadius, 1.0f, 1.0f, 2000.0f, "%.0f");
    float sp = camera->getMovementSpeed();
    if (ImGui::DragFloat(Lang::tr("Fly speed"), &sp, 0.5f, 0.1f, 2000.0f, "%.1f"))
        camera->setMovementSpeed(sp);

    ImGui::End();
}

void Application::drawQuitPopup() {
    if (askQuit) {
        ImGui::OpenPopup(trWin("Quit?", "quit").c_str());
        askQuit = false;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(trWin("Quit?", "quit").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", Lang::tr("Close the engine?"));
        if (currentProjectDir.empty()) {
            ImGui::TextDisabled("%s", Lang::tr("No project open: nothing will be saved."));
        } else {
            ImGui::TextDisabled(Lang::tr("Project: %s"), currentProjectDir.c_str());
        }
        ImGui::Separator();

        if (!currentProjectDir.empty()) {
            if (ImGui::Button(Lang::tr("Save and quit"), ImVec2(180, 0))) {
                saveScene();
                quitting = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button(Lang::tr("Quit without saving"), ImVec2(190, 0))) {
            quitting = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button(Lang::tr("Cancel"), ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::drawAnimationPanel() {
    if(!showAnimationPanel) return;
    ImGui::Begin(trWin("Animation", "animation").c_str(), &showAnimationPanel, panelFlags);

    if (selectedObjectIndex < 0 || selectedObjectIndex >= static_cast<int>(sceneObjects.size())) {
        ImGui::TextDisabled("%s", Lang::tr("Select an object."));
        ImGui::End();
        return;
    }

    auto& obj =sceneObjects[selectedObjectIndex];
    int meshIdx = obj->meshIndex;
    if (meshIdx < 0 || meshIdx >= static_cast<int>(availableMeshes.size()) || !availableMeshes[meshIdx]->isSkinned()) {
        ImGui::TextDisabled("%s", Lang::tr("This object has no skeleton."));
        ImGui::End();
        return;
    }

    auto& mesh = availableMeshes[meshIdx];
    const auto& clips = mesh->getClips();

    ImGui::TextDisabled("%d %s", static_cast<int>(mesh->getSkeleton().skinnedCount()),Lang::tr("bones"));

    if (ImGui::Button(Lang::tr("Load animation (FBX)..."))) {
        IGFD::FileDialogConfig config;
        config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("LoadAnimDlg", Lang::tr("Load animation (FBX)..."), ".fbx,.gltf,.glb,.dae", config);
    }

    ImGui::Separator();

    if (clips.empty()) {
        ImGui::TextDisabled("%s", Lang::tr("No clip. Load an animation file."));
    } else {
        
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##animfilter", Lang::tr("Search..."), animFilter, sizeof(animFilter));

        //comparaison insensible a la casse, sur un fragment qulconque du nom
        auto matches = [&](const std::string& name) {
            if (animFilter[0] == '\0') return true;
            std::string a = name, b = animFilter;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
            return a.find(b) != std::string::npos;
        };

        int nbVisibles = 0;
        for (int k = 0; k < static_cast<int>(clips.size()); ++k) {
            if (matches(clips[k].name)) ++nbVisibles;
        }
        ImGui::TextDisabled("%d / %d %s", nbVisibles, static_cast<int>(clips.size()), Lang::tr("clips"));

        if (ImGui::BeginListBox("##clipslist", ImVec2(-1.0f, 160.0f))) {
            if (matches(Lang::tr("Bind pose"))) {
                if (matches(Lang::tr("Bind pose"))) {
                    if (ImGui::Selectable(Lang::tr("Bind pose"), obj->animClip < 0)) {
                        obj->playClip(-1, 0.0f);
                    }
                }
            }
            for (int k = 0; k <static_cast<int>(clips.size()); ++k) {
                if (!matches(clips[k].name)) continue;

                std::string label = clips[k].name + "##clip" + std::to_string(k);
                if (ImGui::Selectable(label.c_str(), obj->animClip == k)) {
                    obj->playClip(k, obj->animBlendDuration);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%.2f s   %d %s", clips[k].duration, static_cast<int>(clips[k].channels.size()), Lang::tr("channels"));
                }
            }
            ImGui::EndListBox();
        }


        if (obj->animClip >= 0) {
            const AnimationClip& c= clips[obj->animClip];
            ImGui::Checkbox(Lang::tr("Playing"), &obj->animPlaying);
            ImGui::SameLine();
            ImGui::Checkbox(Lang::tr("Loop"), &obj->animLoop);
            ImGui::DragFloat(Lang::tr("Speed"), &obj->animSpeed, 0.01f, -3.0f, 3.0f, "%.2f");
            ImGui::SliderFloat(Lang::tr("Time"), &obj->animTime, 0.0f, c.duration > 0.0f ? c.duration : 1.0f, "%.2f s");
            ImGui::DragFloat(Lang::tr("Blend time"), &obj->animBlendDuration, 0.01f, 0.0f, 1.0f, "%.2f s");
            ImGui::TextDisabled("%s : %.2f s, %d %s", c.name.c_str(),c.duration, static_cast<int>(c.channels.size()), Lang::tr("channels"));
        }
    }

    ImGui::End();
}

void Application::drawTimelinePanel() {
    if (!showTimelinePanel) return;
    ImGui::Begin(trWin("Timeline", "timeline").c_str(), &showTimelinePanel, panelFlags);

    if (animTargetIndex < 0 || animTargetIndex >= static_cast<int>(sceneObjects.size())) {
        ImGui::TextDisabled("%s", Lang::tr("No animated object."));
        ImGui::End();
        return;
    }

    auto& obj = sceneObjects[animTargetIndex];
    const auto& mesh = availableMeshes[obj->meshIndex];
    const auto& clips = mesh->getClips();

    if (obj->animClip < 0 || obj->animClip >= static_cast<int>(clips.size())) {
        ImGui::TextDisabled("%s", Lang::tr("Selecte a clip in the Animation panel."));
        ImGui::End();
        return;
    }

    const AnimationClip& clip = clips[obj->animClip];
    float dur = (clip.duration > 0.0f) ? clip.duration : 1.0f;

    // transport
    if (ImGui::Button(obj->animPlaying ? "||" : ">", ImVec2(32, 0))) obj->animPlaying = !obj->animPlaying;
    ImGui::SameLine();
    if (ImGui::Button("|<", ImVec2(32, 0))) obj->animTime = 0.0f;
    ImGui::SameLine();
    ImGui::Checkbox(Lang::tr("Loop"), &obj->animLoop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat(Lang::tr("Speed"), &obj->animSpeed, 0.01f, -3.0f, 3.0f, "x%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f / %.2f s  (%d / %d)", obj->animTime, dur,
        static_cast<int>(obj->animTime * clip.fps), static_cast<int>(dur * clip.fps));
    
    // pistes 
    const float trackH = 46.0f;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    if (width < 40.0f) {ImGui::End(); return; }

    ImVec2 p1 = ImVec2(p0.x + width, p0.y + trackH);
    ImDrawList* dl =ImGui::GetWindowDrawList();

    dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 32, 255), 3.0f);
    dl->AddRect(p0, p1, IM_COL32(70, 70, 80, 255), 3.0f);

    //graduation, une par secondes, plus fines toutes les 0.1s si la place est disponnible
    float pxPerSec = width / dur;
    float step = (pxPerSec > 300.0f) ? 0.1f : 1.0f;
    for (float t = 0.0f; t <= dur + 1e-4f; t += step) {
        float x = p0.x + (t / dur) * width;
        bool major = (std::fmod(t, 1.0f) < 1e-4f);
        dl->AddLine(ImVec2(x, p1.y - (major ? 14.0f : 7.0f)), ImVec2(x, p1.y), IM_COL32(110, 110, 120, 255));
        if (major && pxPerSec > 40.0f) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f", t);
            dl->AddText(ImVec2(x +3.0f, p0.y + 3.0f), IM_COL32(150, 150, 160, 255), buf);
        }
    }

    // tete de lecture
    float headX = p0.x + (obj->animTime / dur) * width;
    dl->AddLine(ImVec2(headX - 6.0f, p0.y), ImVec2(headX, p1.y), IM_COL32(255, 190, 60, 255),2.0f);
    dl->AddTriangleFilled(ImVec2(headX - 6.0f, p0.y), ImVec2(headX + 6.0f, p0.y), ImVec2(headX, p0.y +9.0f), IM_COL32(255, 190, 60, 255));

    // marqueurs de notify
    auto& clipsMut = availableMeshes[obj->meshIndex]->getClipsMutable();
    AnimationClip& clipMut = clipsMut[obj->animClip];

    for (int n = 0; n < static_cast<int>(clipMut.notifies.size()); ++n) {
        float nx = p0.x + (clipMut.notifies[n].time / dur) * width;
        bool sel = (n == animSelectedNotify);
        ImU32 col = sel ? IM_COL32(255, 90, 90, 255) : IM_COL32(90, 200, 255, 255);

        dl->AddLine(ImVec2(nx, p0.y + 12.0f), ImVec2(nx, p1.y), col, sel ? 3.0f : 2.0f);
        dl->AddCircleFilled(ImVec2(nx, p0.y + 12.0f), 5.0f, col);
        dl->AddText(ImVec2(nx + 7.0f, p0.y + 16.0f), col, clipMut.notifies[n].name.c_str());
    }

    //zone cliquable
    ImGui::InvisibleButton("##track", ImVec2(width, trackH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    float mouseT = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f) * dur;
    //click droit pose un notify a l endroit de la souris 
    if ( ImGui::IsItemClicked(ImGuiMouseButton_Right) && animNotifyName[0] != '\0') {
        clipMut.notifies.push_back({ mouseT, std::string(animNotifyName) });
        animSelectedNotify = static_cast<int>(clipMut.notifies.size()) - 1;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        animSelectedNotify = -1;
        float best = 8.0f / width * dur;
        for (int n = 0; n < static_cast<int>(clipMut.notifies.size()); ++n) {
            float d = std::abs(clipMut.notifies[n].time - mouseT);
            if (d < best) {
                best = d;
                animSelectedNotify = n;
            }
        }
        animDraggingNotify =(animSelectedNotify >= 0);
        if (!animDraggingNotify) obj->animPlaying = false;
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (animDraggingNotify && animSelectedNotify >=0 && animSelectedNotify < static_cast<int>(clipMut.notifies.size())) {
            clipMut.notifies[animSelectedNotify].time = mouseT;
        } else {
            obj->animTime = mouseT;
        }
    }

    if (ImGui::IsItemDeactivated()) animDraggingNotify = false;

    ImGui::Separator();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText(Lang::tr("Notify name"), animNotifyName, sizeof(animNotifyName));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Lang::tr("Right-click the track to add"));

    if (animSelectedNotify >= 0 && animSelectedNotify < static_cast<int>(clipMut.notifies.size())) {
        AnimNotify& note = clipMut.notifies[animSelectedNotify];
        ImGui::SetNextItemWidth(140.0f);
        ImGui::DragFloat(Lang::tr("Notify time"), &note.time, 0.005f, 0.0f, dur, "%.3f s");
        ImGui::SameLine();
        ImGui::TextDisabled("%s %d", Lang::tr("frame"), static_cast<int>(note.time * clipMut.fps));
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Delete notify"))) {
            clipMut.notifies.erase(clipMut.notifies.begin() + animSelectedNotify);
            animSelectedNotify = -1;
        }
    }
    
    ImGui::End();
}