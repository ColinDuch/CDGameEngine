# CDGameEngine

Moteur de jeu vidéo 3D en **C++ / Vulkan** avec éditeur intégré, développé sous Linux (cross-platform visé : Linux / Windows / macOS). Monde en **Z-up** (convention Blender).

## Fonctionnalités

### Rendu

- Vulkan, éclairage **PBR** complet (BRDF Cook-Torrance : GGX, Smith, Fresnel-Schlick)
- **Ombres portées** : shadow map en profondeur avec filtrage PCF, zone d'ombre ancrée sur le regard et sur la grille des texels (pas de scintillement), rayon adaptatif selon l'altitude
- **Environnement HDR** : chargement de fichiers `.hdr` flottants, reflets mipmappés, affichage du ciel en fond
- Contrôle d'**exposition** séparé (façon appareil photo) avec tone mapping
- **Transparence** : pipeline en fusion alpha avec tri des objets de l'arrière vers l'avant
- **Frustum culling** par objet, avec compteur dans le panneau Paramètres
- Chargement de modèles via Assimp (`.obj`, `.fbx`, `.gltf`, `.glb`, `.dae`, `.stl`), multi-matériaux par sous-mesh
- Modes de rendu de diagnostic : damier gris/blanc (texture manquante), magenta (UV absentes), couleur unie du matériau
- **Texture projetée depuis le monde** (triplanaire) pour le blockout : étirer un cube n'étire pas le damier

### Animation squelettique

- Import du squelette et des poids par sommet (jusqu'à 4 os par sommet, normalisés)
- **Skinning sur GPU** via un pipeline et un buffer de sommets dédiés — la géométrie des objets non animés n'est pas alourdie
- Import de **clips d'animation** depuis le modèle lui-même ou depuis des fichiers séparés (`.fbx`, `.gltf`, `.glb`, `.dae`, compatible Mixamo)
- Association des os **par nom**, insensible aux espaces de noms des FBX (`mixamorig:`, `Character1:`…)
- **Fondu entre clips** (cross-fade) : la pose précédente est mélangée à la nouvelle, durée réglable
- **Ombres animées** : la passe d'ombre suit le mouvement, elle aussi skinnée
- **Notifies** : des événements nommés posés sur la timeline (clic droit), déplaçables à la souris, déclenchés en jeu et transmis aux scripts Lua via `onNotify`
- **Machine à états d'animation** : asset partagé entre personnages (fichier `.animsm`), avec paramètres Bool / Float / Trigger pilotés depuis Lua, états, transitions conditionnelles ordonnées et attente de fin de clip
- **Panneau Machine à états** : création, renommage, suppression, édition complète ; pendant le Play, l'état courant et la valeur des paramètres s'affichent en direct
- **Mode Éditeur d'animation** en plein écran : aperçu du personnage isolé, liste des clips, et **timeline** avec lecture, boucle, vitesse, et positionnement au clic image par image
- Clips, notifies et état de lecture enregistrés dans le projet

### Entrées

- **Deux niveaux de contrôles** : ceux de l'**éditeur** (caméra, sélection), personnels et mémorisés dans `preferences.txt`, et les **actions de jeu**, définies par le projet dans `input.json` et lues par les scripts
- **Actions de jeu entièrement configurables** : nom libre, deux touches, un bouton et un stick ou une gâchette par action ; assignation à la volée (« appuie sur une touche… »), clic droit pour effacer
- **Manettes** prises en charge (Xbox, PlayStation, Switch et compatibles) via le mapping standard de GLFW
- Axes analogiques : le stick donne une valeur progressive, pas du tout-ou-rien ; un même script fonctionne au clavier et à la manette

### Matériaux

- **Assets partagés et réutilisables** : un matériau défini une fois, assignable à plusieurs objets
- **8 cartes PBR** par matériau : couleur de base, normale, rugosité, métallique, occlusion ambiante, hauteur (parallax), émissif, opacité
- Facteurs réglables en direct : teinte, opacité, métallique, rugosité, forces d'occlusion et de normale, couleur et intensité d'émission, amplitude du relief, répétition des UV
- **Mode éditeur de matériaux** en plein écran, avec sphère de prévisualisation sur l'environnement HDR

### Terrain

- Heightfield entièrement paramétrable (taille monde et résolution de grille)
- Géométrie en mémoire vidéo dédiée, mise à jour par transfert — les hautes résolutions restent fluides
- Grille allégée pour la passe d'ombre (facteur réglable), invisible à l'œil et bien moins coûteuse
- Sculpt au pinceau : monter/descendre, lisser, aplanir
- Génération procédurale par bruit de Perlin / FBM, cohérente quelle que soit la résolution
- Import de heightmap depuis une image (8 et 16 bits)
- **4 couches de matériaux PBR** mélangées automatiquement selon l'altitude et la pente, avec largeur de fondu réglable
- Transformable comme un objet ; le sculpt suit la transformation

### Éditeur

- Interface ImGui dockable : Hiérarchie, Inspecteur, Assets, Éclairage, Terrain, Matériaux, Caméra, Contrôles, Contrôles du jeu, Animation, Timeline, Machine à états, Console de script
- Trois modes : **Scène**, **Éditeur de matériaux** et **Éditeur d'animation**
- **Bibliothèque de primitives de blockout** disponible au démarrage : cube, sphère, cylindre, cône, pyramide, rampe, plan, plus un personnage de test
- **Hiérarchie en arbre** avec parentage par glisser-déposer ; la position monde est conservée au reparentage (comportement Blender)
- **Sélection multiple** (Ctrl+clic, Maj+clic) et **duplication** de la sélection avec ses descendants
- Caméra style Blender : orbite, panoramique, zoom, centrage sur la sélection, plus un mode vol libre
- Plans de coupe (near/far), vitesse de vol et rayon d'ombre réglables
- Gizmos de manipulation (translation / rotation / échelle) avec alignement ; rotation interne en quaternions
- Gizmo du soleil orientable, widget d'axes du monde, grille de repère paramétrable
- **Annuler / Rétablir** global (objets et leurs réglages d'animation, terrain, relief, matériaux, éclairage, machines à états, contrôles du jeu)
- Objets nommables, avec un **identifiant unique et stable** (affiché dans l'Inspecteur, conservé à l'enregistrement et à l'annulation)
- Interface **multilingue** (français / anglais), extensible par simple fichier texte

### Gameplay

- Mode **Play / Stop** avec restauration de l'état d'édition
- **Scripting Lua** : un script de scène et un script par objet, chacun dans son propre état Lua
- API `Engine` : transformations, actions de jeu (clavier et manette), journal et pilotage de la machine à états d'animation (voir [Scripting](#scripting))
- Erreurs explicites dans la Console : paramètre ou action inconnus signalés avec le numéro de ligne
- Callbacks `start`, `update(dt)` et `onNotify(name, objectIndex)`

### Import / export

- **Export de la scène en glTF** (`.gltf`, géométrie et textures embarquées) depuis le menu Fichier

### Performances

- **Cache de géométrie** au format binaire `.emesh` : un modèle importé une fois se recharge sans repasser par Assimp (de l'ordre de cinq fois plus rapide)
- **Cache de textures** partagé : un même fichier utilisé par cinquante sous-meshes n'est chargé et stocké qu'une seule fois
- Cache d'import global dans `~/.cache/CDGameEngine/`, jetable à tout moment (`rm -rf`)

### Projets

- Une sauvegarde = un **dossier de projet** créé à l'emplacement et au nom de votre choix
- Projet autonome : modèles, textures, matériaux et animations copiés dedans, chemins relatifs dans le `scene.json`
- Le relief du terrain est stocké à part en binaire (`terrain.bin`)
- Machines à états dans `assets/anims/*.animsm` et actions de jeu dans `input.json`, en JSON lisible
- Enregistrement atomique (fichier temporaire puis renommage) : un plantage pendant la sauvegarde ne laisse jamais de fichier à moitié écrit
- Ouverture directe d'un projet en argument de ligne de commande

## Prérequis

### Dépendances système

Sur **Arch Linux** :

```bash
sudo pacman -S --needed base-devel cmake git assimp lua vulkan-icd-loader vulkan-intel nvidia-utils
```

Détail :

- `base-devel`, `cmake`, `git` — outils de compilation (`git` est **indispensable** : voir plus bas)
- `assimp` — chargeur de modèles 3D
- `lua` — moteur de script
- `vulkan-icd-loader` — loader Vulkan
- `vulkan-intel`, `nvidia-utils` — pilotes GPU (adapter selon la carte : `vulkan-radeon` pour AMD, etc.)

Recommandé pour le développement : `vulkan-validation-layers`, qui signale les erreurs Vulkan dans le terminal. Sans elles, le moteur démarre quand même, avec un simple avertissement.

Il faut aussi le **SDK Vulkan** (headers + `glslc` pour compiler les shaders) : soit le SDK officiel installé à la main avec son environnement sourcé, soit `sudo pacman -S --needed vulkan-headers shaderc`.

Optionnel : `clang`, pour le script d'analyse des avertissements (voir plus bas).

### Dépendances gérées automatiquement (rien à installer)

- **GLFW** et **GLM** sont téléchargés et compilés par CMake (via `FetchContent`) — c'est pour ça que `git` est requis.
- **ImGui** (branche docking), **ImGuizmo**, **ImGuiFileDialog** et **stb_image** sont embarqués dans `engine/third_party/`.

## Compilation et lancement

### Méthode rapide

```bash
./run.sh                      # compile les shaders, construit, lance
./run.sh ~/Desktop/MonNiveau  # ... et ouvre directement un projet
```

Le script configure CMake s'il n'existe pas encore de dossier `build/`, compile automatiquement **tous** les shaders du dossier `shaders/`, journalise la sortie dans `logs/` (un fichier horodaté par lancement) et s'arrête en affichant l'erreur si quelque chose échoue. Lancé par double-clic, il s'ouvre de lui-même dans un terminal.

### Méthode manuelle

**1. Compiler les shaders en SPIR-V** — CMake ne le fait pas :

```bash
cd shaders
for f in *.vert *.frag; do
    case "$f" in
        shader.vert) glslc "$f" -o vert.spv ;;
        shader.frag) glslc "$f" -o frag.spv ;;
        *)           glslc "$f" -o "${f%.*}_${f##*.}.spv" ;;
    esac
done
cd ..
```

> À refaire à chaque modification d'un shader. Un oubli = les changements n'apparaissent pas.

**2. Construire et lancer :**

```bash
cmake -S . -B build
cmake --build build
./build/bin/game            # ou : ./build/bin/game ~/Desktop/MonNiveau
```

> Après une réinstallation système, un renommage du dossier ou un clonage du dépôt, supprimer d'abord un éventuel ancien dossier de build : `rm -rf build`. CMake y mémorise des chemins absolus.

Sur PC portable hybride (Optimus), pour forcer le GPU dédié : `prime-run ./build/bin/game`.

### Analyse avec Clang

```bash
./clang-warning.sh          # recompile tout avec Clang et résume les avertissements
./clang-warning.sh --quick  # ne recompile que ce qui a changé
```

Clang repère des choses que GCC ignore, comme les membres de classe jamais utilisés. Le build vit dans `build-clang/`, jetable, et n'affecte pas `./run.sh`.

## Premiers pas

Au lancement, une fenêtre demande de **créer ou ouvrir un projet** : les scripts et la sauvegarde en dépendent. Un nouveau projet crée l'arborescence suivante :

```
MonNiveau/
├── assets/
│   ├── models/       # géométrie cuite en .emesh
│   ├── textures/
│   ├── materiaux/
│   └── anims/        # fichiers d'animation et machines à états (.animsm)
├── code/
│   └── main.lua      # script de scène (start / update)
├── scene.json
├── input.json        # actions de jeu
└── terrain.bin       # relief du terrain
```

## Contrôles

Les touches de l'éditeur ci-dessous sont **réassignables** dans **Options → Fenêtres → Contrôles**, au clavier comme à la manette. Celles du jeu se règlent à part, dans **Contrôles du jeu** (voir [Contrôles du jeu](#contrôles-du-jeu)).

### Navigation

| Action | Commande |
|---|---|
| Orbiter | Clic droit (ou clic molette) + souris |
| Panoramique | Maj + clic droit + souris |
| Zoom | Molette |
| Centrer sur la sélection | `F` ou `.` du pavé numérique |
| Basculer en caméra libre | `Tab` |
| Se déplacer (caméra libre) | `Z` `Q` `S` `D`, `Espace` / `Maj` |
| Accélérer | `Ctrl` |

### Édition

| Action | Commande |
|---|---|
| Translation / Rotation / Échelle | `W` / `E` / `R` |
| Aligner pendant la manipulation | Maintenir `Maj` |
| Ajouter à la sélection | `Ctrl` + clic dans la Hiérarchie |
| Sélectionner une plage | `Maj` + clic dans la Hiérarchie |
| Dupliquer la sélection | `Ctrl+D` |
| Supprimer la sélection | `Suppr` |
| Parenter un objet | Le glisser sur un autre dans la Hiérarchie |
| Détacher un objet | Le glisser dans la zone vide en bas de la Hiérarchie |
| Tout désélectionner | `Échap` |
| Annuler / Rétablir | `Ctrl+Z` / `Ctrl+Y` |
| Saisir une valeur dans un curseur | `Ctrl` + clic |
| Quitter | Menu **Fichier**, ou la croix de la fenêtre |

### Timeline d'animation

| Action | Commande |
|---|---|
| Positionner la tête de lecture | Clic gauche dans la piste |
| Poser un notify | Clic droit dans la piste (avec le nom saisi dans *Notify name*) |
| Déplacer un notify | Le glisser |
| Régler ou supprimer un notify | Le sélectionner, puis *Notify time* / *Delete notify* |

### Sculpt du terrain

Cocher *Activer le sculpt* dans le panneau Terrain, puis : **clic gauche** pour lever la matière, **clic droit** pour creuser (mode Monter/Descendre), **molette** pour le rayon du pinceau.

## Matériaux

Le mode **Éditeur de matériaux** (barre de menus) affiche une sphère de prévisualisation sur l'environnement HDR. On y crée un matériau, on lui charge ses cartes, on règle ses facteurs, et on coche les objets auxquels l'appliquer.

Les textures doivent être **tileables** — [ambientCG](https://ambientcg.com) et [Poly Haven](https://polyhaven.com) en proposent de bonnes gratuitement. Dans une archive ambientCG, le fichier `_Color` correspond à la couleur de base, `_NormalGL` à la normale, `_Roughness` à la rugosité et `_AmbientOcclusion` à l'occlusion.

Pour l'environnement, il faut une image **équirectangulaire** ; privilégier un `.hdr` en 1K ou 2K (en flottant, chaque pixel pèse 16 octets, donc un 8K occuperait plus de 500 Mo de mémoire vidéo).

## Animation

Le mode **Éditeur d'animation** isole le personnage sélectionné et affiche sa timeline.

1. Sélectionner un objet **doté d'un squelette** en mode Scène, puis basculer dans l'éditeur d'animation.
2. **Charger une animation** depuis le panneau Animation.
3. Choisir le clip dans la liste, puis contrôler la lecture depuis la timeline. Un clic dans la piste positionne la tête de lecture image par image. La durée du fondu entre deux clips se règle avec *Blend time*.

Les animations gratuites de [Mixamo](https://www.mixamo.com) fonctionnent directement. Exporter en **FBX Binary**, 30 images par seconde, sans réduction de clés ; choisir *With Skin* pour le personnage et *Without Skin* pour les animations suivantes. Cocher *In Place* tant que le moteur ne gère pas le déplacement issu de l'animation.

Le personnage et ses animations doivent partager la même nomenclature d'os : le moteur associe les canaux **par nom**, sans retargeting.

### Notifies

Un notify est un événement nommé placé à un instant précis d'un clip : un pas, un coup qui touche, le début d'une fenêtre d'invincibilité… En mode Play, chaque notify franchi par la lecture appelle `onNotify` dans le script de l'objet **et** dans le script de scène, qui reçoit ainsi les notifies de tous les objets. Les notifies sont enregistrés avec le clip dans le projet.

### Machine à états

La machine à états décide quel clip jouer ; le script se contente de lui fournir des paramètres. Le gameplay reste dans le code, la chorégraphie devient de la donnée.

- Un **état** désigne un clip par son nom, avec sa vitesse et son mode boucle.
- Une **transition** relie deux états (ou part de n'importe quel état) avec une durée de fondu. Elle se déclenche quand **toutes** ses conditions sont vraies, éventuellement seulement à la fin du clip en cours.
- Un **paramètre** est un booléen, un flottant ou un trigger (un booléen remis à faux après usage).

Une machine est un **asset partagé**, comme un matériau : plusieurs personnages peuvent l'utiliser, et la modifier les met tous à jour. Chacun garde son propre état courant et ses propres valeurs de paramètres. `Ctrl+D` sur un personnage donne une copie qui partage sa machine.

Pour en créer une : en mode Éditeur d'animation, sélectionner le personnage, puis dans le panneau **Machine à états**, cliquer sur **Nouveau**. On y ajoute ensuite :

1. les **paramètres** que le script pilotera (`speed` en Float, par exemple) ;
2. les **états**, chacun avec son clip, choisi dans la liste des clips du personnage ; le bouton radio désigne l'état d'entrée ;
3. les **transitions** et leurs conditions. Elles sont testées **de haut en bas** et la première valide l'emporte : les flèches ↑ ↓ règlent leur priorité.

La machine n'avance qu'en mode Play ; l'état courant s'y allume en vert. Un clip introuvable ou une condition dont le paramètre a été supprimé s'affichent en rouge. Une condition orpheline est toujours **fausse** : la transition est bloquée plutôt que déclenchée par erreur.

Chaque machine est enregistrée dans `assets/anims/<nom>.animsm`, un JSON lisible où états et paramètres sont désignés par leur nom. Une machine supprimée ou renommée ne voit son ancien fichier effacé qu'au prochain enregistrement.

## Contrôles du jeu

Le panneau **Options → Fenêtres → Contrôles du jeu** définit les actions que les scripts utilisent. Un nouveau projet reçoit `move_forward`, `move_backward`, `move_left`, `move_right`, `jump`, `sprint`, `interact`, `attack` et `pause`, mais ce ne sont que des valeurs de départ : on peut en ajouter, les renommer ou les supprimer librement.

Chaque action accepte une touche, une seconde touche, un bouton de manette et un stick ou une gâchette. **Clic** sur une liaison pour l'assigner, **clic droit** pour l'effacer. Les touches désignent une **position** sur le clavier : la touche `W` par défaut est le `Z` d'un clavier AZERTY.

Le script n'utilise que le **nom** de l'action, si bien qu'il fonctionne au clavier comme à la manette sans modification. Renommer une action impose donc de mettre à jour les scripts qui l'utilisent ; un nom inconnu est signalé dans la Console.

Les actions sont enregistrées dans `input.json`. `Échap` sert à annuler une assignation et ne peut donc pas être capturé : il est déjà affecté à `pause` par défaut.

## Scripting

Chaque objet peut recevoir un script depuis l'Inspecteur. Un script reçoit `self`, l'index de son objet (`-1` pour le script de scène), et peut définir trois fonctions, toutes facultatives :

```lua
function start()
    Engine.log("Objet " .. self .. " pret")
end

function update(dt)
    -- -1..1 : progressif au stick, tout-ou-rien au clavier
    local dx = Engine.getAxis("move_left", "move_right")
    local dy = Engine.getAxis("move_backward", "move_forward")

    local x, y, z = Engine.getPosition(self)
    Engine.setPosition(self, x + dx * 3 * dt, y + dy * 3 * dt, z)

    Engine.setFloat(self, "speed", math.sqrt(dx * dx + dy * dy) * 5)

    if Engine.wasActionPressed("jump") then
        Engine.setTrigger(self, "jump")   -- la machine doit avoir un trigger "jump"
    end
end

function onNotify(name, objectIndex)
    if name == "hit" then
        Engine.log("Coup porte par l'objet " .. objectIndex)
    end
end
```

### API `Engine`

| Fonction | Rôle |
|---|---|
| `Engine.count()` | Nombre d'objets dans la scène |
| `Engine.getPosition(i)` / `Engine.setPosition(i, x, y, z)` | Position de l'objet `i` |
| `Engine.getRotation(i)` / `Engine.setRotation(i, x, y, z)` | Rotation en degrés (angles d'Euler) |
| `Engine.isActionDown("jump")` | Action maintenue (clavier ou manette) |
| `Engine.wasActionPressed("jump")` | Action appuyée **à cette image** (une seule fois par appui) |
| `Engine.wasActionReleased("jump")` | Action relâchée à cette image |
| `Engine.getActionValue("sprint")` | Valeur de 0 à 1, progressive au stick ou à la gâchette |
| `Engine.getAxis("move_left", "move_right")` | Valeur de -1 à 1 : la seconde action moins la première |
| `Engine.isKeyDown("W")` | Touche brute du clavier ; à réserver au débogage, les actions sont préférables |
| `Engine.log("texte")` | Écrit dans la Console de script |
| `Engine.setBool(i, "nom", valeur)` | Paramètre booléen de la machine à états |
| `Engine.setFloat(i, "nom", valeur)` | Paramètre flottant de la machine à états |
| `Engine.setTrigger(i, "nom")` | Déclenche un trigger de la machine à états |
| `Engine.getAnimState(i)` | Nom de l'état d'animation courant (`nil` si aucun) |

Les scripts sont rechargés à chaque **Play** : il suffit de faire Stop puis Play pour tester une modification. Les erreurs s'affichent dans la Console de script ; un script en erreur est désactivé jusqu'au prochain Play.

## Traductions

L'interface démarre en anglais ; **Options → Language → Français** charge `lang/fr.txt`. Le choix est mémorisé dans `preferences.txt`.

Le format est volontairement simple — une ligne `texte_anglais=traduction`, `#` pour un commentaire. Ajouter une langue revient donc à déposer un fichier `lang/xx.txt` et une entrée dans le menu : aucune recompilation n'est nécessaire. Une clé absente du fichier s'affiche telle quelle en anglais, jamais d'erreur.

## Structure du projet

```
CDGameEngine/
├── engine/
│   ├── include/
│   ├── src/          # core, renderer, scene, script, ui
│   └── third_party/  # ImGui, ImGuizmo, ImGuiFileDialog, stb_image
├── game/             # exécutable
├── shaders/          # sources GLSL + .spv générés
├── lang/             # fichiers de traduction
├── assets/
├── run.sh
├── clang-warning.sh
└── CMakeLists.txt
```

## Feuille de route

1. **Physique et collisions** avec Jolt Physics, puis un contrôleur de personnage — le prochain grand chantier
2. **Modes de caméra de jeu** : troisième personne, première personne, vue de dessus
3. **Navigateur d'assets** : dossiers, groupes et onglets par type (modèles, matériaux, animations)
4. **Lumières** ponctuelles et projecteurs
5. **Sons**
6. **Végétation** au pinceau, **effets** et particules, **HUD** en jeu
7. **Cinématiques**
8. **Fenêtre de démarrage** dédiée, et à plus long terme les jeux 2D

Plus tard : le déplacement issu de l'animation (root motion) et les cascaded shadow maps pour les grands terrains.

## Licence

Le code du moteur est distribué sous **GNU GPL v3** (voir `LICENSE`). Les bibliothèques embarquées ou téléchargées conservent leurs propres licences, toutes permissives (MIT, zlib…).

## Notes

- `build/`, `build-release/`, `build-clang/`, les `.spv`, `logs/`, `imgui.ini` et `preferences.txt` sont régénérables ou personnels : ils ne sont pas versionnés (voir `.gitignore`).
- GLFW et GLM étant récupérés par CMake, une connexion Internet est nécessaire à la première configuration.
- Les assets fournis avec un moteur doivent être librement redistribuables. Le contenu créé par Epic (mannequin d'Unreal, MetaHumans, Megascans) est licencié pour un usage avec Unreal Engine uniquement et n'a donc pas sa place ici.
- Le personnage de test provient de Mixamo et appartient à Adobe. Il sert uniquement à tester les fonctionnalités et doit être remplacé avant toute distribution.

Ce README est rédigé avec l'aide d'une IA.