# Umbau TFA Dostmann Regenmesser zu AskSinPP Device

Der [TFA Dostmann Ersatz-Regenmesser, Modell "Monsun"](https://www.amazon.de/gp/product/B00FYSUATO/) eigent sich hervorragend als Basis fuer einen Homebrew-Regenmesser. Es handelt sich um einen klassischen Wippenzaehler mit Reedkontakt und eingearbeitetem Gehaeuse fuer eine proprietaeren Funksender und ein Batteriefach 2xAA.

![mechanischer Aufbau](Images/mechanics.png)

Ein Wippenschlag entspricht 0.5mm Regen.

Batteriefach und die Platine mit dem Reedkontakt werden wiederverwendet und die HF-Platine wird entfernt und durch eine AskSinPP-taugliche Platine ersetzt, z.B. meine Universalsensorplatine:

[AskSInPP Universal Board](https://www.github.com/HMSteve/PCBs/AskSinPP_UniversalBoard)


## Software

Zunaechst ist das Addon auf der CCU zu installieren. Die AskSinPP-Platine wird wie ueblich geflasht und kann dann bereits angelernt und getestet werden. Der relevante Datenpunkt ist RAIN_COUNTER, analog dem Homematic-Wetter-Kombisensor. Die CCU erzeugt deswegen beim Anlernen allein Systemvariablen fuer die gestrige und heutige Regenmenge sowie zwei Programme fuer deren Aktualisierung und Ruecksetzung. Dies kann bei Bedarf anschliessend angepasst werden. Ein Impuls am Zaehleingang sollte in der CCU-WebUI als 0.5mm Regen angezeigt werden.


## Umbau des Regenmessers

Zuerst wird der Deckel des Elektronikfaches entfernt, ebenso die beiden Schrauben aus der Platine am Boden des Faches, die die Batteriekontakte traegt. Die HF-Platine kann dann zuerst herausgezogen werden, anschliessend die starr verbundenen Platinen mit dem Reedkontakt sowie mit den Batteriekontakten.

### Schritt 1

Auf der Platine mit den Batteriekontakten wird die Vcc-Leiterbahn gemaess der Markierung durchtrennt. An die Batteriekontakte wird je eine litze fuer die Versorgung der AskSinPP-Platine angeloetet.

![Vcc trennen](Images/cutvcc.png)

### Schritt 2

Zum Entprellen des Reedkontaktes wird ein Tiefpass auf der Platine mit dem Reedkontakt integriert. Dazu wird zunaechst die Leiterbahn unter dem 220R-Widerstand im Bild bei Markierung 1 durchtrennt und sodann ein solcher Widerstand eingeloetet, der axakte Widerstandswert ist eher unkritisch. Danach wird bei Markierung 2 ein Kondensator von ca. 100nF eingeloetet, Bauform 1206 passt perfekt. Am mit T2 markierten Loetauge wird eine Litze fuer den Anschluss der AskSinPP-Platine angeloetet.

![Entprellen](Images/debouncing.png)

### Schritt 3

Die Platinen werden verbunden, oben die Originalplatinen, unten die AskSinPP-Platine. Nun kann die Elektronik wieder in das Elektronikfach einziehen. Man kann den im Batteriefach vorhandenen Taster natuerlich auch als Anlerntaster fuer die AskSinPP-Platine nutzen. Mir erschien das unnoetig.

![Elektronik-Ueberblick](Images/overview.png)


## Lizenz

Creative Commons BY-NC-SA
