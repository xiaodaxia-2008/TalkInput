/// Minimal test for Windows.Media.SpeechRecognition lifecycle.
///
/// Usage:
///   TalkInputSystemSpeechTest [language-tag] [--live N]
///
///   language-tag = "zh-CN" | "en-US" | "" (system default)
///   --live N     = start microphone recognition for N seconds
///
/// Notes:
///   This test validates the WinRT apartment init + recognizer lifecycle
///   that was crashing when Qt initialised the thread as STA.
///
///   The Windows system speech recognizer uses the system microphone.
///   It cannot process a WAV file directly — use --live to test with
///   actual microphone input.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Globalization;
using namespace Windows::Media::SpeechRecognition;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void initWinrtApartment()
{
    // Standalone test — no Qt has pre-initialised COM, so use MTA directly.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::wcout << L"[init] WinRT apartment: multi_threaded (MTA)" << std::endl;
}

void printStatus(const SpeechRecognizer &recognizer)
{
    std::wcout << L"  State: " << static_cast<int>(recognizer.State())
               << std::endl;
    if (recognizer.CurrentLanguage()) {
        std::wcout << L"  Language: "
                   << recognizer.CurrentLanguage().LanguageTag().c_str()
                   << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Step 1 – Create & configure (the regression test)
// ---------------------------------------------------------------------------
int testLifecycle(const std::wstring &languageTag)
{
    std::wcout << L"\n=== Test: Recognizer lifecycle ===" << std::endl;

    // Create recognizer
    SpeechRecognizer recognizer = nullptr;
    try {
        if (!languageTag.empty()) {
            Language lang(languageTag);
            recognizer = SpeechRecognizer(lang);
            std::wcout << L"[ok] Created with language " << languageTag
                       << std::endl;
        }
        else {
            recognizer = SpeechRecognizer();
            std::wcout << L"[ok] Created with system default language"
                       << std::endl;
        }
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] SpeechRecognizer creation: "
                   << e.message().c_str() << L" (0x" << std::hex << e.code()
                   << L")" << std::endl;
        return 1;
    }

    printStatus(recognizer);

    // Configure constraints
    try {
        recognizer.Constraints().Clear();
        recognizer.Constraints().Append(SpeechRecognitionTopicConstraint(
            SpeechRecognitionScenario::Dictation, L"dictation"));
        recognizer.Timeouts().InitialSilenceTimeout(std::chrono::seconds(30));
        recognizer.Timeouts().EndSilenceTimeout(std::chrono::seconds(2));
        std::wcout << L"[ok] Constraints & timeouts set" << std::endl;
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] Constraint setup: " << e.message().c_str()
                   << L" (0x" << std::hex << e.code() << L")" << std::endl;
        return 1;
    }

    // Compile constraints
    try {
        auto result = recognizer.CompileConstraintsAsync().get();
        if (result.Status() == SpeechRecognitionResultStatus::Success) {
            std::wcout << L"[ok] Constraints compiled" << std::endl;
        }
        else {
            std::wcerr << L"[FAIL] Constraint compilation: "
                       << static_cast<int>(result.Status()) << std::endl;
            return 1;
        }
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] CompileConstraintsAsync: "
                   << e.message().c_str() << L" (0x" << std::hex << e.code()
                   << L")" << std::endl;
        return 1;
    }

    // Start & stop continuous recognition (quick cycle)
    auto session = recognizer.ContinuousRecognitionSession();

    bool completed = false;
    auto token = session.Completed(
        [&completed](const auto &, const auto &args) {
            completed = true;
            std::wcout << L"[event] Session completed: "
                       << static_cast<int>(args.Status()) << std::endl;
        });

    std::wcout << L"[ok] Event handlers registered" << std::endl;

    try {
        session.StartAsync().get();
        std::wcout << L"[ok] Recognition started" << std::endl;

        // Let it run very briefly to exercise the paths
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        session.StopAsync().get();
        std::wcout << L"[ok] Recognition stopped" << std::endl;
    }
    catch (const hresult_error &e) {
        // SPERR_SPEECH_PRIVACY_POLICY_NOT_ACCEPTED — Windows online speech
        // recognition is not enabled in Privacy settings.  This is not a code
        // bug; the test cannot exercise the full recognition path without it.
        if (e.code() == 0x80045509) {
            std::wcout << L"[skip] StartAsync requires speech privacy policy"
                       << L" (enable in Settings > Privacy > Speech)"
                       << std::endl;
        }
        else {
            std::wcerr << L"[FAIL] Start/StopAsync: " << e.message().c_str()
                       << L" (0x" << std::hex << e.code() << L")" << std::endl;
            return 1;
        }
    }

    // Small wait for the Completed event
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    session.Completed(token);

    if (completed) {
        std::wcout << L"[ok] Completed event received" << std::endl;
    }
    else {
        std::wcout
            << L"[warn] Completed event not received within 200ms (normal)"
            << std::endl;
    }

    std::wcout << L"\n=== Lifecycle test PASSED ===" << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// Step 2 – Live microphone test (optional)
// ---------------------------------------------------------------------------
int testLive(const std::wstring &languageTag, int seconds)
{
    std::wcout << L"\n=== Live microphone test (" << seconds
               << L" seconds) ===" << std::endl;
    std::wcout << L"Speak into the microphone now..." << std::endl;

    SpeechRecognizer recognizer = nullptr;
    try {
        if (!languageTag.empty()) {
            recognizer = SpeechRecognizer(Language(languageTag));
        }
        else {
            recognizer = SpeechRecognizer();
        }
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FAIL] SpeechRecognizer creation: "
                   << e.message().c_str() << std::endl;
        return 1;
    }

    recognizer.Constraints().Clear();
    recognizer.Constraints().Append(SpeechRecognitionTopicConstraint(
        SpeechRecognitionScenario::Dictation, L"dictation"));
    recognizer.Timeouts().InitialSilenceTimeout(std::chrono::seconds(30));
    recognizer.Timeouts().EndSilenceTimeout(std::chrono::seconds(2));

    auto compilation = recognizer.CompileConstraintsAsync().get();
    if (compilation.Status() != SpeechRecognitionResultStatus::Success) {
        std::wcerr << L"[FAIL] Constraints did not compile" << std::endl;
        return 1;
    }

    auto session = recognizer.ContinuousRecognitionSession();

    auto hypothesisToken = recognizer.HypothesisGenerated(
        [](const auto &, const auto &args) {
            std::wcout << L"[partial] " << args.Hypothesis().Text().c_str()
                       << std::endl;
        });

    auto resultToken = session.ResultGenerated([](const auto &,
                                                   const auto &args) {
        auto r = args.Result();
        std::wcout << L"["
                   << (r.Status() == SpeechRecognitionResultStatus::Success
                           ? L"final"
                           : L"intermediate")
                   << L"] " << r.Text().c_str() << std::endl;
    });

    std::wcout << L"Listening..." << std::endl;
    try {
        session.StartAsync().get();

        std::this_thread::sleep_for(std::chrono::seconds(seconds));

        session.StopAsync().get();
        std::wcout << L"Stopped." << std::endl;
    }
    catch (const hresult_error &e) {
        if (e.code() == 0x80045509) {
            std::wcout << L"[skip] Speech privacy policy not enabled"
                       << std::endl;
        }
        else {
            std::wcerr << L"[FAIL] Live test: " << e.message().c_str()
                       << L" (0x" << std::hex << e.code() << L")" << std::endl;
            return 1;
        }
    }

    recognizer.HypothesisGenerated(hypothesisToken);
    session.ResultGenerated(resultToken);

    std::wcout << L"\n=== Live test done ===" << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int wmain(int argc, wchar_t *argv[])
{
    std::wstring languageTag;
    int liveSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--live" && i + 1 < argc) {
            liveSeconds = std::stoi(argv[++i]);
        }
        else {
            languageTag = argv[i];
        }
    }

    if (languageTag.empty()) {
        std::wcout << L"Using system default language" << std::endl;
    }
    else {
        std::wcout << L"Language: " << languageTag << std::endl;
    }

    if (liveSeconds > 0) {
        std::wcout << L"Live mode: " << liveSeconds << L" seconds" << std::endl;
    }

    try {
        initWinrtApartment();

        int rc = testLifecycle(languageTag);
        if (rc != 0) {
            return rc;
        }

        if (liveSeconds > 0) {
            rc = testLive(languageTag, liveSeconds);
            if (rc != 0) {
                return rc;
            }
        }
    }
    catch (const hresult_error &e) {
        std::wcerr << L"[FATAL] " << e.message().c_str() << L" (0x"
                   << std::hex << e.code() << L")" << std::endl;
        return 1;
    }
    catch (const std::exception &e) {
        std::wcerr << L"[FATAL] std::exception: " << e.what() << std::endl;
        return 1;
    }

    std::wcout << L"\nAll tests PASSED." << std::endl;
    return 0;
}
