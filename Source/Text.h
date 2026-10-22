#pragma once

#include <juce_core/juce_core.h>

namespace autosampler
{

/** UTF-8-Literal als juce::String, z. B. "Gerät"_u. Ohne den Suffix nimmt
    juce::String ein const char* als ASCII an (Assertion bei Umlauten). */
inline juce::String operator""_u (const char* text, size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}

} // namespace autosampler
