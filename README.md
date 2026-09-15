# Système de présence par empreinte digitale

Architecture ESP32, R503, application mobile MIT App Inventor

## 1. Objectif du projet

Concevoir un système de présence biométrique permettant à des étudiants de pointer leur présence en cours en posant simplement leur doigt sur un lecteur d'empreintes digitales. La gestion et le pilotage du lecteur biométrique sont assurés par une application mobile développée avec MIT App Inventor, communiquant directement avec le module ESP32, sans serveur web complet.

## 2. Composants du projet

- un lecteur d'empreintes digitales R503, chargé de la capture et de la reconnaissance des empreintes
- un microcontrôleur ESP32, programmé en Arduino C++, qui pilote le R503 et sert de point de communication avec l'application mobile
- une liaison série UART entre l'ESP32 et le R503
- une application mobile développée avec MIT App Inventor, permettant de commander le lecteur (enregistrement, suppression, suppression totale, consultation d'état)
- une communication entre l'application mobile et l'ESP32 via le réseau Wi-Fi local
- un écran OLED

Le modèle exact de l'écran OLED, son interface (I2C ou SPI) et sa bibliothèque d'affichage ne sont pas précisés dans le document de conception initial. Un modèle I2C type SSD1306 128x64 est le choix le plus courant avec un ESP32, mais reste à confirmer.

## 3. Architecture générale

```
        Smartphone Android
     (Application App Inventor)
                |  Wi-Fi (HTTP)
                v
            ESP32   ------->   Écran OLED
                |  UART
                v
              R503
```

| Composant | Rôle principal |
|---|---|
| R503 | Capture l'image de l'empreinte, la convertit en gabarit, effectue la reconnaissance en interne, retourne l'ID à l'ESP32 |
| ESP32 | Pilote le R503 via UART, reçoit les commandes de l'application mobile, retourne les résultats |
| Application MIT App Inventor | Interface mobile pour déclencher les opérations : enregistrement, suppression, suppression totale, consultation d'état |
| Écran OLED | Affiche les messages de succès, d'erreur, et le mode courant du lecteur |

Le smartphone ne communique jamais directement avec le R503. L'ESP32 est le seul composant qui parle aux trois autres, il fait office de passerelle centrale.

## 4. Matériel nécessaire

- ESP32 (n'importe quel modèle avec Wi-Fi et au moins un port UART matériel disponible en plus du port de programmation)
- Lecteur d'empreintes R503
- Écran OLED (modèle à définir)
- Câbles de connexion, alimentation

## 5. Câblage

### 5.1 ESP32 vers R503 (UART)

| R503 | ESP32 |
|---|---|
| VCC | 3,3V |
| GND | GND |
| TX | RX (UART matériel, par exemple GPIO16 pour Serial2) |
| RX | TX (UART matériel, par exemple GPIO17 pour Serial2) |
| WAKEUP (optionnel) | GPIO libre |
| 3,3V-T (alimentation anneau tactile) | selon module |

Le R503 possède un connecteur à 6 broches. Les couleurs de fils varient selon le fournisseur, il faut vérifier la correspondance exacte sur la datasheet de l'exemplaire utilisé.

Le R503 fonctionne en 3,3V. Certaines sources en ligne peu fiables évoquent une alimentation en 5V, mais la documentation officielle et les tutoriels sérieux confirment tous 3,3V. Ne pas alimenter en 5V sans avoir vérifié la datasheet précise du module utilisé.

Utiliser un port UART matériel de l'ESP32 (Serial2 par exemple) plutôt qu'une émulation logicielle, pour une communication plus fiable.

Le connecteur WAKEUP permet de détecter la présence d'un doigt sans interroger le capteur en continu. Il peut être laissé non connecté si cette optimisation n'est pas nécessaire.

### 5.2 ESP32 vers écran OLED

Non précisé dans le document de conception. Si un OLED I2C est retenu, le câblage type est : VCC vers 3,3V, GND vers GND, SDA et SCL vers deux GPIO dédiées à l'I2C de l'ESP32. À confirmer selon le modèle d'écran choisi.

## 6. Programmation du R503

- Installer la bibliothèque Adafruit_Fingerprint (compatible R503) dans l'Arduino IDE, ou une bibliothèque dédiée ESP32 si un accès plus bas niveau aux commandes est souhaité
- Initialiser le port série matériel :
```cpp
Serial2.begin(57600, SERIAL_8N1, PIN_RX, PIN_TX);
```
- Créer l'objet fingerprint sur ce port et appeler `verifyPassword()` au démarrage pour confirmer que la communication fonctionne
- Fonctions principales du R503 à utiliser : capture d'image, conversion en caractéristiques (character file), recherche (search), stockage d'un gabarit, suppression d'un gabarit, effacement total de la base interne
- Le fingerprint ID correspond à l'emplacement mémoire interne du R503, c'est un simple entier propre au module
- Le score de confiance est une valeur retournée lors d'une recherche, indiquant le degré de similarité entre l'empreinte capturée et le gabarit trouvé
- Ne pas présenter comme un fait établi que le format de gabarit du R503 est conforme à la norme ISO/IEC 19794-2, ce format dépend du protocole propriétaire et du firmware du module
- Tester isolément un enrôlement puis une recherche avant toute intégration dans le firmware complet

## 7. Architecture logicielle du firmware ESP32

Le firmware peut être structuré en modules distincts :

- module de connectivité : établissement de la connexion Wi-Fi en mode client
- module de communication R503 : envoi des commandes UART et interprétation des réponses du lecteur
- module d'écoute des commandes : réception des instructions de l'application mobile via un serveur HTTP embarqué
- gestion des modes : bascule entre veille, recherche, enregistrement, suppression
- traitement des commandes : interprétation des requêtes reçues (ENROLL, DELETE, DELETE_ALL, STATUS) et appel des fonctions correspondantes
- gestion des erreurs : détection des échecs de communication, retour d'un message exploitable par l'application et l'écran OLED

Le format exact des trames UART entre l'ESP32 et le R503 (jeu d'instructions, structure des trames) dépend de la bibliothèque retenue et n'est pas détaillé dans le document de conception initial.

## 8. Logique métier détaillée

### 8.1 Enregistrement d'une empreinte

1. L'utilisateur appuie sur le bouton Enregistrer dans l'application
2. L'ESP32 récupère le dernier ID utilisé
3. L'ESP32 calcule le nouvel ID à attribuer (dernier ID + 1)
4. L'ESP32 lance une commande d'enrollment sur le R503, avec le nouvel ID
5. L'étudiant pose son doigt sur le capteur, en suivant la procédure de double capture requise par le R503
6. Le R503 génère un gabarit et le stocke à l'emplacement correspondant à l'ID
7. Le R503 retourne un code de résultat à l'ESP32
8. L'ESP32 transmet le résultat à l'application et à l'écran OLED

Exemple :
```
Dernier ID connu : 24
Nouvel ID calculé : 25
ESP32 -> R503 : commande d'enrollment sur l'emplacement 25
R503 -> ESP32 : succès
ESP32 -> Application : OK, enregistrement fait à l'emplacement 25
ESP32 -> Écran OLED : Enregistrement fait à l'emplacement 25
```

### 8.2 Suppression d'une empreinte précise

1. L'utilisateur saisit l'ID de l'empreinte à supprimer dans l'application
2. L'utilisateur appuie sur Supprimer
3. L'application envoie une commande de suppression ciblée à l'ESP32 (par exemple DELETE:25)
4. L'ESP32 transmet la commande au R503, qui efface le gabarit correspondant
5. L'application et l'écran OLED affichent une confirmation, en précisant l'ID

### 8.3 Suppression totale

1. L'utilisateur appuie sur Effacer toute la mémoire
2. L'application affiche un message de confirmation explicite avant d'envoyer la commande, en raison du caractère irréversible de l'opération
3. Après confirmation, l'application envoie la commande DELETE_ALL
4. L'ESP32 transmet la commande au R503, qui efface l'intégralité des gabarits
5. L'application et l'écran OLED confirment la réinitialisation complète

La commande de suppression totale doit impérativement être protégée par une confirmation explicite côté application, pour éviter toute suppression accidentelle.

### 8.4 Consultation de l'état du lecteur

L'application peut interroger l'ESP32 pour connaître :
- la disponibilité du lecteur (connecté et opérationnel, ou en erreur)
- le nombre d'empreintes actuellement enregistrées
- le dernier résultat d'opération

Exemple d'échange en mode Wi-Fi / HTTP :
```
GET /status HTTP/1.1
Host: 192.168.1.50

Réponse :
{
  "lecteur": "pret",
  "empreintes_enregistrees": 12
}
```

## 9. Application MIT App Inventor

### 9.1 Interface (Designer)

- un écran principal (Screen1)
- boutons : Enregistrer, Supprimer, Supprimer tout, Vérifier l'état
- un champ de saisie pour l'ID concerné par une suppression (l'enregistrement n'exige pas de saisie manuelle, l'ID est calculé automatiquement côté ESP32)
- un composant Web (non visible), pour l'envoi des requêtes HTTP vers l'ESP32
- une zone d'affichage des résultats et messages de confirmation ou d'erreur
- un composant Notifier, pour la boîte de confirmation avant suppression totale

### 9.2 Logique par blocs

Bouton Enregistrer :
```
QUAND Bouton_Enregistrer.Click
FAIRE
   mettre à jour Web1.Url avec l'URL de commande d'enregistrement
   appeler Web1.Get
```

Réception de la réponse :
```
QUAND Web1.GotText (réponse)
FAIRE
   SI réponse contient "OK" ALORS
      afficher le message de succès avec l'ID reçu
   SINON
      afficher un message d'échec
```

Bouton Supprimer : récupérer l'ID saisi dans le champ de texte, l'inclure dans l'URL, appeler Web1.Get.

Bouton Supprimer tout : ouvrir Notifier1.ShowChooseDialog pour demander confirmation, n'envoyer la commande DELETE_ALL que si l'utilisateur confirme.

Bouton Vérifier l'état : requête GET vers /status, afficher le contenu JSON reçu.

### 9.3 Point de vigilance réseau

L'adresse IP de l'ESP32 en Wi-Fi doit rester stable pour que l'application puisse toujours le joindre. Il est recommandé de fixer une IP statique côté ESP32, ou de faire une réservation DHCP sur le routeur, sinon l'adresse peut changer après un redémarrage du réseau.

Le composant Web fonctionne bien avec des requêtes GET simples. Si des données plus complexes doivent être envoyées à l'avenir (par exemple un JSON en POST), utiliser Web1.PostText plutôt que Web1.Get.

## 10. Tests et intégration

1. Tester le R503 seul (enrôlement, recherche) avant toute intégration réseau
2. Tester le serveur HTTP de l'ESP32 seul, avec un navigateur ou un outil comme curl, avant de brancher l'application
3. Tester l'écran OLED seul (affichage statique) avant de le relier à la logique métier
4. Tester l'application MIT App Inventor bouton par bouton
5. Assembler l'ensemble et tester le scénario complet : enregistrement, consultation, suppression

## 11. Limites et évolutions

- capacité limitée du lecteur : le R503 ne peut stocker qu'un nombre restreint de gabarits, ce qui borne le nombre d'étudiants gérables
- absence de persistance centralisée : sans base de données ni serveur, l'historique des présences n'est pas conservé, seules les opérations de gestion du lecteur sont couvertes
- dépendance à la proximité ou au réseau local : le mode Wi-Fi nécessite que le smartphone et l'ESP32 partagent le même réseau
- dépendance au format et au protocole propriétaire du R503
- inadaptation à une gestion multi-salles ou à grande échelle sans évolution de l'architecture

Évolution proposée : faire cohabiter cette application mobile de pilotage local avec un serveur central (par exemple une API REST) qui recevrait également les événements de présence, afin de centraliser les données de plusieurs salles ou lecteurs. L'application MIT App Inventor pourrait alors être conservée comme outil de gestion rapide et local, en complément d'une solution serveur plus complète.
