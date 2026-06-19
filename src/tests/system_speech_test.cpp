/// Windows offline speech recognition test using SAPI (COM).
///
/// Uses CLSID_SpInprocRecognizer — fully local, no online privacy policy needed.
///
/// Usage:
///   TalkInputSystemSpeechTest [language-id] [--live N] [--file audio.wav]
///
///   language-id = 2052 (zh-CN), 1033 (en-US), default is 2052
///   --live N    = listen from microphone for N seconds
///   --file x    = transcribe WAV file instead of microphone
///
/// Examples:
///   TalkInputSystemSpeechTest --live 10              # 10s mic, zh-CN
///   TalkInputSystemSpeechTest 1033 --live 5          # 5s mic, en-US
///   TalkInputSystemSpeechTest --file Models/data/audio.wav

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <iostream>
#include <string>
#include <thread>

#include <sapi.h>
#include <sphelper.h>

static void printBanner()
{
    std::wcout << L"=== Windows SAPI Offline Speech Recognition ===" << std::endl;
}

static void printUsage()
{
    std::wcout
        << L"Usage: TalkInputSystemSpeechTest [language-id] [--live N] [--file x]\n"
        << L"\n"
        << L"  language-id = 2052 (zh-CN, default), 1033 (en-US), etc.\n"
        << L"  --live N    = microphone input for N seconds\n"
        << L"  --file x    = transcribe WAV file\n"
        << L"\n"
        << L"Examples:\n"
        << L"  TalkInputSystemSpeechTest --live 10\n"
        << L"  TalkInputSystemSpeechTest 1033 --live 5\n"
        << L"  TalkInputSystemSpeechTest --file Models\\data\\audio.wav\n";
}

// ---------------------------------------------------------------------------
// Mic recognition
// ---------------------------------------------------------------------------
static int runMic(int langId, int seconds)
{
    std::wcout << L"\nLanguage: 0x" << std::hex << langId << std::dec
               << L", microphone for " << seconds << L" seconds" << std::endl;

    HRESULT hr = S_OK;

    // Create in-process recognizer — uses system default mic automatically
    CComPtr<ISpRecognizer> cpRecognizer;
    hr = cpRecognizer.CoCreateInstance(CLSID_SpInprocRecognizer);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] Cannot create in-process recognizer: 0x"
                   << std::hex << hr << std::endl;
        return 1;
    }
    std::wcout << L"[ok] In-process recognizer created" << std::endl;

    // Explicitly set microphone as audio input
    CComPtr<ISpObjectToken> cpAudioToken;
    hr = SpGetDefaultTokenFromCategoryId(SPCAT_AUDIOIN, &cpAudioToken, FALSE);
    if (SUCCEEDED(hr) && cpAudioToken) {
        hr = cpRecognizer->SetInput(cpAudioToken, TRUE);
        if (FAILED(hr)) {
            std::wcerr << L"[FAIL] SetInput: 0x" << std::hex << hr << std::endl;
            return 1;
        }
        std::wcout << L"[ok] SetInput to default microphone" << std::endl;
    }
    else {
        std::wcerr << L"[FAIL] No default audio input: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    // Create recognition context
    CComPtr<ISpRecoContext> cpRecoContext;
    hr = cpRecognizer->CreateRecoContext(&cpRecoContext);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] CreateRecoContext: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    // Load dictation grammar
    CComPtr<ISpRecoGrammar> cpRecoGrammar;
    hr = cpRecoContext->CreateGrammar(0, &cpRecoGrammar);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] CreateGrammar: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    hr = cpRecoGrammar->LoadDictation(nullptr, SPLO_STATIC);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] LoadDictation: 0x" << std::hex << hr
                   << std::endl;
        if (hr == SPERR_NOT_FOUND || hr == E_NOINTERFACE) {
            std::wcerr << L"  No dictation engine for this language.\n";
        }
        std::wcerr
            << L"  Settings > Time & Language > Language > Chinese > Options > "
               L"Basic typing & Speech"
            << std::endl;
        return 1;
    }
    std::wcout << L"[ok] Dictation grammar loaded" << std::endl;

    // Set interest — listen for audio state changes too for debugging
    cpRecoContext->SetInterest(
        SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_FALSE_RECOGNITION) |
            SPFEI(SPEI_START_SR_STREAM),
        SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_FALSE_RECOGNITION) |
            SPFEI(SPEI_START_SR_STREAM));
    cpRecoContext->SetNotifyWin32Event();

    // Activate dictation
    hr = cpRecoGrammar->SetDictationState(SPRS_ACTIVE);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] SetDictationState: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }
    std::wcout << L"[ok] Dictation activated" << std::endl;

    // Give SAPI time to initialize the audio stream
    Sleep(500);

    std::wcout << L"[ok] Listening... speak now!" << std::endl;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(seconds);
    int recognitionCount = 0;
    int falseRecoCount = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        hr = cpRecoContext->WaitForNotifyEvent(250);
        if (hr != S_OK) {
            continue;
        }

        CSpEvent spEvent;
        while (spEvent.GetFrom(cpRecoContext) == S_OK) {
            switch (spEvent.eEventId) {
            case SPEI_RECOGNITION: {
                recognitionCount++;
                CComPtr<ISpRecoResult> result = spEvent.RecoResult();
                SPPHRASE *phrase = nullptr;
                if (SUCCEEDED(result->GetPhrase(&phrase)) && phrase) {
                    std::wcout << L"  [final] ";
                    for (ULONG i = 0; i < phrase->Rule.ulCountOfElements; i++) {
                        if (phrase->pElements[i].pszDisplayText) {
                            std::wcout
                                << phrase->pElements[i].pszDisplayText;
                        }
                    }
                    std::wcout << std::endl;
                    CoTaskMemFree(phrase);
                }
                break;
            }
            case SPEI_FALSE_RECOGNITION:
                falseRecoCount++;
                break;
            case SPEI_START_SR_STREAM:
                std::wcout << L"  [stream] Audio stream started" << std::endl;
                break;
            }
            spEvent.Clear();
        }
    }

    std::wcout << L"[debug] Recognitions: " << recognitionCount
               << L", false: " << falseRecoCount << std::endl;

    cpRecoGrammar->SetDictationState(SPRS_INACTIVE);
    cpRecoGrammar->UnloadDictation();

    std::wcout << L"[ok] Done." << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// WAV file recognition
// ---------------------------------------------------------------------------
static int runFile(int langId, const std::wstring &wavPath)
{
    std::wcout << L"\nLanguage: 0x" << std::hex << langId << std::dec
               << L", file: " << wavPath << std::endl;

    HRESULT hr = S_OK;

    // Open WAV stream (auto-detect format)
    CComPtr<ISpStream> cpStream;
    hr = cpStream.CoCreateInstance(CLSID_SpStream);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] Cannot create stream: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    hr = cpStream->BindToFile(wavPath.c_str(), SPFM_OPEN_READONLY, nullptr,
                              nullptr, SPFEI_ALL_EVENTS);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] Cannot open WAV file: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }
    std::wcout << L"[ok] WAV file opened" << std::endl;

    // Create in-process recognizer
    CComPtr<ISpRecognizer> cpRecognizer;
    hr = cpRecognizer.CoCreateInstance(CLSID_SpInprocRecognizer);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] Cannot create recognizer: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    hr = cpRecognizer->SetInput(cpStream, TRUE);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] SetInput: 0x" << std::hex << hr << std::endl;
        return 1;
    }

    // Create context and grammar
    CComPtr<ISpRecoContext> cpRecoContext;
    hr = cpRecognizer->CreateRecoContext(&cpRecoContext);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] CreateRecoContext: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    CComPtr<ISpRecoGrammar> cpRecoGrammar;
    hr = cpRecoContext->CreateGrammar(0, &cpRecoGrammar);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] CreateGrammar: 0x" << std::hex << hr
                   << std::endl;
        return 1;
    }

    hr = cpRecoGrammar->LoadDictation(nullptr, SPLO_STATIC);
    if (FAILED(hr)) {
        std::wcerr << L"[FAIL] LoadDictation: 0x" << std::hex << hr
                   << std::endl;
        std::wcerr
            << L"  Install speech recognition from Settings > Time & "
               L"Language > Language > Add a language > Options > Speech"
            << std::endl;
        return 1;
    }

    // Notification setup
    cpRecoContext->SetInterest(
        SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_FALSE_RECOGNITION) |
            SPFEI(SPEI_END_SR_STREAM) | SPFEI(SPEI_START_SR_STREAM),
        SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_FALSE_RECOGNITION) |
            SPFEI(SPEI_END_SR_STREAM) | SPFEI(SPEI_START_SR_STREAM));
    cpRecoContext->SetNotifyWin32Event();

    // Activate
    cpRecoGrammar->SetDictationState(SPRS_ACTIVE);

    std::wcout << L"[ok] Transcribing..." << std::endl;

    BOOL done = FALSE;
    int recognitionCount = 0;
    int falseRecoCount = 0;
    while (!done && cpRecoContext->WaitForNotifyEvent(60000) == S_OK) {
        CSpEvent spEvent;
        while (!done && spEvent.GetFrom(cpRecoContext) == S_OK) {
            switch (spEvent.eEventId) {
            case SPEI_RECOGNITION: {
                recognitionCount++;
                CComPtr<ISpRecoResult> result = spEvent.RecoResult();
                SPPHRASE *phrase = nullptr;
                if (SUCCEEDED(result->GetPhrase(&phrase)) && phrase) {
                    std::wcout << L"  ";
                    for (ULONG i = 0; i < phrase->Rule.ulCountOfElements; i++) {
                        if (phrase->pElements[i].pszDisplayText) {
                            std::wcout
                                << phrase->pElements[i].pszDisplayText;
                        }
                    }
                    std::wcout << L" (" << phrase->Rule.ulCountOfElements
                               << L" elements)" << std::endl;
                    CoTaskMemFree(phrase);
                }
                break;
            }
            case SPEI_FALSE_RECOGNITION:
                falseRecoCount++;
                break;
            case SPEI_END_SR_STREAM:
                done = TRUE;
                break;
            case SPEI_START_SR_STREAM:
                std::wcout << L"  [stream] Audio stream started" << std::endl;
                break;
            default:
                std::wcout << L"  [event] 0x" << std::hex
                           << spEvent.eEventId << std::dec << std::endl;
                break;
            }
            spEvent.Clear();
        }
    }

    std::wcout << L"[debug] Recognitions: " << recognitionCount
               << L", false: " << falseRecoCount
               << L", done: " << (done ? L"yes" : L"no") << std::endl;

    cpRecoGrammar->SetDictationState(SPRS_INACTIVE);
    cpRecoGrammar->UnloadDictation();
    cpStream->Close();

    std::wcout << L"[ok] Done." << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
int wmain(int argc, wchar_t *argv[])
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    printBanner();

    int langId = 2052; // zh-CN
    int liveSecs = 0;
    std::wstring wavPath;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--live" && i + 1 < argc) {
            liveSecs = std::stoi(argv[++i]);
        }
        else if (arg == L"--file" && i + 1 < argc) {
            wavPath = argv[++i];
        }
        else if (arg == L"--help" || arg == L"-h") {
            printUsage();
            CoUninitialize();
            return 0;
        }
        else {
            // treat as language ID
            try {
                langId = std::stoi(arg, nullptr, 0);
            }
            catch (...) {
                std::wcerr << L"Unknown argument: " << arg << std::endl;
                printUsage();
                CoUninitialize();
                return 1;
            }
        }
    }

    if (liveSecs == 0 && wavPath.empty()) {
        printUsage();
        CoUninitialize();
        return 0;
    }

    int rc = 0;
    if (liveSecs > 0) {
        rc = runMic(langId, liveSecs);
    }
    else if (!wavPath.empty()) {
        rc = runFile(langId, wavPath);
    }

    CoUninitialize();
    return rc;
}
