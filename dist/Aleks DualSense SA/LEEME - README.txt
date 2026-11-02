ALEK'S DUALSENSE SA 2.2
Native DualSense for GTA San Andreas (PC 1.0 US) / DualSense nativo para GTA San Andreas (PC 1.0 US)
https://github.com/Alexgg1014/aleks-dualsense-sa

==================================== ENGLISH ====================================

REQUIREMENTS
- GTA San Andreas 1.0 US with an ASI Loader or Modloader.
- GInput (it reads the controller for the game). Without GInput the game does not see the pad.
- A DualSense (or DualSense Edge) on a USB cable or over Bluetooth (pair it in Windows settings first).
  Over Bluetooth everything works except HD haptics, which need the cable: wireless uses classic rumble.
- No Sony pad? An Xbox / XInput controller gets the mod's rumble effects (shots, hits, explosions, crashes,
  terrain). A DualShock 3 works the same through DsHidMini in XInput mode. [Vibration] XboxRumble=0 turns it off.
- A DualShock 4 on USB or Bluetooth also works: input, rumble and lightbar (no adaptive triggers or HD haptics).
  On the DualShock 4, hold PS instead of Mute for the combos.
- Steam Input OFF for GTA San Andreas (Steam > Library > right click the game > Properties > Controller).
- Close DS4Windows / DSX, or make sure they don't hide the controller.

INSTALL
- Modloader: copy the "Aleks DualSense SA" folder into "modloader".
- ASI Loader: copy Aleks.DualSense.SA.asi and Aleks.DualSense.SA.ini into the game folder (or "scripts").

CONTROLS
- Mute + Right ........ open / close the DualSense menu (also F11)
- Mute + Up / Down .... vibration +10 / -10 %
- Mute (tap) .......... adaptive triggers on / off (mic LED lit = off)
- In the menu: L1/R1 tabs, D-pad to choose and adjust, Cross to test, Circle to close.
  TEST > "Try haptics" lets you feel every effect without playing.

WHAT IT DOES
- Native input: the DualSense becomes XInput controller 0 for the whole game (GInput, other mods).
- Adaptive triggers per weapon and vehicle: pistol click, heavy shotgun pull, COD-style bolt action,
  automatics that buzz while firing, throttle that stiffens with speed, ABS / handbrake on the brake...
- HD haptics through the pad's own audio channels: every gun, footsteps by surface (sand, gravel,
  wood, metal, water...), engine revs, road texture, skids, crashes by zone, explosions from their side,
  molotov glass. Classic rumble is used if HD haptics are off or unavailable.
- With a weapon sound mod for EarShot (NextGen Weapon Sounds in Immersive Weapons Pack), every gunshot and reload
  is felt from that gun's own sound. Without it, the built-in effects play. [Haptics] SoundShots=0 turns it off.
- Lightbar and LEDs: health colour, armour LEDs, speedometer in vehicles, police flashes.
- Effects pause in menus, cutscenes and Photo Mode, and the triggers are always released when you leave.

SETTINGS
- In game through the menu. They are saved to Documents\GTA San Andreas User Files\Aleks DualSense SA.ini.
- Defaults are in Aleks.DualSense.SA.ini.

TROUBLESHOOTING
- Nothing works: check the log, Aleks.DualSense.SA.log, next to the ASI.
- No HD haptics: unplug and replug the cable once. Raise the Windows volume of "Speakers (DualSense...)".
  It must stay 4-channel (Quadraphonic) in Windows sound settings.
- Game sound comes out of the controller: Windows picked the DualSense as the default output. Pick your
  speakers / headset again.
- Triggers stuck after a crash: unplug and replug the cable (or turn the controller off and on).
- Bluetooth: if the pad is not found, remove it in Windows Bluetooth settings and pair it again (hold
  Create (Share on a DualShock 4) + PS until the light flashes). The menu shows the battery level while wireless.
- Mods that use slow motion pause the effects while it lasts.

==================================== ESPAÑOL ====================================

REQUISITOS
- GTA San Andreas 1.0 US con ASI Loader o Modloader.
- GInput (lee el control para el juego). Sin GInput el juego no ve el control.
- Un DualSense (o DualSense Edge) por cable USB o por Bluetooth (emparéjalo antes en la configuración de Windows).
  Por Bluetooth funciona todo menos la háptica HD, que necesita el cable: inalámbrico usa la vibración clásica.
- ¿Sin control de Sony? Un control de Xbox / XInput recibe las vibraciones del mod (disparos, golpes, explosiones,
  choques, terreno). Un DualShock 3 funciona igual con DsHidMini en modo XInput. [Vibration] XboxRumble=0 lo apaga.
- Un DualShock 4 por USB o Bluetooth también funciona: entrada, vibración y lightbar (sin gatillos adaptativos ni háptica HD).
  En el DualShock 4, mantén PS en lugar de Mute para los combos.
- Steam Input APAGADO para GTA San Andreas (Steam > Biblioteca > clic derecho > Propiedades > Controlador).
- Cierra DS4Windows / DSX, o que no oculten el control.

INSTALACIÓN
- Modloader: copia la carpeta "Aleks DualSense SA" dentro de "modloader".
- ASI Loader: copia Aleks.DualSense.SA.asi y Aleks.DualSense.SA.ini en la carpeta del juego (o "scripts").

CONTROLES
- Mute + Derecha ...... abrir / cerrar el menú del DualSense (también F11)
- Mute + Arriba/Abajo . vibración +10 / -10 %
- Mute (toque) ........ gatillos adaptativos sí / no (LED del micro encendido = apagados)
- En el menú: L1/R1 pestañas, cruceta para elegir y ajustar, X para probar, Círculo para cerrar.
  PROBAR > "Probar háptica" te deja sentir cada efecto sin jugar.

QUÉ HACE
- Entrada nativa: el DualSense se vuelve el control XInput 0 para todo el juego (GInput, otros mods).
- Gatillos adaptativos por arma y vehículo: el clic de la pistola, el jalón pesado de la escopeta, el cerrojo
  como en los COD, automáticas que vibran al disparar, acelerador que se endurece con la velocidad, ABS / freno de mano...
- Háptica HD por los propios canales de audio del control: cada arma, pasos según el suelo (arena, grava,
  madera, metal, agua...), revoluciones del motor, textura del camino, derrapes, choques por zona, explosiones
  desde su lado, el vidrio de la molotov. Si la háptica HD está apagada o no disponible, usa la vibración clásica.
- Con un mod de sonidos de armas para EarShot (NextGen Weapon Sounds de Immersive Weapons Pack), cada disparo y
  recarga se siente a partir del sonido de esa arma. Sin él, se usan los efectos propios. [Haptics] SoundShots=0 lo apaga.
- Lightbar y LEDs: color según la vida, LEDs de armadura, velocímetro en vehículos, luces de policía.
- Los efectos se pausan en menús, cinemáticas y el Modo Foto, y los gatillos siempre se sueltan al salir.

AJUSTES
- En el juego desde el menú. Se guardan en Documentos\GTA San Andreas User Files\Aleks DualSense SA.ini.
- Los valores por defecto están en Aleks.DualSense.SA.ini.

PROBLEMAS
- No funciona nada: revisa el log, Aleks.DualSense.SA.log, junto al ASI.
- No hay háptica HD: desconecta y vuelve a conectar el cable una vez. Sube el volumen de Windows de
  "Altavoces (DualSense...)". Debe seguir en 4 canales (Cuadrafónico) en la configuración de sonido.
- El sonido del juego sale por el control: Windows eligió el DualSense como salida predeterminada.
  Vuelve a elegir tus bocinas / audífonos.
- Gatillos trabados tras un crasheo: desconecta y vuelve a conectar el cable (o apaga y prende el control).
- Bluetooth: si no encuentra el control, quítalo en la configuración de Bluetooth de Windows y emparéjalo de
  nuevo (mantén Create (Share en el DualShock 4) + PS hasta que parpadee la luz). El menú muestra la batería cuando está inalámbrico.
- Los mods con cámara lenta pausan los efectos mientras dura.

CREDITS / CRÉDITOS
See / ver README on GitHub. MIT License. Made by Alexgg1014 with Claude (Anthropic's AI).
GTA San Andreas is a trademark of Rockstar Games. Fan-made, not affiliated with Rockstar Games or Take-Two.
