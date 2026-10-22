#include "Vst3ModuleInfoFix.h"

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace autosampler
{

namespace
{
    // Minimaler Ausschnitt der VST3-ABI (IPluginFactory), um nicht die internen
    // SDK-Header von JUCE einbinden zu müssen.
    struct ClassInfo
    {
        char cid[16];
        int32_t cardinality;
        char category[32];
        char name[64];
    };

    struct PluginFactory;

    struct PluginFactoryVtbl
    {
        void* queryInterface;
        uint32_t (*addRef) (PluginFactory*);
        uint32_t (*release) (PluginFactory*);
        void* getFactoryInfo;
        int32_t (*countClasses) (PluginFactory*);
        int32_t (*getClassInfo) (PluginFactory*, int32_t, ClassInfo*);
    };

    struct PluginFactory { PluginFactoryVtbl* vtbl; };

    struct RealClass
    {
        juce::String name;
        std::array<char, 16> cid;
    };

    constexpr const char* audioModuleClass = "Audio Module Class";

    // Muss exakt den Hashes in JUCEs VST3-Format entsprechen
    // (getHashForRange / getNormalisedTUID in juce_VST3PluginFormatImpl.h).
    template <typename Range>
    int hashForRange (const Range& range)
    {
        juce::uint32 value = 0;

        for (const auto& item : range)
            value = (value * 31) + (juce::uint32) item;

        return (int) value;
    }

    int deprecatedUidFor (const std::array<char, 16>& cid)
    {
        return hashForRange (cid);
    }

    int uniqueIdFor (const std::array<char, 16>& cid)
    {
        // FUID::getLong1..4 ohne COM-Kompatibilität: je 4 Bytes big-endian
        std::array<juce::uint32, 4> longs {};

        for (size_t i = 0; i < 4; ++i)
            longs[i] = ((juce::uint32) (juce::uint8) cid[i * 4]     << 24)
                     | ((juce::uint32) (juce::uint8) cid[i * 4 + 1] << 16)
                     | ((juce::uint32) (juce::uint8) cid[i * 4 + 2] << 8)
                     |  (juce::uint32) (juce::uint8) cid[i * 4 + 3];

        return hashForRange (longs);
    }

    std::vector<RealClass> readAudioClassesFromBinary (const juce::String& bundlePath)
    {
        std::vector<RealClass> result;

       #if JUCE_MAC
        const auto path = bundlePath.toRawUTF8();
        auto url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path, (CFIndex) std::strlen (path), true);

        if (url == nullptr)
            return result;

        auto bundle = CFBundleCreate (nullptr, url);
        CFRelease (url);

        if (bundle == nullptr)
            return result;

        if (CFBundleLoadExecutable (bundle))
        {
            using BundleEntry = bool (*) (CFBundleRef);
            using BundleExit  = bool (*) (void);
            using GetFactory  = PluginFactory* (*) (void);

            auto entry      = (BundleEntry) CFBundleGetFunctionPointerForName (bundle, CFSTR ("bundleEntry"));
            auto exit       = (BundleExit)  CFBundleGetFunctionPointerForName (bundle, CFSTR ("bundleExit"));
            auto getFactory = (GetFactory)  CFBundleGetFunctionPointerForName (bundle, CFSTR ("GetPluginFactory"));

            if (getFactory != nullptr && (entry == nullptr || entry (bundle)))
            {
                if (auto* factory = getFactory())
                {
                    const auto numClasses = factory->vtbl->countClasses (factory);

                    for (int32_t i = 0; i < numClasses; ++i)
                    {
                        ClassInfo info {};

                        if (factory->vtbl->getClassInfo (factory, i, &info) != 0)
                            continue;

                        if (std::strncmp (info.category, audioModuleClass, sizeof (info.category)) != 0)
                            continue;

                        RealClass c;
                        c.name = juce::String::fromUTF8 (info.name, (int) strnlen (info.name, sizeof (info.name))).trim();
                        std::copy (std::begin (info.cid), std::end (info.cid), c.cid.begin());
                        result.push_back (std::move (c));
                    }

                    factory->vtbl->release (factory);
                }

                if (exit != nullptr)
                    exit();
            }
        }

        CFRelease (bundle);
       #else
        juce::ignoreUnused (bundlePath);
       #endif

        return result;
    }
}

int fixStaleVst3ModuleInfo (juce::OwnedArray<juce::PluginDescription>& descriptions,
                            const juce::String& bundlePath)
{
    if (descriptions.isEmpty())
        return 0;

    const auto realClasses = readAudioClassesFromBinary (bundlePath);

    if (realClasses.empty())
        return 0;

    int numFixed = 0;

    for (auto* desc : descriptions)
    {
        const auto matchesBinary = std::any_of (realClasses.begin(), realClasses.end(), [&] (const RealClass& c)
        {
            return c.name == desc->name
                && (uniqueIdFor (c.cid) == desc->uniqueId || deprecatedUidFor (c.cid) == desc->deprecatedUid);
        });

        if (matchesBinary)
            continue;

        // Gleicher Name im Binary, sonst die einzige Audio-Klasse
        auto match = std::find_if (realClasses.begin(), realClasses.end(),
                                   [&] (const RealClass& c) { return c.name == desc->name; });

        if (match == realClasses.end() && realClasses.size() == 1)
            match = realClasses.begin();

        if (match == realClasses.end())
            continue;

        desc->name            = match->name;
        desc->uniqueId        = uniqueIdFor (match->cid);
        desc->deprecatedUid   = deprecatedUidFor (match->cid);
        ++numFixed;
    }

    return numFixed;
}

} // namespace autosampler
