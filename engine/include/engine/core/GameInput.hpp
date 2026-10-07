# pragma once 

#include <engine/core/InputMap.hpp>

#include <string>
#include <vector>

/*
actions de jeu : definies par le projet (input.json) et lues par les scripts lua
a ne pas confondre avec InputMap qui contient les controles de l editeur,
propres a cahque utilisateur (preference.txt)
*/

struct GameAction {
    std::string name;   //cle utilisee en lua : Engine.isActionDown("jump")
    InputBinding binding;   // memes champs que l editeur (id et label inutilises)
};

/*
sequence : des actions l une apres l autre, facon jeu de combat 
une etape peut demander plusieurs actions en meme temps (bas + avant = diagonale)
les actions sont designees par leur nom : une action surpprimee rend l etape impossible
*/
struct GameSequence {
    std::string name = "sequence";    // cle lua : Engine.wasSequenceDone("exemple")
    float window = 0.35f;       // delai max entre deux etapes, en secondes
    std::vector<std::vector<std::string>> steps;    // chaque etape : actions a tenir ensemble
};

class GameInput {
    public:
        GameInput() { resetDefaults(); }

        // apres InputMap::update : reutilise l etat de la manette deja lu cette image 
        void update(GLFWwindow* window, const GLFWgamepadstate* pad);

        int find(const std::string& name) const;    // -1 si inconnue

        bool isDown(int a) const { return values[a] > 0.5f; }
        // une sequence terminee "consomme" l appui de sa derniere etape :
        // le coup special part, pas aussi le coup normal
        bool wasPressed(int a) const { return pressedRaw(a) && !consumed[a]; }
        bool wasReleased(int a) const { return values[a] <= 0.5f && previous[a] > 0.5f; }
        float value(int a) const { return values[a]; } // 0..1, analogique au stick

        std::vector<GameAction>& getActions() { return actions; }
        const std::vector<GameAction>& getActions() const { return actions; }

        // --- sequences ---
        std::vector<GameSequence>& getSequences() { return sequences; }
        const std::vector<GameSequence>& getSequences() const { return sequences; }
        int findSequence(const std::string& name) const;    // -1 si inconnue
        bool sequenceDone(int s) const { return s < static_cast<int>(seqState.size()) && seqState[s].done; }
        int sequenceProgress(int s) const { return s < static_cast<int>(seqState.size()) ? seqState[s].progress : 0; }
        // renommer une action met a jour les sequences qui l utilisent 
        void renameAction(const std::string& from, const std::string& to);

        void resetDefaults();

        // input.json a la racine du projet. throw si le fichier est illisible
        void save(const std::string& path) const;
        void load(const std::string& path);

    private:
        struct SequenceState {
            int progress = 0;   // etapes deja validees
            float timer = 0.0f;     //temps depuis la derniere etape 
            bool done = false;  // terminee a cette image
        };

        bool pressedRaw(int a) const { return values[a] > 0.5f && previous[a] <= 0.5f; }
        bool stepMatched(const std::vector<std::string>& step) const;
        void updateSequences(float dt);
        void resizeState();
        
        std::vector<GameAction> actions;
        std::vector<float> values;      // une valeur par action, cette image
        std::vector<float> previous;    // et a l image precedente (pour les fronts)
        std::vector<char> consumed;     // appui absorbe par une sequence cette image

        std::vector<GameSequence> sequences;
        std::vector<SequenceState> seqState;
        double lastTime = -1.0;     // pour le delai entre les etapes 
};