#pragma once

#include <engine/renderer/AnimStateMachine.hpp>

#include <memory>
#include <string>

/*
format .animsm : json lisible et modifiable a la main
dans le fichier les parametres et autres etats sont references par nom
ils sont convertis en index au chargement
*/

namespace AnimStateMachineSerializer {
    // nom du fichier .animsm d une machine, sans caracteres interdits
    std::string fileNameFor(const std::string& machineName);

    // throw si le fichier ne peut pas etre ecrit
    void save(const AnimStateMachineAsset& sm, const std::string& path);

    // throw si le fichier est illisible
    // references cassees ne bloquent pas : elles sont signalees dans warnings
    std::shared_ptr<AnimStateMachineAsset> load(const std::string& path, std::string& warnings);
}