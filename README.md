# Forêt Enchantée 3D — version C++ / OpenGL 3.3

Portage fidèle du « Studio Forêt Enchantée 3D » (Three.js) en **C++17 + OpenGL 3.3 core**.
Mêmes modèles procéduraux.


## Ce qui est reproduit

- **Terrain** ondulé 80×80 (64×64 segments) avec texture de mousse / feuilles mortes procédurale
- **Sapins** (tronc + 3 cônes facettés) et **chênes** (tronc + dodécaèdre facetté), 35 arbres aléatoires
- **Écorce PBR** : texture + normal map générées par code (comme les `canvas` du HTML)
- **Champignons bioluminescents** (cyan / violet) émissifs + lumières ponctuelles pulsantes
- **Feu de camp** : 8 pierres, 3 bûches, flamme filaire orange, lumière vacillante
- **Éclairage** physique identique à Three.js r160 (Lambert/π + GGX, hémisphérique, directionnelle, 3 ponctuelles)
- **Ombres** : soleil/lune (carte 2048², PCF) et **ombres omnidirectionnelles du feu de camp** (cubemap)
- **Brouillard exponentiel** `FogExp2` (densité 0.02), 4 ambiances : Jour / Coucher / Nuit / Aube
- **Vent** (balancement du feuillage), **lucioles** (150, additives), **pluie douce** (1200 gouttes)
- **Bloom** type UnrealBloomPass (high-pass + 5 flous gaussiens + composite, rendu HDR RGBA16F)
- **Caméra orbitale** avec inertie + 4 vues prédéfinies à transition fluide, **clic sur un objet** pour l'inspecter

## Compilation

Prérequis : un compilateur C++17, **CMake ≥ 3.14**, un GPU compatible OpenGL 3.3.
GLM et glad sont **inclus** dans `external/`. GLFW est utilisé s'il est installé, sinon il est téléchargé automatiquement.

### Linux
```bash
sudo apt install build-essential cmake libglfw3-dev   # (facultatif : sinon GLFW est téléchargé)
bash ./build.sh
./build/enchanted_forest
```
> Si GLFW est téléchargé automatiquement, il faut aussi : `sudo apt install xorg-dev libwayland-dev libxkbcommon-dev`.

### macOS
```bash
brew install cmake glfw
./build.sh
./build/enchanted_forest
```

### Windows (Visual Studio 2019/2022 ou MinGW)
```bat
build.bat
build\Release\enchanted_forest.exe
```

## Commandes

| Action | Contrôle |
|---|---|
| Orbite / déplacement / zoom | Souris gauche / souris droite / molette |
| Inspecter un arbre, un champignon, le feu | Clic gauche (le nom s'affiche dans la barre de titre et la console) |
| Ambiance Jour · Coucher · Nuit · Aube | `1` `2` `3` `4` |
| Vue Canopée · Sentier · Feu de camp · Champignons | `5` `6` `7` `8` |
| Vent · Lucioles · Pluie | `W` · `L` · `R` |
| Intensité de la lumière | `↑` / `↓` |
| Éclat du bloom | `←` / `→` |
| Densité du brouillard | `PgUp` / `PgDn` |
| Plein écran / quitter / aide | `F` ou `F11` / `Échap` / `H` |

## Options en ligne de commande

```
enchanted_forest [--size 1920x1080] [--time day|sunset|night|dawn]
                 [--preset overview|trail|campfire|mushrooms] [--rain] [--seed N]
                 [--screenshot image.ppm [--frames N]]
```

## Structure

```
src/main.cpp       application : scène, lumières, caméra, particules, bloom, entrées
src/geometry.hpp   cylindre, cône, dodécaèdre, calotte, terrain (algorithmes Three.js)
src/textures.hpp   textures procédurales (écorce, normal map, sol moussu)
src/shaders.hpp    tous les shaders GLSL 330 (PBR, ombres, brouillard, particules, bloom)
external/glad      chargeur OpenGL 3.3 core (inclus)
external/glm       mathématiques (inclus)
```

## Dépannage

- *« Impossible de créer la fenêtre OpenGL 3.3 »* : mettez à jour le pilote graphique (ou, sur machine virtuelle, activez l'accélération 3D).
- Trop lent sur un petit GPU : réduisez `SUN_SIZE` / `CUBE_SIZE` dans `src/main.cpp`.
