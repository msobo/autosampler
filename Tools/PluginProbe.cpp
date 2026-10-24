// Kommandozeilen-Tool: lädt ein Plugin ohne GUI und gibt Busse und Fehler aus.
// Mit --sample wird ein Sampling-Lauf ohne GUI ausgeführt, mit --find-loops
// die Loop-Suche für eine Session, mit --preview das Vorhören über den Sampler,
// mit --export der Export als WAV, SFZ oder Elektron Tonverk.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "engine/Exporter.h"
#include "engine/SampleWriter.h"
#include "engine/SamplerEngine.h"
#include "engine/SamplerLoader.h"
#include "engine/SamplingJob.h"
#include "engine/Session.h"
#include "host/BusLayout.h"
#include "host/LivePlayProcessor.h"
#include "host/Vst3ModuleInfoFix.h"

#include <iostream>

namespace
{
    constexpr const char* usage =
        "Aufruf:\n"
        "  PluginProbe <plugin-pfad> [sampleRate] [blockSize]\n"
        "  PluginProbe <plugin-pfad> --sample <zielordner> [--notes C2,C3] [--vel 64,127]\n"
        "              [--hold 1.0] [--tail 8] [--rate 48000] [--bits 24] [--realtime] [--name X]\n"
        "              [--rr real|cc] [--rr-steps 3] [--rr-mode independent|linked]\n"
        "              [--cc 74 | --param <id>] [--range 32-45] [--gain 0] [--pan 0] [--list-params]\n"
        "  PluginProbe --find-loops <session.asession> [--algo pad|universal|tonal]\n"
        "              [--blend linear|equalPower|sCurve|spectral] [--xfade 0.05] [--pad 0.5] [--save]\n"
        "  PluginProbe --preview <session.asession> --out <datei.wav> [--notes C2,C3,C4] [--vel 100]\n"
        "              [--hold 2] [--release 0.5] [--together] [--chord Dur] [--arp Auf --octaves 2]\n"
        "              [--seq 0,7,12,x] [--rate 1/16] [--bpm 120] [--reverb 0.3]\n"
        "  PluginProbe --export <session.asession> --out <zielordner> [--target wav|sfz|tonverk]\n"
        "              [--name X] [--rate 48000] [--bits 16|24|32] [--normalize -1] [--mono]\n"
        "              [--tail original|cutAfterLoop] [--loop-only] [--sustain-loop] [--zip] [--smooth 50]\n";

    /** Wert einer Option als "--name wert" oder "--name=wert". */
    juce::String getOption (const juce::StringArray& args, const juce::String& name, const juce::String& fallback = {})
    {
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == name && i + 1 < args.size())
                return args[i + 1];

            if (args[i].startsWith (name + "="))
                return args[i].fromFirstOccurrenceOf ("=", false, false);
        }

        return fallback;
    }

    int runSampling (juce::AudioPluginInstance& plugin, const juce::StringArray& args)
    {
        using namespace autosampler;

        SamplingSettings settings;
        settings.useCustomNotes = true;
        settings.customNotes = getOption (args, "--notes", "C3");
        settings.velocities = getOption (args, "--vel", "127");
        settings.holdSeconds = getOption (args, "--hold", "1.0").getDoubleValue();
        settings.maxTailSeconds = getOption (args, "--tail", "8.0").getDoubleValue();
        settings.sampleRate = getOption (args, "--rate", "48000.0").getDoubleValue();
        settings.bitDepth = getOption (args, "--bits", "24").getIntValue();
        settings.renderMode = args.contains ("--realtime") ? RenderMode::realtime : RenderMode::offline;
        settings.gainDb = getOption (args, "--gain", "0").getFloatValue();
        settings.pan = juce::jlimit (-1.0f, 1.0f, getOption (args, "--pan", "0").getFloatValue());

        if (args.contains ("--list-params"))
        {
            for (auto* parameter : plugin.getParameters())
                std::cout << "  " << getParameterIdentifier (*parameter) << ": " << parameter->getName (64) << "\n";

            return 0;
        }

        if (const auto rr = getOption (args, "--rr"); rr.isNotEmpty())
        {
            const auto source = parseRoundRobinSource (rr);

            if (! source.has_value())
            {
                std::cerr << "Unbekannte RR-Quelle: " << rr << " (real oder cc)\n";
                return 2;
            }

            settings.roundRobinSource = *source;
            settings.roundRobinSteps = getOption (args, "--rr-steps", "3").getIntValue();
            settings.roundRobinMode = parseRoundRobinMode (getOption (args, "--rr-mode", "independent"))
                                          .value_or (RoundRobinMode::independent);

            if (const auto parameterId = getOption (args, "--param"); parameterId.isNotEmpty())
            {
                settings.variationTarget = VariationTarget::parameter;
                settings.variationParameterId = parameterId;
            }

            settings.variationController = getOption (args, "--cc", "74").getIntValue();

            const auto range = getOption (args, "--range", "32-45");
            settings.variationFrom = range.upToFirstOccurrenceOf ("-", false, false).getIntValue();
            settings.variationTo = range.fromFirstOccurrenceOf ("-", false, false).getIntValue();
        }

        juce::String error;
        const auto requests = buildSampleList (settings, error);

        if (error.isNotEmpty())
        {
            std::cerr << error << "\n";
            return 2;
        }

        const auto name = getOption (args, "--name", plugin.getName());
        const auto base = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--sample"));
        base.createDirectory();
        const auto folder = createUniqueFolder (base, name);

        juce::AudioDeviceManager deviceManager;

        if (settings.renderMode == RenderMode::realtime)
            if (const auto deviceError = deviceManager.initialise (0, 2, nullptr, true); deviceError.isNotEmpty())
                std::cerr << "Audio-Gerät: " << deviceError << "\n";

        SamplingContext context { plugin };
        context.deviceManager = &deviceManager;
        context.settings = settings;
        context.requests = requests;
        context.outputFolder = folder;
        context.filePrefix = name;
        plugin.getStateInformation (context.pluginState);

        SamplingCallbacks callbacks;
        callbacks.onLog = [] (const juce::String& line, LogLevel level)
        {
            (level == LogLevel::info ? std::cout : std::cerr) << line << std::endl;
        };

        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto result = autosampler::runSampling (context, callbacks);

        std::cout << result.numWritten << " geschrieben, " << result.numSilent << " still, "
                  << result.numIdenticalGroups << " RR-Gruppen ohne Unterschied, "
                  << juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 1) << " s\n";

        if (result.error.isNotEmpty())
        {
            std::cerr << "FEHLER: " << result.error << "\n";
            return 1;
        }

        return 0;
    }

    /** Loop-Suche für alle entsperrten Samples einer Session, synchron. */
    int runLoopSearch (const juce::StringArray& args)
    {
        using namespace autosampler;

        Session session;
        const auto package = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--find-loops"));

        if (const auto error = session.load (package); error.isNotEmpty())
        {
            std::cerr << error << "\n";
            return 1;
        }

        auto settings = session.getLoopSearchSettings();

        if (const auto algo = getOption (args, "--algo"); algo.isNotEmpty())
        {
            const auto parsed = parseLoopAlgorithm (algo);

            if (! parsed.has_value())
            {
                std::cerr << "Unbekannter Algorithmus: " << algo << "\n";
                return 2;
            }

            settings.algorithm = *parsed;
        }

        settings.crossfadeMode = parseCrossfadeMode (getOption (args, "--blend"), settings.crossfadeMode);
        settings.crossfadeSeconds = getOption (args, "--xfade", juce::String (settings.crossfadeSeconds)).getDoubleValue();
        settings.padAmount = getOption (args, "--pad", juce::String (settings.padAmount)).getFloatValue();

        std::cout << session.getName() << ": " << session.getNumSamples() << " Samples, " << getDisplayName (settings.algorithm)
                  << ", " << getDisplayName (settings.crossfadeMode) << "\n";

        const auto startTime = juce::Time::getMillisecondCounterHiRes();
        int numFound = 0;

        for (int i = 0; i < session.getNumSamples(); ++i)
        {
            auto sample = session.getSample (i);
            std::cout << "  " << sample.getFileName() << ": ";

            if (sample.isLocked())
            {
                std::cout << "gesperrt\n";
                continue;
            }

            juce::AudioBuffer<float> audio;
            double sampleRate = 0.0;

            if (const auto error = readWavFile (session.getSampleFile (sample.getTree()), audio, sampleRate); error.isNotEmpty())
            {
                std::cout << error << "\n";
                continue;
            }

            const auto sampleStart = juce::Time::getMillisecondCounterHiRes();
            const auto result = findLoop (audio, sampleRate, settings, session.getLoopHints (sample.getTree()));
            const auto ms = juce::Time::getMillisecondCounterHiRes() - sampleStart;

            if (! result.found)
            {
                std::cout << result.message << "\n";
                continue;
            }

            ++numFound;
            sample.applyLoopResult (result, settings.crossfadeMode);
            std::cout << "S " << result.start << "  E " << result.end << "  X " << result.crossfade
                      << "  Score " << juce::String (result.score, 1) << "  " << getDisplayName (result.algorithm)
                      << "  (" << juce::roundToInt (ms) << " ms)"
                      << (result.message.isNotEmpty() ? "  " + result.message : juce::String()) << "\n";
        }

        std::cout << numFound << " Loops in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0, 2) << " s\n";

        if (args.contains ("--save"))
        {
            session.setLoopSearchSettings (settings);

            if (const auto error = session.save(); error.isNotEmpty())
            {
                std::cerr << "FEHLER: " << error << "\n";
                return 1;
            }

            std::cout << "Gespeichert\n";
        }

        return 0;
    }
}

namespace
{
    /** Spielt Noten einer Session über den Sampler (mit Loops und Crossfade)
        und schreibt das Ergebnis als WAV: nacheinander oder mit --together als Akkord. */
    int runPreview (const juce::StringArray& args)
    {
        using namespace autosampler;

        Session session;
        const auto package = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--preview"));
        const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--out", "preview.wav"));

        if (const auto error = session.load (package); error.isNotEmpty())
        {
            std::cerr << error << "\n";
            return 1;
        }

        juce::String error;
        const auto notes = parseNoteList (getOption (args, "--notes", "C3"), error);

        if (error.isNotEmpty() || notes.isEmpty())
        {
            std::cerr << "Noten: " << error << "\n";
            return 2;
        }

        constexpr double rate = 48000.0;
        constexpr int blockSize = 512;
        const auto velocity = juce::jlimit (1, 127, getOption (args, "--vel", "100").getIntValue());
        const auto hold = getOption (args, "--hold", "2.0").getDoubleValue();
        const auto release = getOption (args, "--release", "0.5").getDoubleValue();
        const auto together = args.contains ("--together");

        juce::StringArray errors;
        SamplerAudioCache cache;
        const auto instrument = buildSamplerInstrument (getSamplerSources (session), cache, errors);

        for (const auto& e : errors)
            std::cout << "  " << e << "\n";

        const auto& zones = instrument->getZones();
        std::cout << session.getName() << ": " << zones.size() << " Zonen, "
                  << std::count_if (zones.begin(), zones.end(), [] (const SamplerZone& z) { return z.loop; }) << " mit Loop\n";

        // Über dieselbe Kette wie in der App: Chord -> Arp/Sequencer -> Sampler -> Reverb
        LivePlaySettings live;
        live.source = PlaySource::sampler;
        live.sampler.releaseMs = (float) (release * 1000.0);
        live.sampler.oneShotWithoutLoop = false;

        auto& perform = live.perform;
        perform.bpm = getOption (args, "--bpm", "120").getDoubleValue();

        // Namen wie in der App, z. B. --chord Dur, --arp Auf/Ab, --rate 1/16
        const auto parseName = [] (const juce::String& text, auto last, auto& target)
        {
            using Enum = std::decay_t<decltype (last)>;

            for (int i = 0; i <= (int) last; ++i)
                if (getDisplayName ((Enum) i).equalsIgnoreCase (text))
                    target = (Enum) i;
        };

        parseName (getOption (args, "--chord"), ChordType::octave, perform.chord);
        parseName (getOption (args, "--rate", "1/16"), StepRate::thirtySecond, perform.rate);

        if (const auto arp = getOption (args, "--arp"); arp.isNotEmpty())
        {
            perform.pattern = PatternMode::arp;
            parseName (arp, ArpMode::asPlayed, perform.arpMode);
            perform.arpOctaves = juce::jlimit (1, 4, getOption (args, "--octaves", "1").getIntValue());
        }

        // --seq "0,7,12,x": Transposition pro Schritt, x = Pause
        if (const auto seq = juce::StringArray::fromTokens (getOption (args, "--seq"), ",", {}); ! seq.isEmpty())
        {
            perform.pattern = PatternMode::sequencer;
            perform.sequencerLength = juce::jlimit (1, PerformSettings::maxSteps, seq.size());

            for (int i = 0; i < perform.sequencerLength; ++i)
                perform.steps[(size_t) i] = { seq[i].trim() != "x", seq[i].trim().getIntValue(), 100 };
        }

        if (const auto mix = getOption (args, "--reverb"); mix.isNotEmpty())
        {
            live.reverb.enabled = true;
            live.reverb.mix = juce::jlimit (0.0f, 1.0f, mix.getFloatValue());
        }

        LivePlayProcessor processor;
        processor.setPlayConfigDetails (0, 2, rate, blockSize);
        processor.prepareToPlay (rate, blockSize);
        processor.setInstrument (instrument);
        processor.setSettings (live);

        // Ereignisse in Frames
        std::vector<std::pair<int, juce::MidiMessage>> events;
        const auto noteLength = (int) ((hold + release + 0.2) * rate);

        for (int i = 0; i < notes.size(); ++i)
        {
            const auto start = together ? 0 : i * noteLength;
            events.emplace_back (start, juce::MidiMessage::noteOn (1, notes[i], (juce::uint8) velocity));
            events.emplace_back (start + (int) (hold * rate), juce::MidiMessage::noteOff (1, notes[i]));
        }

        const auto numFrames = (together ? 1 : notes.size()) * noteLength;
        juce::AudioBuffer<float> output (2, numFrames);
        juce::AudioBuffer<float> block (2, blockSize);

        for (int start = 0; start < numFrames; start += blockSize)
        {
            const auto length = juce::jmin (blockSize, numFrames - start);
            juce::MidiBuffer midi;

            for (const auto& [frame, message] : events)
                if (frame >= start && frame < start + length)
                    midi.addEvent (message, frame - start);

            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, length);
            processor.processBlock (view, midi);

            for (int c = 0; c < 2; ++c)
                output.copyFrom (c, start, view, c, 0, length);
        }

        if (const auto writeError = writeWavFile (outFile, output, rate, 24, notes[0]); writeError.isNotEmpty())
        {
            std::cerr << "FEHLER: " << writeError << "\n";
            return 1;
        }

        std::cout << outFile.getFullPathName() << ": " << juce::String (numFrames / rate, 2) << " s, Spitze "
                  << juce::String (juce::Decibels::gainToDecibels (output.getMagnitude (0, numFrames)), 1) << " dBFS\n";
        return 0;
    }
}

namespace
{
    /** Export einer Session mit ihren gespeicherten Export-Einstellungen, Optionen überschreiben sie. */
    int runExportCommand (const juce::StringArray& args)
    {
        using namespace autosampler;

        Session session;
        const auto package = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--export"));

        if (const auto error = session.load (package); error.isNotEmpty())
        {
            std::cerr << error << "\n";
            return 1;
        }

        auto settings = session.getExportSettings();

        if (const auto target = getOption (args, "--target"); target.isNotEmpty())
        {
            const auto parsed = parseExportTarget (target);

            if (! parsed.has_value())
            {
                std::cerr << "Unbekanntes Ziel: " << target << "\n";
                return 2;
            }

            settings.target = *parsed;
        }

        if (const auto tail = getOption (args, "--tail"); tail.isNotEmpty())
            settings.tail = parseTailMode (tail).value_or (settings.tail);

        if (const auto normalize = getOption (args, "--normalize"); normalize.isNotEmpty())
        {
            settings.normalize = true;
            settings.peakDb = normalize.getFloatValue();
        }

        settings.name = getOption (args, "--name", settings.name);
        settings.sampleRate = getOption (args, "--rate", juce::String (settings.sampleRate)).getDoubleValue();
        settings.bitDepth = getOption (args, "--bits", juce::String (settings.bitDepth)).getIntValue();
        settings.mono = settings.mono || args.contains ("--mono");
        settings.loopOnly = settings.loopOnly || args.contains ("--loop-only");
        settings.loopOnRelease = settings.loopOnRelease && ! args.contains ("--sustain-loop");
        settings.zip = settings.zip || args.contains ("--zip");

        if (const auto smooth = getOption (args, "--smooth"); smooth.isNotEmpty())
            settings.zoneSmoothing = juce::jlimit (0.0f, 1.0f, smooth.getFloatValue() / 100.0f);

        const auto name = settings.name.isNotEmpty() ? settings.name : session.getName();
        const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile (getOption (args, "--out", "."));
        const auto sources = getExportSources (session);

        std::cout << "Exportiere " << (int) sources.size() << " von " << session.getNumSamples() << " Samples als "
                  << getDisplayName (settings.target) << " ...\n";

        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto result = runExport (sources, settings, name, destination);

        for (const auto& warning : result.warnings)
            std::cerr << "Hinweis: " << warning << "\n";

        if (result.error.isNotEmpty())
        {
            std::cerr << "FEHLER: " << result.error << "\n";
            return 1;
        }

        std::cout << result.numWritten << " Samples nach " << result.output.getFullPathName() << ", "
                  << juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 2) << " s\n";
        return 0;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << usage;
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI init;

    const juce::StringArray args (argv + 1, argc - 1);

    if (getOption (args, "--find-loops").isNotEmpty())
        return runLoopSearch (args);

    if (getOption (args, "--preview").isNotEmpty())
        return runPreview (args);

    if (getOption (args, "--export").isNotEmpty())
        return runExportCommand (args);

    const juce::String path (argv[1]);
    const auto sampleMode   = getOption (args, "--sample").isNotEmpty();
    const double sampleRate = argc > 2 && ! sampleMode ? juce::String (argv[2]).getDoubleValue() : 48000.0;
    const int blockSize     = argc > 3 && ! sampleMode ? juce::String (argv[3]).getIntValue() : 512;

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::OwnedArray<juce::PluginDescription> types;

    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (path))
            format->findAllTypesForFile (types, path);

    if (path.endsWithIgnoreCase (".vst3"))
        if (const auto numFixed = autosampler::fixStaleVst3ModuleInfo (types, path); numFixed > 0)
            std::cout << "moduleinfo.json veraltet: " << numFixed << " Beschreibung(en) korrigiert\n";

    if (types.isEmpty())
    {
        std::cerr << "Keine Plugin-Typen gefunden\n";
        return 1;
    }

    for (const auto* desc : types)
    {
        std::cout << "== " << desc->name << " (" << desc->pluginFormatName << ")  in=" << desc->numInputChannels
                  << " out=" << desc->numOutputChannels << "\n";

        juce::String error;
        auto instance = formats.createPluginInstance (*desc, sampleRate, blockSize, error);

        if (instance == nullptr)
        {
            std::cout << "   FEHLER: " << error << "\n";
            continue;
        }

        autosampler::prepareInstrumentBuses (*instance);

        for (const bool isInput : { true, false })
        {
            for (int i = 0; i < instance->getBusCount (isInput); ++i)
            {
                const auto* bus = instance->getBus (isInput, i);
                std::cout << "   " << (isInput ? "In " : "Out ") << i << ": " << bus->getName()
                          << " [" << bus->getCurrentLayout().getDescription() << "]"
                          << (bus->isEnabled() ? "" : " (deaktiviert)") << "\n";
            }
        }

        std::cout << "   Kanäle gesamt: in=" << instance->getTotalNumInputChannels()
                  << " out=" << instance->getTotalNumOutputChannels() << "\n";

        if (sampleMode)
            return runSampling (*instance, args);
    }

    return 0;
}
