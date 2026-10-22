#include "Fonts.h"
#include "Theme.h"

#include <BinaryData.h>

namespace autosampler
{

void loadBundledFonts()
{
    const auto load = [] (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    };

    theme::registerTypefaces (load (BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize),
                              load (BinaryData::RubikBold_ttf, BinaryData::RubikBold_ttfSize),
                              load (BinaryData::RubikBlack_ttf, BinaryData::RubikBlack_ttfSize));
}

} // namespace autosampler
