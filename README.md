
Pendant toute la durée de l'enrôlement, `mqttClient.loop()` est appelé régulièrement dans les boucles d'attente (`while` sur `getImage()`) pour que l'ESP32 reste capable de recevoir une annulation même en plein milieu d'une capture.

### 4.6 Autres flux

- **Consultation d'état** : l'app publie sur `esp32/cmd/status_request` (payload vide) ; l'ESP32 répond immédiatement sur `esp32/status` avec un texte formaté (état du capteur, nombre d'empreintes).
- **Suppression ciblée** : l'app publie l'ID à supprimer sur `esp32/cmd/delete` ; l'ESP32 vérifie que l'ID existe (sinon "ID non enregistré"), supprime le gabarit (`deleteModel`) et l'entrée NVS correspondante, publie le résultat.
- **Suppression totale** : l'app publie (payload indifférent) sur `esp32/cmd/delete_all` ; l'ESP32 vide la base du capteur (`emptyDatabase`) et toutes les entrées NVS, remet `lastId` à 0.
- **Présence passive** : en dehors de toute commande explicite, l'ESP32 tourne en continu en mode reconnaissance (`checkPresence`) : dès qu'un doigt connu est posé, il identifie l'utilisateur et publie directement `PRESENCE_OK:nom:ID` sur `esp32/result`, sans action requise depuis l'application.

### 4.7 Reconnexion et robustesse

La fonction `reconnectMQTT()` est appelée en boucle tant que le client n'est pas connecté au broker. Elle génère un identifiant client aléatoire, s'authentifie, puis **réabonne systématiquement aux 5 topics de commande** — un abonnement MQTT ne survit pas à une déconnexion, il doit être refait à chaque reconnexion. Une fois reconnecté, l'ESP32 republie immédiatement son état courant, pour que l'application affiche des informations à jour sans avoir à les redemander.

### 4.8 Côté application (MIT App Inventor)

L'application utilise l'extension **UrsAI2PahoMqtt**, configurée avec l'adresse du broker HiveMQ, le port **8883**, le protocole **SSL**, ainsi que les identifiants MQTT (UserName / UserPassword). Les anciens blocs `Web1` (requêtes HTTP) ont été entièrement remplacés par :
- des blocs **Publish** pour envoyer une commande sur un topic `esp32/cmd/...`
- la réception d'événements **MessageReceived**, déclenchée à chaque message reçu sur les topics `esp32/status` et `esp32/result`, qui met à jour les labels d'état et de résultat correspondants

## 5. Matériel nécessaire

- ESP32 — modèle utilisé : **XIAO ESP32-S3** (Wi-Fi + UART matériel disponible en plus du port de programmation)
- Lecteur d'empreintes R503 (piloté via le module JM-101B)
- Écran OLED SSD1306 128x64 (I2C)
- Boîtier imprimé en 3D
- Batterie interne (alimentation autonome, sans dépendance à une prise secteur)
- Câbles de connexion, alimentation

## 6. Câblage

### 6.1 ESP32 vers R503 (UART)

| R503 (JM-101B) | ESP32 |
|---|---|
| VCC | 3,3V |
| GND | GND |
| TX | RX matériel (GPIO2 dans le firmware actuel) |
| RX | TX matériel (GPIO3 dans le firmware actuel) |
| WAKEUP (optionnel) | GPIO libre |
| 3,3V-T (alimentation anneau tactile) | selon module |

Le firmware utilise un port UART matériel dédié (`HardwareSerial mySerial(1)`) plutôt qu'une émulation logicielle, pour une communication plus fiable.

Le R503 fonctionne en 3,3V. Certaines sources en ligne peu fiables évoquent une alimentation en 5V, mais la documentation officielle et les tutoriels sérieux confirment tous 3,3V. Ne pas alimenter en 5V sans avoir vérifié la datasheet précise du module utilisé.

Le connecteur WAKEUP permet de détecter la présence d'un doigt sans interroger le capteur en continu. Il peut être laissé non connecté si cette optimisation n'est pas nécessaire.

### 6.2 ESP32 vers écran OLED

Écran confirmé : **SSD1306 I2C**, câblé comme suit :

| OLED | ESP32 |
|---|---|
| VCC | 3,3V |
| GND | GND |
| SDA | GPIO dédié (D4 dans le firmware actuel) |
| SCL | GPIO dédié (D5 dans le firmware actuel) |

## 7. Programmation du R503

- Installer la bibliothèque Adafruit_Fingerprint (compatible R503) dans l'Arduino IDE
- Initialiser le port série matériel :
```cpp
mySerial.begin(57600, SERIAL_8N1, JM101B_RX_PIN, JM101B_TX_PIN);
```
- Créer l'objet fingerprint sur ce port et appeler `verifyPassword()` au démarrage pour confirmer que la communication fonctionne
- Fonctions principales du R503 utilisées : capture d'image (`getImage`), conversion en caractéristiques (`image2Tz`), recherche rapide (`fingerFastSearch`), fusion et création de modèle (`createModel`), stockage (`storeModel`), suppression (`deleteModel`), effacement total (`emptyDatabase`)
- Le fingerprint ID correspond à l'emplacement mémoire interne du R503, c'est un simple entier propre au module
- Ne pas présenter comme un fait établi que le format de gabarit du R503 est conforme à la norme ISO/IEC 19794-2, ce format dépend du protocole propriétaire et du firmware du module

## 8. Architecture logicielle du firmware ESP32

Le firmware est structuré en modules distincts :

- module de connectivité Wi-Fi : établissement de la connexion en mode client (DHCP)
- module client MQTT : connexion sécurisée (TLS) au broker HiveMQ Cloud, abonnement aux topics de commande, callback unique de traitement des messages reçus (`mqttCallback`), reconnexion automatique (`reconnectMQTT`)
- module de communication R503 : envoi des commandes UART et interprétation des réponses du lecteur
- module de persistance (NVS) : sauvegarde et lecture des noms associés à chaque ID d'empreinte (`Preferences`), pour afficher un nom lisible plutôt qu'un simple numéro
- gestion des modes : bascule entre veille, recherche, enregistrement (avec confirmation en attente), suppression
- traitement des commandes : interprétation des messages reçus sur les topics `esp32/cmd/...` et appel des fonctions correspondantes
- gestion des erreurs : détection des échecs (capteur non détecté, doublon, timeout de capture), retour d'un message exploitable via `esp32/result` et l'écran OLED

## 9. Logique métier détaillée

### 9.1 Enregistrement d'une empreinte

1. L'utilisateur saisit un nom dans l'application et appuie sur Enregistrer
2. L'application publie ce nom sur `esp32/cmd/enroll_start`
3. L'ESP32 vérifie qu'aucun enrôlement n'est déjà en attente et que le capteur est disponible, retient le nom, affiche "Confirmer sur App ?" sur l'écran OLED et publie `WAITING_CONFIRMATION`
4. L'utilisateur confirme ; l'application publie "continue" sur `esp32/cmd/enroll_confirm`
5. L'ESP32 calcule le nouvel ID (dernier ID connu + 1), lance la procédure de double capture requise par le R503, vérifie l'absence de doublon avant de créer le gabarit
6. Le gabarit est stocké sur le R503, et le nom associé est sauvegardé en mémoire NVS
7. L'ESP32 publie le résultat final sur `esp32/result` et l'affiche sur l'écran OLED
8. L'annulation reste possible à tout moment via "cancel" sur `esp32/cmd/enroll_confirm`

### 9.2 Suppression d'une empreinte précise

1. L'utilisateur saisit l'ID à supprimer et appuie sur Supprimer
2. L'application publie cet ID sur `esp32/cmd/delete`
3. L'ESP32 vérifie que l'ID existe (sinon message d'erreur "ID non enregistré")
4. Le gabarit est supprimé du R503, et l'entrée NVS correspondante est effacée
5. L'ESP32 publie une confirmation précisant le nom concerné, affichée sur l'application et l'écran OLED

### 9.3 Suppression totale

1. L'utilisateur appuie sur Effacer toute la mémoire
2. L'application affiche un message de confirmation explicite avant d'envoyer la commande, en raison du caractère irréversible de l'opération
3. Après confirmation, l'application publie sur `esp32/cmd/delete_all`
4. L'ESP32 efface l'intégralité des gabarits sur le R503 ainsi que toutes les entrées NVS, et remet le dernier ID à zéro
5. L'application et l'écran OLED confirment la réinitialisation complète

### 9.4 Consultation de l'état du lecteur et présence passive

- L'application peut publier sur `esp32/cmd/status_request` pour connaître : la disponibilité du lecteur (connecté et opérationnel, ou en erreur), le nombre d'empreintes actuellement enregistrées
- En dehors de toute commande explicite, l'ESP32 fonctionne aussi en **mode présence passif** : dès qu'un doigt reconnu est posé sur le capteur, il identifie l'utilisateur et publie directement le résultat (`PRESENCE_OK:nom:ID` ou `PRESENCE_REFUSED:Inconnu`) sur `esp32/result`, sans action requise depuis l'application

## 10. Application MIT App Inventor

### 10.1 Interface (Designer)

- un écran principal (Screen1)
- boutons regroupés : Enregistrer, Supprimer, Supprimer tout, Vérifier l'état
- un champ de saisie pour le nom (enrôlement) et un champ pour l'ID (suppression ciblée)
- deux labels séparés : un pour l'état du lecteur (status), un pour le résultat des opérations (result)
- une horloge (Clock) de rafraîchissement automatique
- le composant MQTT (extension **UrsAI2PahoMqtt**, non visible), configuré avec Broker, Port 8883, Protocol SSL, UserName, UserPassword
- un composant Notifier, pour la confirmation avant suppression totale et avant confirmation d'enrôlement

### 10.2 Logique par blocs

- Au démarrage : `Connect` du composant MQTT, puis `Subscribe` aux topics `esp32/status` et `esp32/result`
- Bouton Enregistrer : `Publish` sur `esp32/cmd/enroll_start` avec le nom saisi
- Réception `MessageReceived` : routage selon le contenu du message — `WAITING_CONFIRMATION` déclenche l'ouverture du Notifier de confirmation, qui publie ensuite "continue" ou "cancel" sur `esp32/cmd/enroll_confirm` ; les autres messages (`OK`, `ERROR`, `PRESENCE_...`) mettent à jour le label de résultat
- Réception sur `esp32/status` : mise à jour du label d'état
- Bouton Supprimer : `Publish` de l'ID saisi sur `esp32/cmd/delete`
- Bouton Supprimer tout : ouverture du Notifier de confirmation, puis `Publish` sur `esp32/cmd/delete_all` si confirmé
- Bouton Vérifier l'état : `Publish` (payload vide) sur `esp32/cmd/status_request`

### 10.3 Sécurité et robustesse de la connexion

- Plus besoin d'IP fixe côté ESP32, ni de réseau Wi-Fi partagé entre l'application et l'ESP32 : les deux se connectent indépendamment à Internet, au même broker
- Connexion chiffrée obligatoire : port **8883** en TLS/TCP, propriété Protocol du composant réglée sur **SSL** (le port 8884, en WebSocket, ne fonctionne pas dans cette configuration)
- En cas de coupure réseau d'un côté ou de l'autre, chacun doit relancer sa connexion au broker indépendamment (reconnexion gérée par le composant MQTT côté application, et par `reconnectMQTT()` côté firmware)

## 11. Tests et intégration

1. Tester le R503 seul (enrôlement, recherche) avant toute intégration réseau
2. Tester la connexion MQTT de l'ESP32 seul, en vérifiant sur le dashboard HiveMQ qu'il apparaît connecté et abonné aux bons topics, avant de brancher l'application
3. Tester l'écran OLED seul (affichage statique) avant de le relier à la logique métier
4. Tester l'application MIT App Inventor bouton par bouton, en vérifiant sur le dashboard HiveMQ que les deux clients (ESP32 et application) apparaissent connectés simultanément
5. Assembler l'ensemble et tester le scénario complet : enregistrement avec nom, consultation d'état, suppression ciblée, suppression totale, mode présence passif

Statut : l'ensemble de ces tests, de bout en bout, a été validé avec succès.

## 12. Limites et évolutions

- capacité limitée du lecteur : le R503 ne peut stocker qu'un nombre restreint de gabarits, ce qui borne le nombre d'étudiants gérables
- absence de persistance centralisée des présences : sans base de données ni serveur, l'historique des présences n'est pas conservé, seul l'état courant (via NVS) et les opérations de gestion du lecteur sont couverts
- dépendance à un service tiers : le broker MQTT (HiveMQ Cloud) doit rester disponible, et les deux parties (ESP32 et application) doivent disposer d'un accès Internet fonctionnel — MQTT supprime la contrainte de réseau local partagé, mais introduit une dépendance à un service cloud externe
- dépendance au format et au protocole propriétaire du R503
- inadaptation à une gestion multi-salles ou à grande échelle sans évolution de l'architecture

Évolution proposée : faire cohabiter cette application mobile de pilotage local avec un serveur central (par exemple une API REST, ou un client MQTT côté serveur abonné aux mêmes topics) qui recevrait également les événements de présence, afin de centraliser les données de plusieurs salles ou lecteurs. L'application MIT App Inventor pourrait alors être conservée comme outil de gestion rapide et local, en complément d'une solution serveur plus complète.
