
# AGENT_FUSION.md – Firmware All-in-One Freenove

## Objectif

Fusionner les rôles audio, scénario, UI, stockage et gestion hardware dans un firmware unique pour le Media Kit Freenove, avec structure modulaire, traçabilité agent, et conformité aux specs suivantes :

### Specs cibles (intégrées depuis la demande produit)
- Lecteur Audio
- Appareil photo / vidéo
- Dictaphone
- Chronomètre / minuteur
- Lampe de poche
- Calculatrice
- Webradio enfants
- Podcast enfants
- Lecteur de QR code
- Émulateur de jeux vidéo
- Lecteur de livres audio
- Application de dessin
- Application de coloriage
- Application de musique pour enfants
- Application de yoga pour enfants
- Application de méditation pour enfants
- Application de langues pour enfants
- Application de mathématiques pour enfants
- Application de sciences pour enfants
- Application de géographie pour enfants

### Stack technique (contraintes validées)
- Réseau local: support Bonjour/mDNS pour découverte automatique des appareils sur le LAN.
- Partage réseau: transfert simple de fichiers entre appareils (workflow enfant-friendly, sans configuration complexe).
- Plateforme centrale: ESP32-S3 pour orchestrer OS embarqué, apps et services runtime.
- Architecture UI: interface simple, lisible et robuste pour un usage enfant.

### Direction UX/UI (contraintes design)
- Univers visuel: inspiration Amiga (codes rétro, ambiance demoscene, identité nostalgique).
- Ergonomie enfant: icônes colorées, navigation explicite, retours visuels immédiats.
- Animation: transitions et micro-animations ludiques mais non bloquantes pour la loop runtime.
- Lisibilité: textes courts, hiérarchie claire, actions principales accessibles en 1-2 interactions.

### Critères de livraison (minimum viable)
- Chaque app doit être accessible depuis la navigation du scénario ou un écran dédié.
- Chaque app doit avoir un fallback si ressource manquante (affichage, son, erreur claire).
- Les fonctionnalités sensibles (caméra, audio, réseau) doivent respecter la topologie de ressources existante (`ResourceCoordinator`) et éviter la contention.
- Les assets doivent être livrés via LittleFS avec structure de dossiers par app.
- Les logs d’exécution doivent tracer ouverture/fermeture d’écran, erreurs de ressources, et actions critiques utilisateur.

### Plan d’intégration complète (couverture specs)

- Fichiers de scènes et écrans individuels, stockés sur LittleFS (data/) ✔️
- Navigation UI dynamique (LVGL, écrans générés depuis fichiers) ✔️
- Exécution de scénarios (lecture, transitions, actions, audio) ✔️
- Gestion audio (lecture/stop, mapping fichiers LittleFS) ✔️
- Gestion boutons et tactile (événements, mapping, callbacks) ✔️
- Fallback robuste si fichier manquant (scénario par défaut) ✔️
- Génération de logs et artefacts (logs/, artifacts/) ✔️
- Validation hardware sur Freenove (affichage, audio, boutons, tactile) ⏳
- Documentation et onboarding synchronisés ⏳
- Intégration des apps enfant (audio, utilitaires, médias, pédagogiques) ⏳
- Orchestration caméra/vidéo + dictaphone selon profils ressources ⏳
- Intégration Bonjour/mDNS + partage fichiers inter-appareils ⏳
- Refonte UX enfant avec direction visuelle Amiga ⏳

### Structure modulaire

- audio_manager.{h,cpp} : gestion audio
- scenario_manager.{h,cpp} : gestion scénario
- ui_manager.{h,cpp} : gestion UI dynamique (LVGL)
- button_manager.{h,cpp} : gestion boutons
- touch_manager.{h,cpp} : gestion tactile
- storage_manager.{h,cpp} : gestion LittleFS, fallback
- app_registry.{h,cpp} : catalogue des applications disponibles, permissions, metadata
- app_audio_gallery.{h,cpp} : lecteur audio + livres audio + podcasts + webradio
- app_camera.{h,cpp} : appareil photo/vidéo + QR code
- app_utils.{h,cpp} : chronomètre, calculatrice, lampe de poche
- app_kids.{h,cpp} : jeux, dessin, coloriage, musique/yoga/méditation/langues/math/sciences/géographie
- app_file_share.{h,cpp} : découverte Bonjour + transfert fichiers simple inter-appareils
- ui_kids_shell.{h,cpp} : shell UX enfant (grille icônes, transitions ludiques, thème Amiga)

### Points critiques à valider

- Robustesse du fallback LittleFS
- Synchronisation UI/scénario/audio
- Mapping dynamique boutons/tactile
- Logs d’évidence et artefacts produits

### Livraison partielle déjà intégrée (code)
- Runtime apps unifié (`AppRegistry`, `AppRuntimeManager`, endpoints `/api/apps/*`)
- Gating capacités explicites (`CAP_*`) et arbitrage avant lancement
- Actions scénario applicatives (`open_app`, `close_app`, `app_action`)
- Socle Bonjour/mDNS + partage de fichiers local (`/api/share/*`)
- Spécs techniques sérialisées (`specs/apps/*.schema.json`, registre exemple, thème UX enfant Amiga)

Voir AGENT_TODO.md pour le suivi détaillé et la progression.
