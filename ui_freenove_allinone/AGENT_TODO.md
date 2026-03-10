## Validation hardware – TODO détaillée

- [ ] Compiler et flasher le firmware sur le Freenove Media Kit
- [ ] Préparer les fichiers de test sur LittleFS (/data/scene_*.json, /data/screen_*.json, /data/*.wav)
- [ ] Vérifier l’affichage TFT (boot, écran dynamique, transitions)
- [ ] Tester la réactivité tactile (zones, coordonnées, mapping)
- [ ] Tester les boutons physiques (appui, mapping, transitions)
- [ ] Tester la lecture audio (fichiers présents/absents, fallback)
- [ ] Observer les logs série (initialisation, actions, erreurs)
- [ ] Générer et archiver les logs d’évidence (logs/)
- [ ] Produire un artefact de validation (artifacts/)
- [ ] Documenter toute anomalie ou limitation hardware constatée

## Revue finale – Checklist agents

- [ ] Vérifier la cohérence de la structure (dossiers, modules, scripts)
- [ ] Relire AGENT_FUSION.md (objectifs, couverture specs, structure)
- [ ] Relire README.md (usage, build, validation, modules)
- [ ] Relire la section onboarding (docs/QUICKSTART.md, etc.)
- [ ] Vérifier la présence et la robustesse du fallback LittleFS
- [ ] Vérifier la traçabilité des logs et artefacts
- [ ] Vérifier la synchronisation UI/scénario/audio
- [ ] Vérifier la gestion dynamique des boutons/tactile
- [ ] Vérifier la non-régression sur les autres firmwares (split)

# TODO Agent – Firmware All-in-One Freenove


## Plan d’intégration détaillé (COMPLÉTÉ)

- [x] Vérifier la présence du scénario par défaut sur LittleFS
- [x] Charger la liste des fichiers de scènes et d’écrans (data/)
- [x] Initialiser la navigation UI (LVGL, écrans dynamiques)
- [x] Mapper les callbacks boutons/tactile vers la navigation UI
- [x] Préparer le fallback LittleFS si fichier manquant
- [x] Logger l’initialisation (logs/)

- [x] Boucle principale d’intégration
	- [x] Navigation UI (LVGL, écrans dynamiques)
	- [x] Exécution scénario (lecture, actions, transitions)
	- [x] Gestion audio (lecture, stop, files LittleFS)
	- [x] Gestion boutons/tactile (événements, mapping)
	- [x] Gestion stockage (LittleFS, fallback)
	- [x] Logs/artefacts

## Validation hardware (EN COURS)

- [ ] Tests sur Freenove Media Kit (affichage, audio, boutons, tactile)
- [ ] Génération de logs d’évidence (logs/)
- [ ] Production d’artefacts de validation (artifacts/)

## Documentation

- [ ] Mise à jour README.md (usage, build, structure)
- [ ] Mise à jour AGENT_FUSION.md (règles d’intégration, conventions)
- [ ] Synchronisation avec la doc onboarding principale

## Spécifications fonctionnelles utilisateur à livrer

- [ ] **Lecteur Audio** (lecture locale, play/pause/stop, playlist de base)
- [ ] **Appareil photo / vidéo** (capture image/vidéo, stockage média, aperçu)
- [ ] **Dictaphone** (enregistrement audio local, lecture, suppression)
- [ ] **Chronomètre / minuteur** (UI dédiée, rappel sonore)
- [ ] **Lampe de poche** (commande on/off, intensité si supportée)
- [ ] **Calculatrice** (opérations de base, historique simple)
- [ ] **Webradio enfants** (gestion flux HTTP/ICY, reprise auto minimale)
- [ ] **Podcast enfants** (lecture podcasts local ou RSS simplifié)
- [ ] **Lecteur de QR code** (scan caméra, parsing et action de base)
- [ ] **Émulateur de jeux vidéo** (version minimale ludique compatible mémoire embarquée)
- [ ] **Lecteur de livres audio** (index/chapitres, reprise de progression)
- [ ] **Application de dessin** (canvas tactile + outils minimum)
- [ ] **Application de coloriage** (templates + remplissage couleur)
- [ ] **Application de musique pour enfants** (playlists ciblées, contrôle simple)
- [ ] **Application de yoga pour enfants** (séquences guidées, audio/texte)
- [ ] **Application de méditation pour enfants** (tempos/sons/apaisement)
- [ ] **Application de langues pour enfants** (mini leçons + répétition audio)
- [ ] **Application de mathématiques pour enfants** (exercices interactifs)
- [ ] **Application de sciences pour enfants** (contenus interactifs + quiz)
- [ ] **Application de géographie pour enfants** (quiz/carte/images interactives)

### Plan d’intégration recommandé (ordre de dépendance)

- [ ] 1) Base commune UI + assets + navigation (priorité haute)
- [ ] 2) Audio stack: lecteur audio / livres / podcasts / webradio
- [ ] 3) Outils système: chronomètre, calculatrice, lampe de poche
- [ ] 4) Camera stack: photo/vidéo + QR code
- [ ] 5) Contenus enfants: dessin, coloriage, musique, yoga, méditation, langues, maths, sciences, géographie
- [ ] 6) Jeux: intégrer émulateur léger après stabilisation mémoire/perf

## Stack technique réseau (nouvelle contrainte)

- [ ] Ajouter découverte réseau Bonjour/mDNS (nom appareil + présence services)
- [ ] Ajouter service de transfert simple de fichiers entre appareils
- [ ] Définir protocole minimal de transfert (metadata + chunks + ack + reprise simple)
- [ ] Ajouter endpoints/API de partage (liste appareils, push/pull fichier, état transfert)
- [ ] Journaliser les transferts (début, progression, succès/échec)

## UX/UI enfants + inspiration Amiga (nouvelle contrainte)

- [ ] Définir un thème visuel enfant inspiré Amiga (palette, icônes, typo, motion)
- [ ] Créer écran home "kids shell" (grille d’apps colorée + navigation simple)
- [ ] Ajouter transitions ludiques non bloquantes (sans pénaliser FPS/runtime loop)
- [ ] Standardiser composants UI: bouton, carte app, badge état, feedback action
- [ ] Ajouter règles de lisibilité enfant (texte court, contraste, zones tactiles larges)

## Progression technique réalisée (itération courante)

- [x] Socle apps runtime: registre, lifecycle manager, endpoints `/api/apps/*`
- [x] Contrat capacités runtime: flags explicites et gating au lancement d’app
- [x] Actions scénario app-aware: `open_app`, `close_app`, `app_action`
- [x] Bonjour/mDNS: service publié `_zacus._tcp` + découverte peers
- [x] Partage fichiers simple: endpoints `/api/share/peers`, `/api/share/files`, `/api/share/upload`
- [x] Spécifications JSON: manifest/streams/progress + registre exemple
- [x] Direction UX enfant + Amiga: fichier de thème de référence
