using System;
using System.Linq;
using System.Globalization;
static class NexusLanguages {
 internal static readonly string[] Codes={"en","es","it","fr","de"};
 internal static int Selected=Array.IndexOf(Codes,CultureInfo.CurrentUICulture.TwoLetterISOLanguageName);
 internal static string Code {get{return Codes[Selected<0?0:Selected];}}
 internal static readonly string[][] Rows={
new string[]{"Bypass","Omitir efecto","Bypass","Contourner","Bypass"},
new string[]{"Choose VST3 effect...","Elegir efecto VST3...","Scegli effetto VST3...","Choisir un effet VST3...","VST3-Effekt ausw\u00e4hlen..."},
new string[]{"Choose VST3 effect","Elegir efecto VST3","Scegli effetto VST3","Choisir un effet VST3","VST3-Effekt ausw\u00e4hlen"},
new string[]{"Open plugin interface","Abrir interfaz del plugin","Apri interfaccia del plugin","Ouvrir l\u2019interface du plugin","Plugin-Oberfl\u00e4che \u00f6ffnen"},
new string[]{"Remove effect","Quitar efecto","Rimuovi effetto","Retirer l\u2019effet","Effekt entfernen"},
new string[]{"No effect selected. Audio passes through.","Ning\u00fan efecto seleccionado. El audio pasa sin procesar.","Nessun effetto selezionato. L\u2019audio passa senza elaborazione.","Aucun effet s\u00e9lectionn\u00e9. L\u2019audio passe sans traitement.","Kein Effekt ausgew\u00e4hlt. Das Audio wird unver\u00e4ndert durchgeleitet."},
new string[]{"One effect per filter. Add another OBS filter for another effect.","Un efecto por filtro. A\u00f1ade otro filtro de OBS para otro efecto.","Un effetto per filtro. Aggiungi un altro filtro OBS per un altro effetto.","Un effet par filtre. Ajoutez un autre filtre OBS pour un autre effet.","Ein Effekt pro Filter. F\u00fcge f\u00fcr einen weiteren Effekt einen weiteren OBS-Filter hinzu."},
new string[]{"Effect unavailable","Efecto no disponible","Effetto non disponibile","Effet indisponible","Effekt nicht verf\u00fcgbar"},
new string[]{"Loading effect...","Cargando efecto...","Caricamento effetto...","Chargement de l\u2019effet...","Effekt wird geladen..."},
new string[]{"Wait for the current plugin operation to finish.","Espera a que termine la operaci\u00f3n actual del plugin.","Attendi il completamento dell\u2019operazione del plugin.","Attendez la fin de l\u2019op\u00e9ration du plugin.","Warte, bis der aktuelle Plugin-Vorgang abgeschlossen ist."},
new string[]{"Source is no longer available.","La fuente ya no est\u00e1 disponible.","La sorgente non \u00e8 pi\u00f9 disponibile.","La source n\u2019est plus disponible.","Die Quelle ist nicht mehr verf\u00fcgbar."},
new string[]{"Plugin interface is unavailable.","La interfaz del plugin no est\u00e1 disponible.","L\u2019interfaccia del plugin non \u00e8 disponibile.","L\u2019interface du plugin est indisponible.","Die Plugin-Oberfl\u00e4che ist nicht verf\u00fcgbar."},
new string[]{"Plugin error. Technical details:","Error del plugin. Detalles t\u00e9cnicos:","Errore del plugin. Dettagli tecnici:","Erreur du plugin. D\u00e9tails techniques :","Plugin-Fehler. Technische Details:"},
new string[]{"Add VST3 Effect","A\u00f1adir efecto VST3","Aggiungi effetto VST3","Ajouter un effet VST3","VST3-Effekt hinzuf\u00fcgen"},
new string[]{"Select a 64-bit VST3 audio effect. Instruments are not supported.","Selecciona un efecto de audio VST3 de 64 bits. No se admiten instrumentos.","Seleziona un effetto audio VST3 a 64 bit. Gli strumenti non sono supportati.","S\u00e9lectionnez un effet audio VST3 64 bits. Les instruments ne sont pas pris en charge.","W\u00e4hle einen 64-Bit-VST3-Audioeffekt. Instrumente werden nicht unterst\u00fctzt."},
new string[]{"Search effects","Buscar efectos","Cerca effetti","Rechercher des effets","Effekte suchen"},
new string[]{"Optional VST3 folder","Carpeta VST3 opcional","Cartella VST3 facoltativa","Dossier VST3 facultatif","Optionaler VST3-Ordner"},
new string[]{"Browse...","Examinar...","Sfoglia...","Parcourir...","Durchsuchen..."},
new string[]{"VST3 effects folder","Carpeta de efectos VST3","Cartella degli effetti VST3","Dossier des effets VST3","Ordner f\u00fcr VST3-Effekte"},
new string[]{"Choose a scan location.","Elige una ubicaci\u00f3n para explorar.","Scegli una posizione da analizzare.","Choisissez un emplacement \u00e0 analyser.","W\u00e4hle einen Speicherort zum Durchsuchen."},
new string[]{"Scan installed","Explorar instalados","Analizza installati","Analyser les effets install\u00e9s","Installierte durchsuchen"},
new string[]{"Scan folder","Explorar carpeta","Analizza cartella","Analyser le dossier","Ordner durchsuchen"},
new string[]{"Retry scan","Reintentar exploraci\u00f3n","Riprova analisi","Relancer l\u2019analyse","Erneut durchsuchen"},
new string[]{"Add","A\u00f1adir","Aggiungi","Ajouter","Hinzuf\u00fcgen"},
new string[]{"Cancel","Cancelar","Annulla","Annuler","Abbrechen"},
new string[]{"Choose a folder or use Scan installed.","Elige una carpeta o pulsa Explorar instalados.","Scegli una cartella o usa Analizza installati.","Choisissez un dossier ou utilisez Analyser les effets install\u00e9s.","W\u00e4hle einen Ordner oder nutze Installierte durchsuchen."},
new string[]{"Stopping scan. Retry when it finishes.","Deteniendo la exploraci\u00f3n. Reintenta cuando termine.","Arresto dell\u2019analisi. Riprova al termine.","Arr\u00eat de l\u2019analyse. R\u00e9essayez une fois termin\u00e9e.","Suche wird angehalten. Versuche es danach erneut."},
new string[]{"Other scans are finishing. Please retry.","Otras exploraciones est\u00e1n terminando. Vuelve a intentarlo.","Altre analisi stanno terminando. Riprova.","D\u2019autres analyses se terminent. R\u00e9essayez.","Andere Suchvorg\u00e4nge werden beendet. Versuche es erneut."},
new string[]{"VST3 scan helper is missing.","Falta el explorador auxiliar de VST3.","Manca il programma di analisi VST3.","Le programme d\u2019analyse VST3 est introuvable.","Das VST3-Suchprogramm fehlt."},
new string[]{"Finding VST3 effects...","Buscando efectos VST3...","Ricerca effetti VST3...","Recherche des effets VST3...","VST3-Effekte werden gesucht..."},
new string[]{"Folder discovery limit reached.","Se alcanz\u00f3 el l\u00edmite de carpetas.","Raggiunto il limite delle cartelle.","Limite de recherche des dossiers atteinte.","Maximale Anzahl von Ordnern erreicht."},
new string[]{"Effect list limit reached.","Se alcanz\u00f3 el l\u00edmite de efectos.","Raggiunto il limite degli effetti.","Limite de la liste d\u2019effets atteinte.","Maximale Anzahl von Effekten erreicht."},
new string[]{"Scanning %1 / %2...","Explorando %1 / %2...","Analisi %1 / %2...","Analyse %1 / %2...","Suche %1 / %2..."},
new string[]{"Found %1 effect candidates. Compatibility is checked when adding.","Se encontraron %1 efectos candidatos. La compatibilidad se comprueba al a\u00f1adirlos.","Trovati %1 effetti candidati. La compatibilit\u00e0 viene verificata all\u2019aggiunta.","%1 effets candidats trouv\u00e9s. La compatibilit\u00e9 est v\u00e9rifi\u00e9e lors de l\u2019ajout.","%1 m\u00f6gliche Effekte gefunden. Die Kompatibilit\u00e4t wird beim Hinzuf\u00fcgen gepr\u00fcft."},
new string[]{"Scan cancelled. ","Exploraci\u00f3n cancelada. ","Analisi annullata. ","Analyse annul\u00e9e. ","Suche abgebrochen. "},
new string[]{"%1 scan issue(s).","%1 incidencias de exploraci\u00f3n.","%1 problemi di analisi.","%1 probl\u00e8me(s) d\u2019analyse.","%1 Suchproblem(e)."},
new string[]{"Invalid path in package.","Ruta no v\u00e1lida en el paquete.","Percorso non valido nel pacchetto.","Chemin invalide dans le paquet.","Ung\u00fcltiger Pfad im Paket."},
new string[]{"Incomplete package.","Paquete incompleto.","Pacchetto incompleto.","Paquet incomplet.","Unvollst\u00e4ndiges Paket."},
new string[]{"Close OBS before continuing.","Cierra OBS antes de continuar.","Chiudi OBS prima di continuare.","Fermez OBS avant de continuer.","Schlie\u00dfe OBS, bevor du fortf\u00e4hrst."},
new string[]{"Invalid installation record.","Registro de instalaci\u00f3n no v\u00e1lido.","Registro di installazione non valido.","Registre d\u2019installation invalide.","Ung\u00fcltige Installationsaufzeichnung."},
new string[]{"Record contains unknown files.","Registro con archivos desconocidos.","Il registro contiene file sconosciuti.","Le registre contient des fichiers inconnus.","Die Aufzeichnung enth\u00e4lt unbekannte Dateien."},
new string[]{"The destination folder must be named obs-vst3-nexus.","La carpeta de destino debe llamarse obs-vst3-nexus.","La cartella di destinazione deve chiamarsi obs-vst3-nexus.","Le dossier de destination doit s\u2019appeler obs-vst3-nexus.","Der Zielordner muss obs-vst3-nexus hei\u00dfen."},
new string[]{"An unowned or modified file was preserved: ","Se conserva un archivo ajeno o modificado: ","File estraneo o modificato conservato: ","Un fichier tiers ou modifi\u00e9 a \u00e9t\u00e9 conserv\u00e9 : ","Eine fremde oder ge\u00e4nderte Datei wurde beibehalten: "},
new string[]{"The operation did not complete. Check the administrator window.","No se complet\u00f3 la operaci\u00f3n. Revisa el mensaje de la ventana de administrador.","Operazione non completata. Controlla la finestra amministratore.","L\u2019op\u00e9ration n\u2019a pas abouti. Consultez la fen\u00eatre administrateur.","Der Vorgang wurde nicht abgeschlossen. Pr\u00fcfe das Administratorfenster."},
new string[]{"Test installer 0.1.0 \u00b7 revision 10","Instalador de prueba 0.1.0 \u00b7 revisi\u00f3n 10","Installer di prova 0.1.0 \u00b7 revisione 10","Installateur de test 0.1.0 \u00b7 r\u00e9vision 10","Testinstallation 0.1.0 \u00b7 Revision 10"},
new string[]{"One VST3 effect per filter \u00b7 Windows x64 \u00b7 OBS 31 / 32 / 33","Un efecto VST3 por filtro \u00b7 Windows x64 \u00b7 OBS 31 / 32 / 33","Un effetto VST3 per filtro \u00b7 Windows x64 \u00b7 OBS 31 / 32 / 33","Un effet VST3 par filtre \u00b7 Windows x64 \u00b7 OBS 31 / 32 / 33","Ein VST3-Effekt pro Filter \u00b7 Windows x64 \u00b7 OBS 31 / 32 / 33"},
new string[]{"Test version. Close OBS before installing or uninstalling.","Versi\u00f3n de prueba. Cierra OBS antes de instalar o desinstalar.","Versione di prova. Chiudi OBS prima di installare o disinstallare.","Version de test. Fermez OBS avant d\u2019installer ou de d\u00e9sinstaller.","Testversion. Schlie\u00dfe OBS vor der Installation oder Deinstallation."},
new string[]{"Folder\u2026","Carpeta\u2026","Cartella\u2026","Dossier\u2026","Ordner\u2026"},
new string[]{"Select the parent folder for obs-vst3-nexus","Selecciona la carpeta padre para instalar obs-vst3-nexus","Seleziona la cartella principale per obs-vst3-nexus","S\u00e9lectionnez le dossier parent pour obs-vst3-nexus","W\u00e4hle den \u00fcbergeordneten Ordner f\u00fcr obs-vst3-nexus"},
new string[]{"Scenes and profiles are preserved. Keep this installer to uninstall.","No modifica escenas ni perfiles. Conserva el instalador para desinstalar.","Scene e profili vengono conservati. Tieni l\u2019installer per disinstallare.","Sc\u00e8nes et profils conserv\u00e9s. Gardez cet installateur pour d\u00e9sinstaller.","Szenen und Profile bleiben erhalten. Bewahre das Installationsprogramm auf."},
new string[]{"Commercial effects are not included. Extended OBS testing remains pending.","No incluye efectos comerciales. Quedan pendientes pruebas ampliadas en OBS.","Effetti commerciali non inclusi. Restano da svolgere test estesi in OBS.","Effets commerciaux non inclus. Des tests OBS approfondis restent n\u00e9cessaires.","Kommerzielle Effekte sind nicht enthalten. Erweiterte OBS-Tests stehen noch aus."},
new string[]{"Install","Instalar","Installa","Installer","Installieren"},
new string[]{"Uninstall","Desinstalar","Disinstalla","D\u00e9sinstaller","Deinstallieren"},
new string[]{"Close","Cerrar","Chiudi","Fermer","Schlie\u00dfen"},
new string[]{"Installed. Open OBS and add VST 3 NEXUS as an audio filter.","Instalado. Abre OBS y a\u00f1ade el filtro VST 3 NEXUS a una fuente de audio.","Installato. Apri OBS e aggiungi VST 3 NEXUS come filtro audio.","Install\u00e9. Ouvrez OBS et ajoutez VST 3 NEXUS comme filtre audio.","Installiert. \u00d6ffne OBS und f\u00fcge VST 3 NEXUS als Audiofilter hinzu."},
new string[]{"Uninstall OBS VST3 NEXUS from this folder?","\u00bfDesinstalar OBS VST3 NEXUS de esta carpeta?","Disinstallare OBS VST3 NEXUS da questa cartella?","D\u00e9sinstaller OBS VST3 NEXUS de ce dossier ?","OBS VST3 NEXUS aus diesem Ordner deinstallieren?"},
new string[]{"Uninstalled. Unrelated files, settings and backups are preserved.","Desinstalado. Los archivos ajenos, ajustes y copias de seguridad se conservan.","Disinstallato. File estranei, impostazioni e backup sono conservati.","D\u00e9sinstall\u00e9. Fichiers tiers, r\u00e9glages et sauvegardes conserv\u00e9s.","Deinstalliert. Fremde Dateien, Einstellungen und Sicherungen bleiben erhalten."},
new string[]{"Install VST 3 NEXUS","Instalar VST 3 NEXUS","Installa VST 3 NEXUS","Installer VST 3 NEXUS","VST 3 NEXUS installieren"},
new string[]{"Installation folder","Carpeta de instalaci\u00f3n","Cartella di installazione","Dossier d\u2019installation","Installationsordner"},
new string[]{"Close OBS before installing or uninstalling.","Cierra OBS antes de instalar o desinstalar.","Chiudi OBS prima di installare o disinstallare.","Fermez OBS avant d\u2019installer ou de d\u00e9sinstaller.","Schlie\u00dfe OBS vor der Installation oder Deinstallation."},
new string[]{"Scenes and profiles are preserved. Commercial effects are not included.","Se conservan tus escenas y perfiles. No incluye efectos comerciales.","Scene e profili vengono conservati. Effetti commerciali non inclusi.","Vos sc\u00e8nes et profils sont conserv\u00e9s. Effets commerciaux non inclus.","Szenen und Profile bleiben erhalten. Kommerzielle Effekte sind nicht enthalten."},
new string[]{"Destination path is too long. Choose a shorter folder.","La ruta de destino es demasiado larga. Elige una carpeta m\u00e1s corta.","Il percorso \u00e8 troppo lungo. Scegli una cartella pi\u00f9 corta.","Le chemin est trop long. Choisissez un dossier plus court.","Der Zielpfad ist zu lang. W\u00e4hle einen k\u00fcrzeren Ordner."}
 };
 internal static string Text(string key){
 int selected=Selected<0?0:Selected;
 foreach(var row in Rows)if(row.Contains(key))return row[selected];
 foreach(var row in Rows)foreach(var cell in row)if(cell.EndsWith(": ")&&key.StartsWith(cell,StringComparison.Ordinal))return row[selected]+key.Substring(cell.Length);
 return String.Join("\n",key.Split('\n').Select(line=>{foreach(var row in Rows)if(row.Contains(line))return row[selected];return line;}));
 }
}
