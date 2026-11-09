# Changelog

## 3.7.1

- Fix stock-client integer truncation of filtered and sensitivity-scaled mouse movement. Slow one-count motion responds immediately at sensitivity 0.1, including negative movement and HUD/zoom overrides.
- Keep stock mouse-look, strafing, pitch limits and command creation; validate the supported instruction block before applying runtime-only changes.
- Add a real-client regression with an unpatched control, relocation, rollback and modified-code rejection checks.
- Publish an add-on-only release and clean installer requiring a user-supplied licensed 3248/WON game, Big AG HUD pack and BXT injector.
- Document the feature list, supported layout, raw-input/EPP behaviour and frame-timing limitations.

## 3.7

- Stock client.dll layout, with UHLCompat loaded by rebuilt Tankfird BXT.
- Borderless/DXGI capture path, raw gameplay input, cursor/load protections and console copy.
- Native texture/sprite fixes, independent scene/HUD scaling, scene brightness and local single-player permission synchronization.
- Precision FPS limiter and single previous-save backups in the integrated BXT build.
