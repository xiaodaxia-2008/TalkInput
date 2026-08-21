/// Quick WinRT online speech recognition microphone test.
///
/// Usage:
///   TalkInputWinRTSpeechTest [language-tag] [seconds]
///
///   language-tag = "zh-CN" (default), "en-US", etc.
///   seconds = 10 (default)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.SpeechRecognition.h>
#include <winrt/base.h>

#include <chrono>
#include <iostream>
#include <thread>

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Globalization;
using namespace Windows::Media::SpeechRecognition;

int wmain(int argc, wchar_t *argv[])
{
    std::wstring lang = L"zh-CN";
    int secs = 10;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") {
            std::wcout << L"Usage: TalkInputWinRTSpeechTest [lang-tag] [seconds]\n";
            return 0;
        }
        try { secs = std::stoi(arg); }
        catch (...) { lang = arg; }
    }

    init_apartment(apartment_type::multi_threaded);

    std::wcout << L"=== WinRT Online Speech Recognition ===" << std::endl;
    std::wcout << L"Language: " << lang << L", " << secs << L" seconds"
               << std::endl;

    SpeechRecognizer recognizer = nullptr;
    try {
        recognizer = SpeechRecognizer(Language(lang));
        std::wcout << L"[ok] Recognizer created" << std::endl;
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] Create: " << e.message().c_str() << L" (0x"
                   << std::hex << e.code() << L")" << std::endl;
        return 1;
    }

    try {
        recognizer.Constraints().Clear();
        recognizer.Constraints().Append(SpeechRecognitionTopicConstraint(
            SpeechRecognitionScenario::Dictation, L"dictation"));
        recognizer.Timeouts().InitialSilenceTimeout(std::chrono::seconds(30));
        recognizer.Timeouts().EndSilenceTimeout(std::chrono::seconds(2));

        auto r = recognizer.CompileConstraintsAsync().get();
        if (r.Status() != SpeechRecognitionResultStatus::Success) {
            std::wcerr << L"[FAIL] Compile: " << static_cast<int>(r.Status())
                       << std::endl;
            return 1;
        }
        std::wcout << L"[ok] Constraints compiled" << std::endl;
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] Compile: " << e.message().c_str() << std::endl;
        return 1;
    }

    auto t1 = recognizer.HypothesisGenerated([](const auto &, const auto &a) {
        std::wcout << L"  [partial] " << a.Hypothesis().Text().c_str()
                   << std::endl;
    });

    auto session = recognizer.ContinuousRecognitionSession();
    auto t2 = session.ResultGenerated([](const auto &, const auto &a) {
        auto r = a.Result();
        std::wcout
            << L"  ["
            << (r.Status() == SpeechRecognitionResultStatus::Success ? L"final"
                                                                     : L"interm")
            << L"] " << r.Text().c_str() << std::endl;
    });

    try {
        std::wcout << L"[ok] Listening... speak now!" << std::endl;
        session.StartAsync().get();
        std::wcout << L"[ok] Recognition started" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(secs));

        session.StopAsync().get();
        std::wcout << L"[ok] Recognition stopped" << std::endl;
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] Start/Stop: " << e.message().c_str() << L" (0x"
                   << std::hex << e.code() << L")" << std::endl;
        return 1;
    }

    recognizer.HypothesisGenerated(t1);
    session.ResultGenerated(t2);

    std::wcout << L"[ok] Done." << std::endl;
    return 0;
}
