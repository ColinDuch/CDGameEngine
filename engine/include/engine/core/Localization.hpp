#pragma once
#include <string>

// Traduction : la CLE est le texte anglais. Sans fichier charge, tr() renvoie la cle,
// donc l'anglais fonctionne toujours, meme sans traduction.
namespace Lang {
    void load(const std::string& file);   // ex "lang/fr.txt" ; chaine vide = anglais
    const char* tr(const char* key);
    const std::string& current();
}