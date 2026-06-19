#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on

#include <QByteArray>

#include <spdlog/spdlog.h>

void stopProcessListeningOnPort(quint16 port)
{
    using GetExtendedTcpTableFn =
        DWORD(WINAPI *)(PVOID, PDWORD, BOOL, ULONG, TCP_TABLE_CLASS, ULONG);

    HMODULE module = LoadLibraryW(L"iphlpapi.dll");
    if (!module) {
        SPDLOG_WARN("Cannot load iphlpapi.dll to inspect LLM server port");
        return;
    }

    auto *getExtendedTcpTable = reinterpret_cast<GetExtendedTcpTableFn>(
        GetProcAddress(module, "GetExtendedTcpTable"));
    if (!getExtendedTcpTable) {
        SPDLOG_WARN("Cannot resolve GetExtendedTcpTable");
        FreeLibrary(module);
        return;
    }

    DWORD size = 0;
    DWORD ret = getExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                    TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER) {
        FreeLibrary(module);
        return;
    }

    QByteArray buffer;
    buffer.resize(static_cast<qsizetype>(size));
    auto *table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
    ret = getExtendedTcpTable(table, &size, FALSE, AF_INET,
                              TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != NO_ERROR) {
        SPDLOG_WARN("GetExtendedTcpTable failed: {}", ret);
        FreeLibrary(module);
        return;
    }

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto &row = table->table[i];
        const auto localPort =
            static_cast<quint16>(ntohs(static_cast<u_short>(row.dwLocalPort)));
        if (localPort != port) {
            continue;
        }

        const DWORD pid = row.dwOwningPid;
        if (pid == 0 || pid == GetCurrentProcessId()) {
            continue;
        }

        HANDLE process =
            OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) {
            SPDLOG_WARN("Cannot open process {} listening on LLM port {}", pid,
                        port);
            continue;
        }

        SPDLOG_WARN("Terminating existing process {} listening on LLM port {}",
                    pid, port);
        if (TerminateProcess(process, 0)) {
            WaitForSingleObject(process, 3000);
        }
        else {
            SPDLOG_WARN("TerminateProcess failed for pid {}", pid);
        }
        CloseHandle(process);
    }

    FreeLibrary(module);
}
