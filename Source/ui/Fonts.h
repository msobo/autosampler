#pragma once

namespace autosampler
{

/** Lädt die Schriften der Oberfläche aus den Ressourcen (VT323 für Bildschirme,
    Rubik für Schilder und Logo; Lizenz SIL OFL, siehe Resources/Fonts/LICENSE.txt)
    und meldet sie bei theme::registerTypefaces an. */
void loadBundledFonts();

} // namespace autosampler
