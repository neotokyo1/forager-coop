# Trade-Handy (F3)

## Im Spiel

Host und Gäste brauchen denselben Build (Protokoll 24). In der gemeinsamen Oberwelt öffnet F3 das Handy. Mit den Pfeiltasten hoch/runter wird die Zeile gewählt, mit links/rechts der Partner, der Rucksackstapel oder die Menge. Die linke und rechte Hälfte einer Zeile funktionieren als Maustastenflächen. Enter oder der Aktionsknopf sendet das Angebot. Die Auswahl startet beim ganzen Stapel. Die rechte Mengentaste wechselt von dort auf ein Stück.

Der Empfänger erhält einen Hinweis, öffnet F3 und bietet seinen Stapel an. Beide sehen ihren eigenen Stapel und den Gegenwert vor der Bestätigung. Fachnummern beginnen bei eins, die Itemnummer ist die interne Spiel-ID. F3, Escape und das Kreuz brechen einen offenen Handel ab. Nach beiden Bestätigungen entscheidet der Host. Während dieser kurzen Entscheidung kann die Darstellung anhalten.

Pro Sitzung verhandelt ein Paar gleichzeitig. Ein Angebot verfällt nach 90 Sekunden. Coins gehören der ganzen Sitzung. Vaults und Ausrüstung werden durch diesen Tausch nicht belastet. Handelbar sind belegte, einfache Inventareinträge mit zwei ganzzahligen Feldern für Item und Menge. Unbekannte Eintragsformate bleiben erhalten. Ein kompletter Stapel kann seinen Platz für einen neuen Typ freigeben. Für Teilmengen muss der Zugang in einen bestehenden Stapel, ein erkanntes Leerfach oder einen nachweislich erweiterbaren Rucksack passen. Ein gleichartiges Gegenangebot und unklare Platzverhältnisse werden abgewiesen.

## Ablauf und Wiederherstellung

1. Der Host bindet eine Transaktionsnummer an zwei Verbindungs-IDs und zwei Angebote. Nach dem Gegenangebot sind beide Angebote unveränderlich. Beide Spieler bestätigen diese Ansicht. Der Host verarbeitet Nachrichten 115–118 gezielt zwischen den Beteiligten.
2. Jeder Teilnehmer liest seinen echten lokalen Rucksack erneut und prüft Fach, Typ, ursprüngliche Menge und Zielkapazität. Auf dem Spielthread setzt er eine vorbereitete Kopie des Rucksacks für einen Speicheraufruf ein. Der Puffer-Hook übernimmt das vollständige Data.cin, wenn es während dieses Aufrufs an den Writer übergeben wird. Dann wird der ursprüngliche Rucksack wiederhergestellt. Ohne einen solchen Puffer wird der Handel abgebrochen.
3. Die Vorbereitung benutzt einen eigenen Arbeitsslot aus 90–98. Jede Spielinstanz reserviert höchstens einen davon. Vorhandene Spielstände ohne Trade-Marker werden übersprungen. Der markierte Arbeitsordner bleibt für spätere Sitzungen erhalten. Hintergrundthreads dürfen ihre ausstehenden Schreibvorgänge beenden.
4. Der Host schreibt beide vorbereiteten Images in einem atomaren Dateiersatz nach `saveData<N>/coop_guests/trade.pending`. Vor diesem Schritt darf abgebrochen werden. Ab diesem Schritt gilt der Austausch. Der Host installiert die beiden Images, Gäste erhalten die Entscheidung, übernehmen den neuen Rucksack und quittieren. Bis zur Quittung sind alte Charakter-Uploads des jeweiligen Peers gesperrt. Nach dem Tausch verwendet der Gast die beim Speichern übergebenen Puffer für seine Uploads.
5. Nach einem Absturz während der Installation spielt der Ladepfad das Journal vollständig nach. Ein Host-Checkpoint schützt den lokalen Charakter gegen einen noch ausstehenden älteren Save. Verliert ein vorbereiteter Gast die Verbindung, bevor er die Entscheidung kennt, kehrt er zum Menü zurück und erhält beim Wiederbeitritt den Stand des Hosts. Ein wiederholtes Einspielen desselben Images schreibt dieselben Mengen.

Der Besitz eines Gasts wird durch dessen Spielprozess geprüft. Dies übernimmt das Vertrauensmodell der bestehenden Mod. Ein absichtlich modifizierter Client kann seinen eigenen Charakter oder ein Save-Image fälschen.

## Bauen und prüfen

Benötigt werden Windows, die Visual Studio C++ Build Tools mit x86-Toolset und Forager 4.1.9. In einem Git-Checkout installiert `git submodule update --init --recursive` die deklarierte MinHook-Abhängigkeit. In einem entpackten Projektarchiv mit leerem MinHook-Ordner lautet der entsprechende Befehl:

```powershell
git clone --branch v1.3.4 --depth 1 https://github.com/TsudaKageyu/minhook.git native/third_party/minhook
```

Danach im Projektstamm bauen und installieren:

```powershell
powershell -File native\build.ps1 -Install -GameDir "C:\Program Files (x86)\Steam\steamapps\common\Forager"
```

Die gebaute `native/build/version.dll` gehört auf alle Teilnehmer. Für den ersten Test einen separaten Spielstand verwenden.

| Test | Erwartung |
|---|---|
| Host und Gast tauschen zwei verschiedene ganze Stapel | Beide sehen das Gegenangebot. Erst nach zwei Bestätigungen wechseln exakt die vereinbarten Mengen. |
| Zwei Gäste handeln, der Host läuft weiter | Nur die beteiligten Rucksäcke ändern sich. Ein dritter Spieler kann den laufenden Tausch nicht übernehmen. |
| F3 halten, schließen, erneut öffnen; Maus und Werkzeug gleichzeitig benutzen | Pro Tastendruck ein Zustandswechsel. Navigation und Bestätigung lösen keine normale Spieleraktion aus. |
| Ein voller Rucksack gibt einen ganzen Stapel für einen bislang fehlenden Typ ab | Der eingehende Stapel nutzt dasselbe Fach. Eine Teilmenge ohne passenden Zielplatz wird abgewiesen. |
| Während der Angebotsphase verändert eine Aktion oder automatische Aufnahme den angebotenen Stapel | Eine veraltete Bestätigung führt zu keiner Buchung. |
| Vor der zweiten Bestätigung abbrechen oder die Verbindung trennen | Beide Seiten behalten ihre ursprünglichen Mengen. |
| Direkt nach beiden Bestätigungen einen Gast trennen, danach wieder beitreten | Beide Rucksäcke entsprechen derselben Hostentscheidung. Mehrfacher Wiederbeitritt erhöht keinen Bestand. |
| Nach Abschluss speichern, beide Prozesse neu starten und erneut beitreten | Die getauschten Mengen und spätere regulär gespeicherte Änderungen bleiben erhalten. |

Der eigenständige Regressionstest prüft Mengenbilanz, Kapazität, veraltete Stapel, Zahlenüberlauf, ungültige Images und das Wiederholen eines unterbrochenen Journal-Commits. In einer Visual-Studio-Entwicklerkonsole aus dem Projektstamm:

```bat
cl /nologo /std:c++17 /EHsc tools\gametest\trade_test.cpp native\src\trade_store.cpp native\src\files.cpp /Fe:%TEMP%\forager_trade_test.exe
%TEMP%\forager_trade_test.exe
```
