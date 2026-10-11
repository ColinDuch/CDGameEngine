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

namespace{
    // rayon contre boite alignee sur les axes (methodes des "slabs")
    // t = position du point d entree le long du rayon
    bool rayBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& t) {
        float tMin = 0.0f, tMax = 1e30f;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(d[a]) < 1e-8f) {
                if (o[a] < bmin[a] || o[a] > bmax[a]) return false;     // parallele et a cote
                continue;
            }
            float t1 = (bmin[a] - o[a]) / d[a];
            float t2 = (bmax[a] - o[a]) / d[a];
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
        t = tMin;
        return true;
    }

    // Moller-Trumbore : rayon contre triangle, des deux cotes
    bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
        const glm::vec3 e1 = b - a;
        const glm::vec3 e2 = c - a;
        const glm::vec3 p = glm::cross(d, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 1e-12f) return false;       // rayon parallele au triangle 

        const float inv = 1.0f / det;
        const glm::vec3 s = o - a;
        const float u = glm::dot(s, p) * inv;       // coordonnees dasn le triangle 
        if (u < 0.0f || u > 1.0f) return false;
        const glm::vec3 q = glm::cross(s, e1);
        const float v =glm::dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) return false;

        t = glm::dot(e2, q) * inv;
        return t > 0.0f;        // devantla camera seulement
    }
}

bool Application::mouseRay(glm::vec3& origin, glm::vec3& dir) const {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid() || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) return false;

    // du pixel a l ecran [-1, 1], puis retour dans le monde par l inverse de la camera
    const glm::vec2 ndc(2.0f * io.MousePos.x / io.DisplaySize.x - 1.0f, 2.0f * io.MousePos.y / io.DisplaySize.y - 1.0f);
    const glm::mat4 invVP = glm::inverse(projectionMatrix * camera->getViewMatrix());
    glm::vec4 pNear = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    glm::vec4 pFar = invVP * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
    pNear /= pNear.w;
    pFar /= pFar.w;

    origin = glm::vec3(pNear);
    dir = glm::normalize(glm::vec3(pFar - pNear));
    return true;
}

int Application::pickObject(const glm::vec3& origin, const glm::vec3& dir) const {
    int best = -1;
    float bestDist = 1e30f;

    for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
        const auto& o = sceneObjects[i];
        if (o->meshIndex < 0 || o->meshIndex >= static_cast<int>(availableMeshes.size())) continue;
        const Mesh& mesh = *availableMeshes[o->meshIndex];

        // le rayon passe dans le repere de l objet, ou sa boite est alignee sur les axes
        const glm::mat4 inv = glm::inverse(o->getModelMatrix());
        const glm::vec3 localOrigin = glm::vec3(inv * glm::vec4(origin, 1.0f));
        const glm::vec3 localDir = glm::vec3(inv * glm::vec4(dir, 0.0f));       // non normalise : voir plus bas

        // localDir n etant pas renormalise, t designe le meme point qu en monde :
        // c est directement la distance dpuis la camera, quelle que soit l echelle 
        float t;
        if (!rayBox(localOrigin, localDir, mesh.getBoundsMin(), mesh.getBoundsMax(), t)) continue;
        if (t >= bestDist) continue;    // meme sa boite est derriere un objet deja touche
        
        const auto& verts = mesh.getVerticesCPU();
        const auto& idx = mesh.getIndicesCPU();
        if (idx.empty()) {      // pas de geometrie en memoire : la boite suffira
            bestDist = t;
            best = i;
            continue;
        }

        // precision : le triangle touche le plus proche
        for (size_t k = 0; k + 2 < idx.size(); k += 3) {
            float tt;
            if (rayTriangle(localOrigin, localDir, verts[idx[k]].position, verts[idx[k + 1]].position, verts[idx[k + 2]].position, tt) && tt < bestDist) {
                bestDist = tt;
                best = i;
            }
        }
        //dans la boite mais entre les triangles : aucun ne touche, rien ne change 
    }
    return best;
}

void Application::updateViewportPick() {
    if (isPlaying || sculptEnabled || window->isCursorLocked()) return;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return;      // ce clic est pour le gizmo

    glm::vec3 origin, dir;
    if (!mouseRay(origin, dir)) return;

    const int hit = pickObject(origin, dir);
    if (hit >= 0) {
        selectObject(hit, io.KeyCtrl, false);   // Ctrl : ajoute ou retire
    } else if (!io.KeyCtrl) {
        selectedObjects.clear();    // clic dans le vide : on deselectionne
        selectedObjectIndex = -1;
    }
}

uint32_t Application::claimObjectId(uint32_t wanted) {
    // 0 ou deja pris (fichier modifie a la main) : on en donne un neuf
    if (wanted == 0 || indexOfObject(wanted) >= 0) return newObjectId();
    if (wanted >= nextObjectId) nextObjectId = wanted + 1;  // les suivants ne le reprendront pas
    return wanted;
}

int Application::indexOfObject(uint32_t id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        if (sceneObjects[i]->id == id) return static_cast<int>(i);
    }
    return -1;
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
        copy->id = newObjectId();   // une copie est un nouvel objet
        copy->bodyType = src->bodyType;
        copy->mass = src->mass;
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

    glm::vec3 rayOrigin, rayDir;
    if (!mouseRay(rayOrigin, rayDir)) return;
    glm::mat4 view = camera->getViewMatrix();   // encore utilise pour dessiner le cercle du pinceau

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
        os.id = o->id;
        os.bodyType = o->bodyType;
        os.mass = o->mass;
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
    s.gameActions = gameInput.getActions();
    s.gameSequences = gameInput.getSequences();
    return s;
}

void Application::applySnapshot(const Snapshot& s) {
    // --- Objets ---
    sceneObjects.clear();
    for (const auto& os : s.objects) {
        if (os.meshIndex < 0 || os.meshIndex >= static_cast<int>(availableMeshes.size())) continue;
        auto varObj = std::make_shared<SceneObject>(availableMeshes[os.meshIndex]);
        varObj->meshIndex = os.meshIndex;
        varObj->id = claimObjectId(os.id);
        varObj->bodyType = os.bodyType;
        varObj->mass = os.mass;
        varObj->position = os.position;
        varObj->orientation = os.orientation;
        varObj->scale = os.scale;
        varObj->scriptPath = os.scriptPath;
        varObj->parent = os.parent;
        varObj->materialIndex = os.materialIndex;
        sceneObjects.push_back(varObj);
        varObj->metallic = os.metallic;
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
    gameInput.getActions() = s.gameActions;
    gameInput.getSequences() = s.gameSequences;
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

// personnage : capsule debout, haute comme le mesh, epaisse comme sa profondeur
// (la largeur compte les bras ecartes de la T pose, la profondeur non)
void Application::characterCapsule(const SceneObject& o, float& radius, float& height, glm::vec3& feet) const {
    const Mesh& mesh = *availableMeshes[o.meshIndex];
    const glm::vec3 bmin = mesh.getBoundsMin(), bmax = mesh.getBoundsMax();
    const glm::vec3 ext = (bmax - bmin) * glm::abs(o.scale);     // tailles du mesh, echelle appliquee
    const glm::mat3 rot = glm::mat3_cast(o.orientation);

    // quel axe du mesh pointe vers le haut du monde ? Un modele importe en Y-up et
    // redresse par une rotation a sa hauteur sur Y, pas sur Z. rot[a] = ou va l axe a.
    int up = 2;
    float best = -1.0f;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(rot[a].z) > best) {
            best = std::abs(rot[a].z);
            up = a;
        }
    }
    height = ext[up];
    // l epaisseur : la plus petite des deux autres tailles (la profondeur, pas les bras)
    radius = std::min(0.5f * std::min(ext[(up + 1) % 3], ext[(up + 2) % 3]), 0.5f * height);

    // les pieds : le point le plus bas de la boite tournee, au centre horizontal,
    // en axes du MONDE par rapport a la position de l objet (la capsule ne tourne pas)
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z);
        const glm::vec3 p = rot * (corner * o.scale);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    feet = glm::vec3(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), lo.z);
}

/*
formes de collision en fil de fer, dessinees par-dessus la scene
elles sont deduites des objets eux-memes : en play, les objets physiques suivent la physique,
donc on voir exactement les boites simulees 
*/
void Application::drawColliders() {
    if (!showColliders) return;

    const ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x, height = io.DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f) return;

    const glm::mat4 viewProj = projectionMatrix * camera->getViewMatrix();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // un segment du monde vers l ecran ; rien si un bout est derriere la camera 
    auto line3D = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
        const glm::vec4 ca = viewProj * glm::vec4(a, 1.0f);
        const glm::vec4 cb = viewProj * glm::vec4(b, 1.0f);
        if (ca.w <= 0.0001f || cb.w <= 0.0001f) return;
        const glm::vec3 na = glm::vec3(ca) / ca.w, nb = glm::vec3(cb) / cb.w;
        dl->AddLine(ImVec2((na.x * 0.5f + 0.5f) * width, (na.y * 0.5f + 0.5f) * height),
            ImVec2((nb.x * 0.5f + 0.5f) * width, (nb.y * 0.5f + 0.5f) * height), color, thickness);
    };

    // les 12 aretes d une boite. Un coin est numerote par 3 bits : bit 0 x max, bit 1 = y max, bit 2 = z max
    static const int EDGES[12][2] = { {0, 1}, {2, 3}, {4, 5}, {6, 7},      // aretes selon x
                                      {0, 2}, {1, 3}, {4, 6}, {5, 7},      // selon y
                                      {0, 4}, {1, 5}, {2, 6}, {3, 7} };    // selon z
    
    for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
        const auto& o = sceneObjects[i];
        if (o->bodyType == SceneObject::BodyType::None) continue;
        if (o->meshIndex < 0 || o->meshIndex >= static_cast<int>(availableMeshes.size())) continue;
        // personnage : capsule debout (violette), comme celle que cree le Play
        if (o->bodyType == SceneObject::BodyType::Character) {
            const bool sel = std::find(selectedObjects.begin(), selectedObjects.end(), i) != selectedObjects.end();
            const ImU32 color = (o->parent >= 0) ? IM_COL32(255, 170, 60, sel ? 255 : 150)
                                                 : IM_COL32(200, 120, 255, sel ? 255 : 150);
            const float thick = sel ? 2.0f : 1.0f;

            float r, h;
            glm::vec3 feet;
            characterCapsule(*o, r, h, feet);
            r = std::max(r, 0.05f);
            const glm::vec3 base = o->position + feet;
            const glm::vec3 c0 = base + glm::vec3(0.0f, 0.0f, r);                     // centre de la sphere du bas
            const glm::vec3 c1 = base + glm::vec3(0.0f, 0.0f, std::max(h - r, r));    // ... et du haut

            constexpr int SEG = 24;
            for (int s = 0; s < SEG; ++s) {
                const float a0 = 6.2831853f * s / SEG, a1 = 6.2831853f * (s + 1) / SEG;
                const glm::vec3 p0(std::cos(a0) * r, std::sin(a0) * r, 0.0f);
                const glm::vec3 p1(std::cos(a1) * r, std::sin(a1) * r, 0.0f);
                line3D(c0 + p0, c0 + p1, color, thick);      // cercle du bas
                line3D(c1 + p0, c1 + p1, color, thick);     // cercle du haut
            }
            for (int s = 0; s < SEG / 2; ++s) {     // demi-cercles des calottes
                const float a0 = 3.1415927f * s / (SEG / 2), a1 = 3.1415927f * (s + 1) / (SEG / 2);
                const glm::vec3 x0(std::cos(a0) * r, 0.0f, std::sin(a0) * r), x1(std::cos(a1) * r, 0.0f, std::sin(a1) * r);
                const glm::vec3 y0(0.0f, std::cos(a0) * r, std::sin(a0) * r), y1(0.0f, std::cos(a1) * r, std::sin(a1) * r);
                line3D(c1 + x0, c1 + x1, color, thick);
                line3D(c1 + y0, c1 + y1, color, thick);
                line3D(c0 - x0, c0 - x1, color, thick);
                line3D(c0 - y0, c0 - y1, color, thick);
            }
            for (int s = 0; s < 4; ++s) {       // 4 aretes verticales
                const float a = 1.5707963f * s;
                const glm::vec3 p(std::cos(a) * r, std::sin(a) * r, 0.0f);
                line3D(c0 + p, c1 + p, color, thick);
            }
            continue;
        }

        const Mesh& mesh = *availableMeshes[o->meshIndex];
        const glm::vec3 bmin = mesh.getBoundsMin(), bmax = mesh.getBoundsMax();
        const glm::mat4 mvp = viewProj * o->getModelMatrix();

        ImVec2 pts[8];
        bool front[8];
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner((c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z);
            const glm::vec4 clip = mvp * glm::vec4(corner, 1.0f);
            front[c] = clip.w > 0.0001f;
            if (front[c]) {
                const glm::vec3 n = glm::vec3(clip) / clip.w;
                pts[c] = ImVec2((n.x * 0.5f + 0.5f) * width, (n.y * 0.5f + 0.5f) * height);
            }
        }

        // vert = dynamique, bleu = statique, orange = ignore (enfant), plus vif si selectionne
        const bool selected = std::find(selectedObjects.begin(), selectedObjects.end(), i) != selectedObjects.end();
        const int alpha = selected ? 255 : 150;
        ImU32 color = (o->bodyType == SceneObject::BodyType::Dynamic) ? IM_COL32(80, 230, 120, alpha) : IM_COL32(90, 160, 255, alpha);
        if (o->parent >= 0) color = IM_COL32(255, 170, 60, alpha);

        for (const auto& e : EDGES) {
            if (front[e[0]] && front[e[1]]) dl->AddLine(pts[e[0]], pts[e[1]], color, selected ? 2.0f : 1.0f);
        }
    }
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