# pragma once 

#include <engine/core/InputMap.hpp>

#include <string>
#include <vector>

/*
actions de jeu : definis par le projet (input.json) et lues par les scripts lua
a ne pas confondre avec InputMap qui contient les controles de l editeur,
propres a chauque utilisateur (preferences.txt)
*/

struct GameAction {
    std::string name;   // cle utilisee en lua : Engine.isActionDown("jump")
    InputBinding binding;   // memes champs que l editeur (id et label inutilises)
};

class GameInput {
    public:
        GameInput() { resetDefaults(); }

        // apres InputMap::update : reutilise l etat de la manette deja lu cette image
        void update(GLFWwindow* window, const GLFWgamepadstate* pad);
        
        int find(const std::string& name) const;    // -1 si inconnue

        bool isDown(int a) const { return values[a] > 0.5f; }
        bool wasPressed(int a) const { return values[a] > 0.5f && previous[a] <= 0.5f; }
        bool wasReleased(int a) const { return values[a] <= 0.5f && previous[a] > 0.5f; }
        float value(int a) const {return values[a]; } // 0..1, analogique au stick

        std::vector<GameAction>& getActions() { return actions; }
        const std::vector<GameAction>& getActions() const { return actions; }
        void resetDefaults();

        // input.json a la racine du projet. throw si le fichier est illisible
        void save(const std::string& path) const;
        void load(const std::string& path);
    
    private:
        std::vector<GameAction> actions;
        std::vector<float> values;      // une valeur pas action, cette image
        std::vector<float> previous;    // et a l image precedente (pour les fronts)
};