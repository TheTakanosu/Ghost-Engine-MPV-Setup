#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QSettings>

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

namespace {

// One window, however many .m3u files are double-clicked: a second start
// hands its files to the first over this pipe and quits.
constexpr const char *kInstancePipe = "ghost-engine-app";

bool handOver(const QStringList &files)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstancePipe));
    if (!socket.waitForConnected(300))
        return false;
    socket.write(files.join('\n').toUtf8());
    socket.waitForBytesWritten(1000);
    return true;
}

// A crash leaves its stack in %TEMP%\GhostEngine-crash.txt: module, offset
// and, where the .pdb is beside the exe, function and line. One at zoom
// could not be made to happen again on purpose; this is what it leaves next
// time. Plain C here, nothing of Qt: the program is already broken.
LONG WINAPI writeCrash(EXCEPTION_POINTERS *ep)
{
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "GhostEngine-crash.txt");
    FILE *f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f)
        return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(f, "\n%04d-%02d-%02d %02d:%02d:%02d  " GHOST_VERSION "  code %08lx\n", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, ep->ExceptionRecord->ExceptionCode);
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *ep->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    for (int i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                                          SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i) {
        const DWORD64 at = frame.AddrPC.Offset;
        const DWORD64 base = SymGetModuleBase64(process, at);
        char module[MAX_PATH] = "?";
        if (base)
            GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
        const char *name = strrchr(module, '\\') ? strrchr(module, '\\') + 1 : module;
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 into = 0;
        IMAGEHLP_LINE64 line{sizeof(line)};
        DWORD column = 0;
        fprintf(f, "%2d %s+%llx  %s", i, name, at - base, SymFromAddr(process, at, &into, symbol) ? symbol->Name : "");
        if (SymGetLineFromAddr64(process, at, &column, &line))
            fprintf(f, "  %s:%lu", strrchr(line.FileName, '\\') ? strrchr(line.FileName, '\\') + 1 : line.FileName,
                    line.LineNumber);
        fputc('\n', f);
    }
    fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

int main(int argc, char *argv[])
{
    SetUnhandledExceptionFilter(writeCrash);
    // No Windows error boxes from the tools this starts (yt-dlp, and through
    // it ffmpeg): children inherit this, and a missing DLL in one of them
    // should fail quietly, not stack up dialogs.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    // A gaming mouse reports its position 1000 times a second or more, and
    // each move re-laid and repainted the page while a scroll bar was
    // dragged: the page fell behind the mouse. Moves that pile up are taken
    // as one, as Qt does by default on Linux.
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TheTakanosu");
    QApplication::setApplicationName("Ghost Engine");
    QApplication::setApplicationVersion(GHOST_VERSION);

    // "--startup": opened by Windows at sign-in (Settings > Playback).
    QStringList files = app.arguments().mid(1);
    const bool atStartup = files.removeAll(QStringLiteral("--startup")) > 0;
    if (handOver(files.isEmpty() ? QStringList{""} : files))
        return 0;
    {
        // Kept pointing at this exe, wherever an update or a move put it.
        QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
        const QString line = QStringLiteral("\"%1\" --startup").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        if (run.contains(QStringLiteral("Ghost Engine")) && run.value(QStringLiteral("Ghost Engine")).toString() != line)
            run.setValue(QStringLiteral("Ghost Engine"), line);
    }

    // Song lists play with no mpv window by default since 1.3.3. Someone who
    // used the app before and never chose keeps the window they had.
    {
        QSettings settings;
        if (!settings.contains("player/window") && settings.contains("window/geometry"))
            settings.setValue("player/window", true);
    }

    theme::apply(app);
    MainWindow window;

    QLocalServer server;
    QLocalServer::removeServer(QString::fromLatin1(kInstancePipe));
    server.listen(QString::fromLatin1(kInstancePipe));
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        QLocalSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, &window, [&window, client] {
            const QStringList paths = QString::fromUtf8(client->readAll()).split('\n', Qt::SkipEmptyParts);
            window.importFiles(paths);
            if (paths.isEmpty())
                window.bringToFront();
            client->deleteLater();
        });
    });

    // At sign-in it waits out of the way: in the tray when it is kept
    // running there, otherwise minimised on the taskbar.
    if (!atStartup)
        window.show();
    else if (!QSettings().value("tray/keepRunning", false).toBool())
        window.showMinimized();
    window.importFiles(files);
    return app.exec();
}
