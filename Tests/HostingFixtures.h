#pragma once

// What Tamber Plug's plugin scanner wrote, so the hosting suites can pin that a
// database Plug wrote loads here unchanged and that one written here is the same
// bytes. Captured with Plug's own Tools/PluginScanner (Tamber-Inc/Plug,
// makeasound-migration, whose hosting code is the same from 5d90086 to 9df4749),
// run twice over
// a scratch folder holding copies of Plug's Gain, Synth and MidiTranspose bundles
// and a Broken.vst3 whose binary is five bytes of text:
//
//   PluginScanner --output <dir>/Plugins.json --search-folder <dir>/VST3
//                 --no-default-folders --no-au
//
// then a copy of Plug's Delay added and the same command run again with
// --timeout-ms 1, so the one new module was killed and recorded as crashed while
// the four cached ones were left alone. pluginCacheDocument is that file verbatim.
//
// appleDelayScan is the payload of the result line Plug's worker printed for
// Apple's AUDelay (`PluginScanner --scan aufx:dely:appl`), after its
// "TAMBERPLUG_SCAN:" marker: one AU entry, stamped as its component version.

#include <string_view>

namespace HostingFixtures
{

// clang-format off
inline constexpr auto pluginCacheDocument = std::string_view {R"json({
  "modules": [
    {
      "classes": [],
      "crashed": false,
      "error": "The bundle “Broken.vst3” couldn’t be loaded because it is damaged or missing necessary resources.",
      "fileSize": 5,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Broken.vst3"
    },
    {
      "classes": [
        {
          "category": "Audio Module Class",
          "classId": "4541504D53796E315653543300000000",
          "format": "VST3",
          "hasEditor": true,
          "hasMidiInput": true,
          "name": "Synth",
          "numAudioInputs": 0,
          "numAudioOutputs": 2,
          "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Synth.vst3",
          "subCategories": "Instrument|Synth",
          "vendor": "TamberPlug",
          "version": "1.0.0"
        }
      ],
      "crashed": false,
      "error": "",
      "fileSize": 5241312,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Synth.vst3"
    },
    {
      "classes": [
        {
          "category": "Audio Module Class",
          "classId": "4541504D4761696E5653543300000000",
          "format": "VST3",
          "hasEditor": true,
          "hasMidiInput": true,
          "name": "Gain",
          "numAudioInputs": 2,
          "numAudioOutputs": 2,
          "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Gain.vst3",
          "subCategories": "Fx",
          "vendor": "TamberPlug",
          "version": "1.0.0"
        },
        {
          "category": "Audio Module Class",
          "classId": "4541504D4E47696E5653543300000000",
          "format": "VST3",
          "hasEditor": true,
          "hasMidiInput": true,
          "name": "Not Gain",
          "numAudioInputs": 2,
          "numAudioOutputs": 2,
          "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Gain.vst3",
          "subCategories": "Fx",
          "vendor": "TamberPlug",
          "version": "1.0.0"
        }
      ],
      "crashed": false,
      "error": "",
      "fileSize": 5081712,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Gain.vst3"
    },
    {
      "classes": [
        {
          "category": "Audio Module Class",
          "classId": "4541504D4D5472315653543300000000",
          "format": "VST3",
          "hasEditor": true,
          "hasMidiInput": true,
          "name": "MidiTranspose",
          "numAudioInputs": 0,
          "numAudioOutputs": 2,
          "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/MidiTranspose.vst3",
          "subCategories": "Instrument",
          "vendor": "TamberPlug",
          "version": "1.0.0"
        }
      ],
      "crashed": false,
      "error": "",
      "fileSize": 5159856,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/MidiTranspose.vst3"
    },
    {
      "classes": [],
      "crashed": true,
      "error": "the scan worker was killed after 1 ms",
      "fileSize": 5185568,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3/Delay.vst3"
    }
  ],
  "searchPaths": [
    "/private/tmp/claude-501/-Users-eyalamir-Code-PlugMS/15b068c2-21f0-4b68-810d-c68830d75385/scratchpad/PlugCache/VST3"
  ],
  "version": 1
})json"};

inline constexpr auto appleDelayScan = std::string_view {R"json({"classes":[{"category":"aufx","classId":"aufx:dely:appl","format":"AU","hasEditor":true,"hasMidiInput":false,"name":"AUDelay","numAudioInputs":2,"numAudioOutputs":2,"path":"aufx:dely:appl","subCategories":"Fx","vendor":"Apple","version":"1.6.0"}],"crashed":false,"error":"","fileSize":0,"format":"AU","lastModified":67072,"path":"aufx:dely:appl"})json"};
// clang-format on

} // namespace HostingFixtures
