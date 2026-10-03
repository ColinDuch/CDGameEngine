#include <engine/core/Application.hpp>
#include <engine/core/Config.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>
#include <engine/scene/AnimStateMachineSerializer.hpp>

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

void Application::updateCamera(float deltaTime) {
    // Echap : tout deselectionner. Place ICI et pas dans updateEditorCamera,
    // sinon les gardes (souris sur un panneau, mode jeu, camera libre) le bloqueraient.
    bool escNow = input.isDown(ACTION_DESELECT);
    if (escNow && !deselectKeyWas && !ImGui::GetIO().WantTextInput) {
        selectedObjectIndex = -1;
        terrainSelected = false;
        sunSelected = false;
    }
    deselectKeyWas = escNow;

    // On lit les deltas une seule fois (les getters les remettent a zero).
    float dx    = static_cast<float>(window->getMouseDeltaX());
    float dy    = static_cast<float>(window->getMouseDeltaY());
    float wheel = static_cast<float>(window->getScrollDelta());

    if (window->isCursorLocked()) {
        // Mode vol libre (Tab) : inchange.
        camera->processKeyboard(input, deltaTime);
        camera->processMouseMovement(dx, dy);
        camera->processMouseScroll(wheel);

        // Le pivot reste devant la camera pour que l'orbite reprenne proprement.
        glm::mat4 varViewMtrx = camera->getViewMatrix();
        glm::vec3 varForward = -glm::vec3(varViewMtrx[0][2], varViewMtrx[1][2], varViewMtrx[2][2]);
        orbitTarget = camera->getPosition() + varForward * orbitDistance;
    } else {
        updateEditorCamera(dx, dy, wheel);
    }
}

void Application::drawGizmo() {
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    if (isPlaying) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 proj = projectionMatrix;
    proj[1][1] *= -1;

    drawAxisGizmo();

    // Cible : le terrain, ou l'objet selectionne.
    bool objSelected = (selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(sceneObjects.size()));
    if (!terrainSelected && !objSelected && !sunSelected) return;

    int activeId = sunSelected ? -3 : (terrainSelected ? -2 : selectedObjectIndex);
    if (!ImGuizmo::IsUsing() || activeId != gizmoActiveObject) {
        if (sunSelected) {
            // Repere dont l'axe -Z pointe dans la direction d'eclairage.
            glm::vec3 varLightDir = glm::normalize(lightDirection);
            if (glm::length(lightDirection) < 0.0001f) varLightDir = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 upRef = (std::abs(varLightDir.z) > 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
            glm::vec3 varRight = glm::normalize(glm::cross(upRef, varLightDir));
            glm::vec3 varUp = glm::cross(varLightDir, varRight);
            glm::mat4 basis(glm::mat3(varRight, varUp, -varLightDir));
            basis[3] = glm::vec4(sunGizmoPos, 1.0f);
            gizmoMatrix = basis;
        } else {
            gizmoMatrix = terrainSelected ? terrain->getModelMatrix()
                                          : sceneObjects[selectedObjectIndex]->getModelMatrix();
        }
        gizmoActiveObject = activeId;
    }

    if (!window->isCursorLocked() && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOperation = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOperation = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOperation = 2;
    }

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (gizmoOperation == 1) op = ImGuizmo::ROTATE;
    else if (gizmoOperation == 2) op = ImGuizmo::SCALE;

    bool useSnap = io.KeyShift && !window->isCursorLocked();
    float snapValue = snapTranslate;
    if (gizmoOperation == 1) snapValue = snapRotate;
    else if (gizmoOperation == 2) snapValue = snapScale;
    float snapValues[3] = { snapValue, snapValue, snapValue };

    ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::WORLD,
        glm::value_ptr(gizmoMatrix), nullptr, useSnap ? snapValues : nullptr);

    if (ImGuizmo::IsUsing() && !gizmoWasUsing) pushUndo();
    gizmoWasUsing = ImGuizmo::IsUsing();

    if (ImGuizmo::IsUsing()) {
        glm::vec3 c0(gizmoMatrix[0]), c1(gizmoMatrix[1]), c2(gizmoMatrix[2]);
        glm::vec3 varScale(glm::length(c0), glm::length(c1), glm::length(c2));
        glm::mat3 rot(c0 / varScale.x, c1 / varScale.y, c2 / varScale.z);

        glm::vec3 pos = glm::vec3(gizmoMatrix[3]);
        glm::quat ori = glm::normalize(glm::quat_cast(rot));

        if (sunSelected) {
            sunGizmoPos = glm::vec3(gizmoMatrix[3]);
            lightDirection = -glm::normalize(glm::vec3(gizmoMatrix[2]));   // -Z du repere
        } else if (terrainSelected) {
            terrain->position = pos;
            terrain->orientation = ori;
            terrain->scale = varScale;
        } else {
            auto& obj = sceneObjects[selectedObjectIndex];

            // ImGuizmo rend une matrice MONDE : on la ramene dans l'espace du parent.
            glm::mat4 local = gizmoMatrix;
            int parentIdx = obj->parent;
            if (parentIdx >= 0 && parentIdx < static_cast<int>(sceneObjects.size())) {
                local = glm::inverse(sceneObjects[parentIdx]->worldMatrix) * gizmoMatrix;
            }

            glm::vec3 l0(local[0]), l1(local[1]), l2(local[2]);
            glm::vec3 varLocalScale(glm::length(l0), glm::length(l1), glm::length(l2));
            glm::mat3 lrot(l0 / varLocalScale.x, l1 / varLocalScale.y, l2 / varLocalScale.z);

            obj->position = glm::vec3(local[3]);
            obj->orientation = glm::normalize(glm::quat_cast(lrot));
            obj->scale = varLocalScale;
        }
    }
}

void Application::updateWorldMatrices() {
    const int n = static_cast<int>(sceneObjects.size());
    std::vector<char> state(n, 0);   // 0 = a faire, 1 = en cours, 2 = fait

    std::function<void(int)> solve = [&](int i) {
        if (state[i] == 2) return;

        int parentIdx = sceneObjects[i]->parent;
        if (parentIdx < 0 || parentIdx >= n || parentIdx == i || state[i] == 1) {
            // Racine, index invalide, ou cycle detecte : on detache.
            if (parentIdx != -1 && (parentIdx < 0 || parentIdx >= n || parentIdx == i || state[i] == 1))
                sceneObjects[i]->parent = -1;
            sceneObjects[i]->worldMatrix = sceneObjects[i]->getLocalMatrix();
            state[i] = 2;
            return;
        }

        state[i] = 1;
        solve(parentIdx);
        sceneObjects[i]->worldMatrix = sceneObjects[parentIdx]->worldMatrix * sceneObjects[i]->getLocalMatrix();
        state[i] = 2;
    };

    for (int i = 0; i < n; ++i) solve(i);
}

void Application::setParent(int child, int newParent) {
    const int n = static_cast<int>(sceneObjects.size());
    if (child < 0 || child >= n) return;
    if (newParent == child) return;
    if (newParent >= n) return;

    // Interdit de parenter un objet a l'un de ses propres descendants.
    int guard = 0;
    for (int p = newParent; p >= 0 && guard < n + 1; p = sceneObjects[p]->parent, ++guard) {
        if (p == child) return;
    }

    pushUndo();

    // On memorise la position MONDE, puis on recalcule la locale pour la conserver.
    glm::mat4 world = sceneObjects[child]->worldMatrix;
    sceneObjects[child]->parent = newParent;

    glm::mat4 local = world;
    if (newParent >= 0) {
        local = glm::inverse(sceneObjects[newParent]->worldMatrix) * world;
    }

    glm::vec3 c0(local[0]), c1(local[1]), c2(local[2]);
    glm::vec3 varScale(glm::length(c0), glm::length(c1), glm::length(c2));

    sceneObjects[child]->position = glm::vec3(local[3]);
    sceneObjects[child]->orientation = glm::normalize(glm::quat_cast(glm::mat3(c0 / varScale.x, c1 / varScale.y, c2 / varScale.z)));
    sceneObjects[child]->scale = varScale;

    updateWorldMatrices();
}

bool Application::isSelected(int i) const {
    return std::find(selectedObjects.begin(), selectedObjects.end(), i) != selectedObjects.end();
}

void Application::selectObject(int i, bool additive, bool range) {
    const int n = static_cast<int>(sceneObjects.size());
    if (i < 0 || i >= n) return;

    if (range && selectedObjectIndex >= 0 && selectedObjectIndex < n) {
        // Plage sur les index bruts (pas l'ordre visuel de l'arbre).
        int varFirst = std::min(selectedObjectIndex, i);
        int varLast = std::max(selectedObjectIndex, i);
        selectedObjects.clear();
        for (int k = varFirst; k <= varLast; ++k) selectedObjects.push_back(k);
    } else if (additive) {
        auto it = std::find(selectedObjects.begin(), selectedObjects.end(), i);
        if (it != selectedObjects.end()) selectedObjects.erase(it);
        else                             selectedObjects.push_back(i);
    } else {
        selectedObjects.assign(1, i);
    }

    selectedObjectIndex = isSelected(i) ? i
                        : (selectedObjects.empty() ? -1 : selectedObjects.back());
    terrainSelected = false;
}

void Application::duplicateSelection() {
    if (selectedObjects.empty()) return;
    const int n = static_cast<int>(sceneObjects.size());

    // On duplique aussi tous les DESCENDANTS des objets selectionnes.
    std::vector<char> toCopy(n, 0);
    for (int idx : selectedObjects) {
        if (idx >= 0 && idx < n) toCopy[idx] = 1;
    }
    bool changed = true;
    while (changed) {                       // profondeur inconnue : on itere
        changed = false;
        for (int i = 0; i < n; ++i) {
            int parentIdx = sceneObjects[i]->parent;
            if (!toCopy[i] && parentIdx >= 0 && parentIdx < n && toCopy[parentIdx]) { toCopy[i] = 1; changed = true; }
        }
    }

    pushUndo();

    std::vector<int> mapOldNew(n, -1);
    std::vector<int> created;

    for (int i = 0; i < n; ++i) {
        if (!toCopy[i]) continue;
        const auto& src = sceneObjects[i];
        if (src->meshIndex < 0 || src->meshIndex >= static_cast<int>(availableMeshes.size()))
            continue;                       // objet sans mesh : pas encore gere

        auto copy = std::make_shared<SceneObject>(availableMeshes[src->meshIndex]);
        copy->position = src->position;
        copy->orientation = src->orientation;
        copy->scale = src->scale;
        copy->meshIndex = src->meshIndex;
        copy->materialIndex = src->materialIndex;
        copy->metallic = src->metallic;
        copy->roughness = src->roughness;
        copy->worldUvScale = src->worldUvScale;
        copy->scriptPath = src->scriptPath;
        copy->parent = src->parent;   // corrige juste apres
        copy->name = src->name + " copy";
        copy->animClip = src->animClip;
        copy->animSpeed = src->animSpeed;
        copy->animPlaying = src->animPlaying;
        copy->animLoop = src->animLoop;
        copy->stateMachine.asset = src->stateMachine.asset;     // la copie partage la machine

        sceneObjects.push_back(copy);
        mapOldNew[i] = static_cast<int>(sceneObjects.size()) - 1;
        created.push_back(mapOldNew[i]);
    }

    // Si le parent a ete copie lui aussi, la copie pointe sur LA COPIE du parent.
    for (int newIdx : created) {
        int parentIdx = sceneObjects[newIdx]->parent;
        if (parentIdx >= 0 && parentIdx < n && mapOldNew[parentIdx] >= 0) sceneObjects[newIdx]->parent = mapOldNew[parentIdx];
    }

    selectedObjects = created;
    selectedObjectIndex = created.empty() ? -1 : created.front();

    commandBuffer->setSceneObjects(sceneObjects);
    updateWorldMatrices();
}

void Application::deleteSelection() {
    if (selectedObjects.empty()) return;
    const int n = static_cast<int>(sceneObjects.size());

    // Supprimer un parent supprime ses descendants.
    std::vector<char> doomed(n, 0);
    for (int idx : selectedObjects) {
        if (idx >= 0 && idx < n) doomed[idx] = 1;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; ++i) {
            int parentIdx = sceneObjects[i]->parent;
            if (!doomed[i] && parentIdx >= 0 && parentIdx < n && doomed[parentIdx]) { doomed[i] = 1; changed = true; }
        }
    }

    pushUndo();

    // Table ancien -> nouveau : SANS elle, les parents pointent n'importe ou.
    std::vector<int> remap(n, -1);
    std::vector<std::shared_ptr<SceneObject>> kept;
    for (int i = 0; i < n; ++i) {
        if (doomed[i]) continue;
        remap[i] = static_cast<int>(kept.size());
        kept.push_back(sceneObjects[i]);
    }
    for (auto& o : kept) {
        o->parent = (o->parent >= 0 && o->parent < n) ? remap[o->parent] : -1;
    }

    sceneObjects.swap(kept);
    selectedObjects.clear();
    selectedObjectIndex = -1;

    commandBuffer->setSceneObjects(sceneObjects);
    updateWorldMatrices();
}

void Application::drawTerrainPanel() {
    ImGui::Begin(trWin("Terrain", "terrain").c_str());

    int sstride = terrain->getShadowStride();
    if (ImGui::SliderInt(Lang::tr("Shadow mesh stride"), &sstride, 1, 16)) {
        terrain->setShadowStride(sstride);
        commandBuffer->setTerrain(
            terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
            terrain->getIndexCount(), terrain->getLayerSet(),
            terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount()
        );
    }
    ImGui::TextDisabled(Lang::tr("Shadow triangles: %d"), terrain->getShadowIndexCount() / 3);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Brush"));
    ImGui::Checkbox(Lang::tr("Enable sculpt"), &sculptEnabled);

    ImGui::Text("%s", Lang::tr("Mode:"));
    ImGui::RadioButton(Lang::tr("Raise/Lower"), &brushMode, 0);
    ImGui::RadioButton(Lang::tr("Smooth"), &brushMode, 1);
    ImGui::RadioButton(Lang::tr("Flatten"), &brushMode, 2);

    ImGui::DragFloat(Lang::tr("Radius (units)"), &brushRadius, 0.1f, 0.1f, 200.0f);
    ImGui::DragFloat(Lang::tr("Strength"), &brushStrength, 0.1f, 0.1f, 100.0f);

    if (brushMode == 0)
        ImGui::TextDisabled("%s", Lang::tr("LMB: raise  |  RMB: lower"));
    else if (brushMode == 1)
        ImGui::TextDisabled("%s", Lang::tr("LMB: smooth"));
    else
        ImGui::TextDisabled("%s", Lang::tr("LMB: flatten (height of the first clicked point)"));
    ImGui::TextDisabled("%s", Lang::tr("Wheel: adjust radius"));

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Reference grid"));
    if (ImGui::Checkbox(Lang::tr("Show grid"), &gridVisible)) {
        commandBuffer->setGridVisible(gridVisible);
    }
    ImGui::DragInt(Lang::tr("Extent (tiles)"), &gridHalfSize, 1.0f, 1, 500);
    if (ImGui::IsItemDeactivatedAfterEdit()) regenerateGrid();
    ImGui::DragFloat(Lang::tr("Step (units)"), &gridStep, 0.1f, 0.1f, 50.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) regenerateGrid();

    ImGui::Separator();

    if (ImGui::Checkbox(Lang::tr("Show ground"), &terrainVisible)) {
        commandBuffer->setTerrainVisible(terrainVisible);
    }
    ImGui::Separator();

    ImGui::Text("%s", Lang::tr("Size (world units)"));
    ImGui::DragFloat(Lang::tr("Width (X)"), &terrainSizeX, 1.0f, 1.0f, 10000.0f);
    ImGui::DragFloat(Lang::tr("Depth (Y)"), &terrainSizeY, 1.0f, 1.0f, 10000.0f);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Resolution (vertices per axis)"));
    ImGui::DragInt(Lang::tr("Vertices X"), &terrainResX, 1.0f, 2, 1024);
    ImGui::DragInt(Lang::tr("Vertices Y"), &terrainResY, 1.0f, 2, 1024);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Texture"));
    if (ImGui::DragFloat(Lang::tr("Tiling (units)"), &terrainUvTiling, 0.1f, 0.1f, 200.0f)) {
        terrain->setUvTiling(terrainUvTiling);
    }
    if (ImGui::Button(Lang::tr("Load a texture..."))) {
        
        IGFD::FileDialogConfig config;
        config.path = "assets/textures";
        ImGuiFileDialog::Instance()->OpenDialog(
            "LoadTerrainTexDlg", Lang::tr("Choose a terrain texture"), ".png,.jpg,.jpeg", config
        );
    }

    if (ImGui::Button(Lang::tr("Checker"))) {
        commandBuffer->setTerrainRenderMode(5);
    }

    ImGui::Spacing();
    if (ImGui::Button(Lang::tr("Regenerate terrain"))) {
        terrain->rebuild(terrainResX, terrainResY, terrainSizeX, terrainSizeY);
        commandBuffer->setTerrain(
        terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
        terrain->getIndexCount(), terrain->getLayerSet(),terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount());
    }

    ImGui::TextDisabled(Lang::tr("%d x %d vertices, %d triangles"),
        terrain->getResX(), terrain->getResY(), terrain->getIndexCount() / 3);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Procedural generation (noise)"));
    ImGui::DragFloat(Lang::tr("Amplitude"),   &noiseParams.amplitude,   0.1f,  0.0f, 500.0f);
    ImGui::DragFloat(Lang::tr("Scale"),     &noiseParams.scale,       0.5f,  1.0f, 1000.0f);
    ImGui::DragInt  ("Octaves",     &noiseParams.octaves,     1,     1,    8);
    ImGui::DragFloat(Lang::tr("Persistence"), &noiseParams.persistence, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(Lang::tr("Lacunarity"),  &noiseParams.lacunarity,  0.01f, 1.0f, 4.0f);
    ImGui::DragInt  ("Seed",        &noiseParams.seed);

    if (ImGui::Button(Lang::tr("Random seed"))) {
        noiseParams.seed = rand();
        bool prevAdd = noiseParams.add;
        noiseParams.add = false;
        terrain->generateNoise(noiseParams);
        noiseParams.add = prevAdd;
    }

    if (ImGui::Button(Lang::tr("Randomize all"))) {
        // Helper : reel aleatoire entre a et b.
        auto rnd = [](float a, float b) {
            return a + (b - a) * (rand() / (float)RAND_MAX);
        };
        noiseParams.seed        = rand();
        noiseParams.amplitude   = rnd(5.0f, 40.0f);
        noiseParams.scale       = rnd(15.0f, 120.0f);
        noiseParams.octaves     = 3 + rand() % 4;      // 3 a 6
        noiseParams.persistence = rnd(0.35f, 0.65f);
        noiseParams.lacunarity  = rnd(1.8f, 2.5f);

        bool prevAdd = noiseParams.add;
        noiseParams.add = false;                       // toujours un terrain frais
        terrain->generateNoise(noiseParams);
        noiseParams.add = prevAdd;
    }

    ImGui::Checkbox (Lang::tr("Add to the current terrain"), &noiseParams.add);

    if (ImGui::Button(Lang::tr("Generate terrain (noise)"))) {
        terrain->generateNoise(noiseParams);
    }

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Imported heightmap"));
    ImGui::DragFloat(Lang::tr("Min altitude"), &heightmapMin, 0.1f, -500.0f, 500.0f);
    ImGui::DragFloat(Lang::tr("Max altitude"), &heightmapMax, 0.1f, -500.0f, 500.0f);
    if (ImGui::Button(Lang::tr("Import a heightmap..."))) {
        IGFD::FileDialogConfig config; config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog("LoadHeightmapDlg",
            Lang::tr("Choose a heightmap (greyscale image)"), ".png,.jpg,.jpeg,.tga,.bmp", config);
    }
    ImGui::TextDisabled("%s", Lang::tr("The heightfield is replaced. Ctrl+Z to undo."));

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Auto texturing (altitude / slope)"));
    if (ImGui::Button(Lang::tr("Enable (auto)"))) {
        commandBuffer->setTerrainRenderMode(6);
    }
    bool changed = false;
    changed |= ImGui::DragFloat(Lang::tr("Layer 1 up to"), &terrainLayers.x, 0.1f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat(Lang::tr("Layer 2 up to"), &terrainLayers.y, 0.1f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat(Lang::tr("Layer 3 up to"), &terrainLayers.z, 0.1f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat(Lang::tr("Slope threshold"),   &terrainLayers.w, 0.01f, 0.0f, 1.0f);
    if (changed) commandBuffer->setTerrainLayers(terrainLayers);

    ImGui::Separator();
    ImGui::Text("%s", Lang::tr("Ground materials (4 layers)"));
    const char* layerNames[4] = { "Layer 0 (low)", "Layer 1", "Layer 2 (slopes)", "Layer 3 (peaks)" };

    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        int layerMatIdx = terrain->layerMaterial[i];
        std::string current = (layerMatIdx >= 0 && layerMatIdx < static_cast<int>(materials.size()))
                            ? materials[layerMatIdx]->getName() : std::string(Lang::tr("(default)"));

        if (ImGui::BeginCombo(Lang::tr(layerNames[i]), current.c_str())) {
            if (ImGui::Selectable(Lang::tr("(default)"), layerMatIdx < 0)) {
                pushUndo();
                terrain->layerMaterial[i] = -1;
            }
            for (size_t k = 0; k < materials.size(); ++k) {
                bool sel = (layerMatIdx == static_cast<int>(k));
                if (ImGui::Selectable(materials[k]->getName().c_str(), sel)) {
                    pushUndo();
                    terrain->layerMaterial[i] = static_cast<int>(k);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::SliderFloat(Lang::tr("Blend width"), &terrainRoughness, 0.05f, 10.0f)) {
        commandBuffer->setTerrainMaterial(terrainMetallic, terrainRoughness);
    }
    if (ImGui::IsItemActivated()) pushUndo();
    ImGui::End();
}

void Application::updateBrush() {
    brushActive = false;

    if (isPlaying) return;

    if (!terrainVisible) return;
    if (!sculptEnabled) return;
    if (window->isCursorLocked()) return;
    if (ImGuizmo::IsUsing()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (!ImGui::IsMousePosValid()) return;

    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f) return;

    if (io.MouseWheel != 0.0f) {
        brushRadius += io.MouseWheel;
        if (brushRadius < 0.1f)   brushRadius = 0.1f;
        if (brushRadius > 200.0f) brushRadius = 200.0f;
    }

    glm::vec2 ndc(2.0f * io.MousePos.x / width - 1.0f,
                  2.0f * io.MousePos.y / height - 1.0f);
    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 invVP = glm::inverse(projectionMatrix * view);
    glm::vec4 pNear = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
    pNear /= pNear.w;
    pFar  /= pFar.w;
    glm::vec3 rayOrigin(pNear);
    glm::vec3 rayDir = glm::normalize(glm::vec3(pFar - pNear));

    // Terrain transforme : on passe le rayon en espace LOCAL du terrain.
    glm::mat4 terrainModel = terrain->getModelMatrix();
    glm::mat4 invM = glm::inverse(terrainModel);
    glm::vec3 localRayOrigin = glm::vec3(invM * glm::vec4(rayOrigin, 1.0f));
    glm::vec3 localRayDir = glm::normalize(glm::vec3(invM * glm::vec4(rayDir, 0.0f)));

    glm::vec3 hit;   // espace LOCAL
    if (!terrain->raycast(localRayOrigin, localRayDir, hit)) return;

    brushHitPos = hit;
    brushActive = true;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pushUndo();

    if (sculptEnabled) {
        float amount = brushStrength * io.DeltaTime;

        if (brushMode == 0) {
            float dir = 0.0f;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))  dir += 1.0f;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) dir -= 1.0f;
            if (dir != 0.0f)
                terrain->sculpt(brushHitPos, brushRadius, dir * amount);
        }
        else if (brushMode == 1) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                terrain->smooth(brushHitPos, brushRadius, amount);
        }
        else if (brushMode == 2) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                flattenTarget = terrain->heightAtWorld(brushHitPos.x, brushHitPos.y);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                terrain->flatten(brushHitPos, brushRadius, amount, flattenTarget);
        }
    }

    // Anneau : cercle LOCAL drape, reprojete via la matrice du terrain (M).
    const int nbSegments = 48;
    ImVec2 pts[nbSegments];
    bool ok = true;
    for (int i = 0; i < nbSegments; ++i) {
        float a = (float)i / nbSegments * 6.2831853f;
        glm::vec3 wp = hit + glm::vec3(std::cos(a) * brushRadius,
                                       std::sin(a) * brushRadius, 0.0f);
        wp.z = terrain->heightAtWorld(wp.x, wp.y) + 0.02f;

        glm::vec4 clip = projectionMatrix * view * terrainModel * glm::vec4(wp, 1.0f);
        if (clip.w <= 0.0001f) { ok = false; break; }
        glm::vec3 n = glm::vec3(clip) / clip.w;
        pts[i] = ImVec2((n.x * 0.5f + 0.5f) * width,
                        (n.y * 0.5f + 0.5f) * height);
    }
    if (ok) {
        ImGui::GetBackgroundDrawList()->AddPolyline(
            pts, nbSegments, IM_COL32(255, 220, 60, 255), ImDrawFlags_Closed, 2.0f);
    }
}

void Application::drawAxisGizmo() {
    if (!showAxisGizmo) return;

    ImGuiIO& io = ImGui::GetIO();
    const float size = 55.0f;                                  // rayon du widget
    ImVec2 center(io.DisplaySize.x - size - 25.0f, size + 45.0f);

    glm::mat3 viewRot(camera->getViewMatrix());   // rotation monde -> vue

    struct Axis { glm::vec3 dir; ImU32 col; const char* name; };
    const Axis axes[3] = {
        { glm::vec3(1,0,0), IM_COL32(230,  70,  70, 255), "X" },
        { glm::vec3(0,1,0), IM_COL32( 90, 200,  90, 255), "Y" },
        { glm::vec3(0,0,1), IM_COL32( 80, 140, 245, 255), "Z" },
    };

    struct Tip { ImVec2 p; float depth; ImU32 col; const char* name; bool positive; };
    Tip tips[6];
    for (int i = 0; i < 3; ++i) {
        glm::vec3 axisView = viewRot * axes[i].dir;
        for (int j = 0; j < 2; ++j) {
            float sign = (j == 0) ? 1.0f : -1.0f;
            glm::vec3 axisDir = axisView * sign;
            Tip& t = tips[i * 2 + j];
            t.p = ImVec2(center.x + axisDir.x * size, center.y - axisDir.y * size); // ecran : y vers le bas
            t.depth = axisDir.z;  // vue : +z = vers la camera
            t.col = axes[i].col;
            t.name = axes[i].name;
            t.positive = (j == 0);
        }
    }

    // Du plus lointain au plus proche, pour que les axes se recouvrent correctement.
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            if (tips[j].depth < tips[i].depth) std::swap(tips[i], tips[j]);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (int i = 0; i < 6; ++i) {
        const Tip& t = tips[i];
        if (t.positive) {
            dl->AddLine(center, t.p, t.col, 3.0f);
            dl->AddCircleFilled(t.p, 9.0f, t.col);
            ImVec2 textSize = ImGui::CalcTextSize(t.name);
            dl->AddText(ImVec2(t.p.x - textSize.x * 0.5f, t.p.y - textSize.y * 0.5f),
                        IM_COL32(20, 20, 20, 255), t.name);
        } else {
            dl->AddCircleFilled(t.p, 7.0f, IM_COL32(35, 35, 35, 190));
            dl->AddCircle(t.p, 7.0f, t.col, 0, 2.0f);
        }
    }
}

void Application::updateEditorCamera(float dx, float dy, float wheel) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (isPlaying) return;

    GLFWwindow* w = window->getHandle();

    // Le clic molette est souvent capte par le systeme (defilement par bouton) :
    // on accepte aussi le clic droit, sauf en mode sculpt ou il creuse le terrain.
    bool middle = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS
               || (!sculptEnabled && glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

    bool shift = input.isDown(ACTION_SNAP_MODIFIER);

    // Axes de la camera lus dans la matrice de vue (independant de la convention).
    glm::mat4 varViewMtrx = camera->getViewMatrix();
    glm::vec3 right   =  glm::vec3(varViewMtrx[0][0], varViewMtrx[1][0], varViewMtrx[2][0]);
    glm::vec3 upVec   =  glm::vec3(varViewMtrx[0][1], varViewMtrx[1][1], varViewMtrx[2][1]);
    glm::vec3 forward = -glm::vec3(varViewMtrx[0][2], varViewMtrx[1][2], varViewMtrx[2][2]);

    // --- Orbite : clic droit (ou molette) ---
    if (middle && !shift && (dx != 0.0f || dy != 0.0f)) {
        const float sens = 0.3f;
        float yaw   = camera->getYaw() - dx * sens;
        float pitch = glm::clamp(camera->getPitch() - dy * sens, -89.0f, 89.0f);

        camera->setState(camera->getPosition(), yaw, pitch);            // 1) oriente
        glm::mat4 varViewMtrx2 = camera->getViewMatrix();
        glm::vec3 forward2 = -glm::vec3(varViewMtrx2[0][2], varViewMtrx2[1][2], varViewMtrx2[2][2]);
        camera->setState(orbitTarget - forward2 * orbitDistance, yaw, pitch); // 2) repositionne
        return;
    }

    // --- Pan : Maj + clic droit (ou molette) ---
    if (middle && shift && (dx != 0.0f || dy != 0.0f)) {
        float k = orbitDistance * 0.0015f;
        glm::vec3 offset = (-right * dx + upVec * dy) * k;
        orbitTarget += offset;
        camera->setState(camera->getPosition() + offset,
                         camera->getYaw(), camera->getPitch());
        return;
    }

    // --- Zoom : molette (sauf en mode sculpt, ou elle regle le pinceau) ---
    if (wheel != 0.0f && !sculptEnabled) {
        orbitDistance = glm::clamp(orbitDistance * std::pow(0.9f, wheel), 0.5f, 5000.0f);
        camera->setState(orbitTarget - forward * orbitDistance,
                         camera->getYaw(), camera->getPitch());
    }

    // --- L'orbite suit la selection (recale seulement quand elle CHANGE) ---
    if (orbitAroundSelection) {
        int cur = terrainSelected ? -2 : selectedObjectIndex;
        if (cur != lastOrbitSelection) {
            lastOrbitSelection = cur;
            if (selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(sceneObjects.size()))
                orbitTarget = sceneObjects[selectedObjectIndex]->position;
            else if (terrainSelected)
                orbitTarget = terrain->position;
        }
    }

    // --- F ou pave numerique "." : centrer sur la selection ---
    bool fNow = input.isDown(ACTION_FOCUS_SELECTION);
    if (fNow && !focusKeyWas && !io.WantTextInput) {
        if (terrainSelected) {
            orbitTarget = terrain->position;
        } else if (selectedObjectIndex >= 0 &&
                   selectedObjectIndex < static_cast<int>(sceneObjects.size())) {
            orbitTarget = sceneObjects[selectedObjectIndex]->position;
        }
        camera->setState(orbitTarget - forward * orbitDistance,
                         camera->getYaw(), camera->getPitch());
    }
    focusKeyWas = fNow;
}

Application::Snapshot Application::captureState() {
    Snapshot s;

    for (const auto& o : sceneObjects) {
        ObjectState os;
        os.meshIndex = o->meshIndex;
        os.position = o->position;
        os.orientation = o->orientation;
        os.scale = o->scale;
        os.scriptPath = o->scriptPath;
        os.metallic = o->metallic;
        os.roughness = o->roughness;
        os.name = o->name;
        os.parent = o->parent;
        os.materialIndex = o->materialIndex;
        os.worldUvScale = o->worldUvScale;
        os.animClip = o->animClip;
        os.animSpeed = o->animSpeed;
        os.animPlaying = o->animPlaying;
        os.animLoop = o->animLoop;
        os.stateMachine = o->stateMachine.asset;
        s.objects.push_back(os);      // LE PUSH_BACK EN DERNIER, toujours
    }

    s.resX = terrain->getResX();   s.resY = terrain->getResY();
    s.sizeX = terrain->getSizeX(); s.sizeY = terrain->getSizeY();
    s.uvTiling = terrain->getUvTiling();
    s.terrainRenderMode = commandBuffer->getTerrainRenderMode();
    s.layers = terrainLayers;
    s.tPos = terrain->position; s.tOri = terrain->orientation; s.tScale = terrain->scale;

    // Le relief n'est recopie que s'il a change depuis le dernier instantane.
    if (heightsCache && heightsCacheVersion == terrain->getVersion()) {
        s.heights = heightsCache;
    } else {
        s.heights = std::make_shared<std::vector<float>>(terrain->heights());
        heightsCache = s.heights;
        heightsCacheVersion = terrain->getVersion();
    }
    s.heightsVersion = terrain->getVersion();

    s.lightDirection = lightDirection; s.lightColor = lightColor;
    s.lightIntensity = lightIntensity; s.ambientStrength = ambientStrength;
    s.shininess = shininess;
    for (const auto& m : animMachines) s.machines.push_back({ m, *m});
    return s;
}

void Application::applySnapshot(const Snapshot& s) {
    // --- Objets ---
    sceneObjects.clear();
    for (const auto& os : s.objects) {
        if (os.meshIndex < 0 || os.meshIndex >= static_cast<int>(availableMeshes.size())) continue;
        auto varObj = std::make_shared<SceneObject>(availableMeshes[os.meshIndex]);
        varObj->meshIndex   = os.meshIndex;
        varObj->position    = os.position;
        varObj->orientation = os.orientation;
        varObj->scale       = os.scale;
        varObj->scriptPath  = os.scriptPath;
        varObj->parent = os.parent;
        varObj->materialIndex = os.materialIndex;
        sceneObjects.push_back(varObj);
        varObj->metallic  = os.metallic;
        varObj->roughness = os.roughness;
        varObj->name =os.name;
        varObj->worldUvScale = os.worldUvScale;
        varObj->animClip = os.animClip;
        varObj->animSpeed = os.animSpeed;
        varObj->animPlaying = os.animPlaying;
        varObj->animLoop = os.animLoop;
        varObj->stateMachine.asset = os.stateMachine;
        varObj->stateMachine.reset();
    }
    commandBuffer->setSceneObjects(sceneObjects);
    updateWorldMatrices();
    if (selectedObjectIndex >= static_cast<int>(sceneObjects.size())) selectedObjectIndex = -1;
    selectedObjects.clear();

    // --- Terrain : geometrie (seulement si elle a change) ---
    if (s.resX != terrain->getResX() || s.resY != terrain->getResY() ||
        s.sizeX != terrain->getSizeX() || s.sizeY != terrain->getSizeY()) {
        terrain->rebuild(s.resX, s.resY, s.sizeX, s.sizeY);
        commandBuffer->setTerrain(
            terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
            terrain->getIndexCount(), terrain->getLayerSet(),terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount());
    }

    // --- Relief ---
    if (s.heights && s.heights->size() == terrain->heights().size()) {
        terrain->setHeights(*s.heights);
        heightsCache = s.heights;
        heightsCacheVersion = terrain->getVersion();
    }

    terrain->position = s.tPos; terrain->orientation = s.tOri; terrain->scale = s.tScale;
    terrain->setUvTiling(s.uvTiling);
    commandBuffer->setTerrainRenderMode(s.terrainRenderMode);
    commandBuffer->setTerrainLayers(s.layers);

    // --- Reglages refletes dans l'UI ---
    terrainLayers = s.layers;
    terrainResX = s.resX;   terrainResY = s.resY;
    terrainSizeX = s.sizeX; terrainSizeY = s.sizeY;
    terrainUvTiling = s.uvTiling;

    lightDirection = s.lightDirection; lightColor = s.lightColor;
    lightIntensity = s.lightIntensity; ambientStrength = s.ambientStrength;
    shininess = s.shininess;

    // --- Machines a etats : meme objets, contenu restaure ---
    animMachines.clear();
    for (const auto& ms : s.machines) {
        // le nom change : l ancien fichier devra disparaitre au prochain save
        if (ms.asset->name != ms.content.name) 
            pendingMachineDeletes.push_back(AnimStateMachineSerializer::fileNameFor(ms.asset->name));
        ms.asset->restoreFrom(ms.content);
        animMachines.push_back(ms.asset);
    }
    smUndoRef = nullptr; // le panneau reprend sa copie de reference sans creer d entree
    smRenameTarget = nullptr;   // et recharge le nom affiche
}

void Application::pushUndo() {
    pushUndo(captureState());
}

void Application::pushUndo(Snapshot s) {
    undoStack.push_back(std::move(s));
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void Application::undo() {
    if (undoStack.empty()) { sceneMessage = Lang::tr("Nothing to undo."); return; }
    redoStack.push_back(captureState());
    applySnapshot(undoStack.back());
    undoStack.pop_back();
    sceneMessage = Lang::fmt("Undone (%d left)", static_cast<int>(undoStack.size()));
}

void Application::redo() {
    if (redoStack.empty()) { sceneMessage = Lang::tr("Nothing to redo."); return; }
    undoStack.push_back(captureState());
    applySnapshot(redoStack.back());
    redoStack.pop_back();
    sceneMessage = Lang::tr("Redone.");
}

void Application::drawSunGizmo() {
    if (!showSunGizmo || isPlaying) return;
    if (glm::length(lightDirection) < 0.0001f) return;

    ImGuiIO& io = ImGui::GetIO();
    glm::mat4 vp = projectionMatrix * camera->getViewMatrix();

    auto project = [&](const glm::vec3& w, ImVec2& out) -> bool {
        glm::vec4 clip = vp * glm::vec4(w, 1.0f);
        if (clip.w <= 0.0001f) return false;
        glm::vec3 n = glm::vec3(clip) / clip.w;
        out = ImVec2((n.x * 0.5f + 0.5f) * io.DisplaySize.x,
                     (n.y * 0.5f + 0.5f) * io.DisplaySize.y);
        return true;
    };

    glm::vec3 d = glm::normalize(lightDirection);
    glm::vec3 upRef = (std::abs(d.z) > 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    glm::vec3 varRight = glm::normalize(glm::cross(upRef, d));

    const float len = 4.0f;
    const float head = 0.8f;
    glm::vec3 tip = sunGizmoPos + d * len;

    ImU32 col = sunSelected ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 190, 40, 220);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Trois rayons paralleles (icone de lumiere directionnelle).
    for (int i = -1; i <= 1; ++i) {
        glm::vec3 off = varRight * (static_cast<float>(i) * 0.6f);
        ImVec2 a, b;
        if (project(sunGizmoPos + off, a) && project(tip + off, b)) {
            dl->AddLine(a, b, col, (i == 0) ? 3.0f : 1.5f);

            // Pointe de fleche
            ImVec2 h1, h2;
            if (project(tip + off - d * head + varRight * (head * 0.4f), h1) &&
                project(tip + off - d * head - varRight * (head * 0.4f), h2)) {
                dl->AddLine(b, h1, col, (i == 0) ? 3.0f : 1.5f);
                dl->AddLine(b, h2, col, (i == 0) ? 3.0f : 1.5f);
            }
        }
    }

    // Petit disque : le "soleil" lui-meme.
    ImVec2 c;
    if (project(sunGizmoPos, c)) {
        dl->AddCircleFilled(c, 7.0f, col);
        dl->AddCircle(c, 11.0f, col, 0, 1.5f);
    }
}

/*
place la camera a distance du centre en le reagrdant 
on lit l'axe avant dans la matrice de vue
*/
void Application::frameOrbit(const glm::vec3& center, float distance, float yaw, float pitch) {
    orbitTarget = center;
    orbitDistance = distance;

    camera->setState(center, yaw, pitch);
    glm::mat4 varViewMtrx = camera->getViewMatrix();
    glm::vec3 varForward = -glm::vec3(varViewMtrx[0][2], varViewMtrx[1][2], varViewMtrx[2][2]);
    camera->setState(center - varForward * distance, yaw, pitch);
}

void Application::setEditorMode(EditorMode m) {
    if (m == editorMode) return;

    // on quitte la scen et on change l'orientation de la camera 
    if (m == EditorMode::Scene) {
        savedCamPos = camera->getPosition();
        savedCamYaw = camera->getYaw();
        savedCamPitch = camera->getPitch();
        savedOrbitDistance = orbitDistance;
    }

    if (m == EditorMode::Material) {
        frameOrbit(glm::vec3(0.0f), 3.5f, 150.0f, -20.0f);
    
    } else if (m == EditorMode::Animation) {
        // edition du perso si il a un squelette
        animTargetIndex = -1;
        if (selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(sceneObjects.size())) {
            int meshIdx = sceneObjects[selectedObjectIndex]->meshIndex;
            if (meshIdx >= 0 && meshIdx < static_cast<int>(availableMeshes.size()) && availableMeshes[meshIdx]->isSkinned()) {
                animTargetIndex = selectedObjectIndex;
            }
        }

        glm::vec3 center(0.0f);
        float dist = 3.5f;
        if (animTargetIndex >= 0) {
            const auto& obj = sceneObjects[animTargetIndex];
            const auto& mesh = availableMeshes[obj->meshIndex];
            glm::vec3 lo = mesh->getBoundsMin() * obj->scale;
            glm::vec3 hi = mesh->getBoundsMax() * obj->scale;
            center = obj->position + (lo + hi) *0.5f;
            float radius = glm::length(hi - lo) * 0.5f;
            dist = std::max(1.0f, radius * 2.2f);
        }
        frameOrbit(center , dist, 150.0f, -10.0f);
    } else {
        camera->setState(savedCamPos, savedCamYaw, savedCamPitch);
        orbitDistance = savedOrbitDistance;
    }

    editorMode = m;
}