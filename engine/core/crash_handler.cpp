#include "core/crash_handler.h"

#include "core/logger.h"

#if defined(__linux__)
#include <cxxabi.h>
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <link.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#endif

namespace engine3d {

#if defined(__linux__)
namespace {

char g_directory[256] = "crash";
char g_program[128] = "program";
// Zapasowy stos dla funkcji obsługi sygnału - przy przepełnieniu stosu (nieskończona rekurencja) zwykły stos
// jest już zajęty i bez tego obsługa sama by się wywaliła.
alignas(16) char g_altStack[64 * 1024];

// --- Pisanie bez malloc i bez printf (bezpieczne w obsłudze sygnału) ---
void put(int fd, const char* s) {
    if (!s) return;
    size_t n = std::strlen(s);
    while (n > 0) {
        const ssize_t w = ::write(fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= static_cast<size_t>(w);
    }
}
void putHex(int fd, uintptr_t v) {
    char buf[2 + 16 + 1];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) buf[2 + i] = "0123456789abcdef"[(v >> ((15 - i) * 4)) & 0xF];
    buf[18] = '\0';
    put(fd, buf);
}
void putInt(int fd, long v) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    const bool neg = v < 0;
    unsigned long u = neg ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    do {
        buf[--i] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u > 0 && i > 1);
    if (neg) buf[--i] = '-';
    put(fd, buf + i);
}

const char* signalDescription(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV - odwołanie do niedozwolonej pamięci (np. zły wskaźnik, indeks poza tablicą)";
    case SIGBUS: return "SIGBUS - błąd dostępu do pamięci (np. źle wyrównany adres, obcięty plik zmapowany)";
    case SIGFPE: return "SIGFPE - błąd arytmetyki (np. dzielenie całkowite przez zero)";
    case SIGILL: return "SIGILL - nieprawidłowa instrukcja procesora";
    case SIGABRT: return "SIGABRT - program przerwany (abort, assert albo nieobsłużony wyjątek)";
    default: return "sygnał";
    }
}

// Plik zrzutu: <katalog>/<program>_RRRRMMDD-GGMMSS.log
int openCrashFile() {
    char path[512];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    std::snprintf(path, sizeof(path), "%s/%s_%s.log", g_directory, g_program, stamp);
    int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        put(STDERR_FILENO, "\n*** Awaria programu - opis zapisany w pliku: ");
        put(STDERR_FILENO, path);
        put(STDERR_FILENO, "\n");
    }
    return fd;
}

// Czy moduł to plik wykonywalny ładowany pod stały adres (ET_EXEC) - wtedy addr2line chce adresu bezwzględnego,
// a dla bibliotek .so i plików PIE - przesunięcia od początku modułu.
bool isFixedAddressExecutable(const void* base) {
    const auto* eh = static_cast<const ElfW(Ehdr)*>(base);
    return eh && std::memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_type == ET_EXEC;
}

// Plik i linia ramki przez zewnętrzny addr2line (fork + exec - wynik idzie wprost do pliku zrzutu).
void writeSourceLine(int fd, void* address, bool returnAddress, bool inlines = false) {
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname || !info.dli_fbase) {
        put(fd, "?\n");
        return;
    }
    // Adres powrotu wskazuje instrukcję PO wywołaniu - cofamy o 1, żeby trafić w linię wywołania.
    uintptr_t addr = reinterpret_cast<uintptr_t>(address) - (returnAddress ? 1 : 0);
    if (!isFixedAddressExecutable(info.dli_fbase)) addr -= reinterpret_cast<uintptr_t>(info.dli_fbase);
    char addrText[32];
    std::snprintf(addrText, sizeof(addrText), "0x%lx", static_cast<unsigned long>(addr));
    const pid_t pid = fork();
    if (pid == 0) {
        ::dup2(fd, STDOUT_FILENO);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) ::dup2(devnull, STDERR_FILENO);
        if (inlines) {   // -i: także funkcje wstawione inline (błąd często siedzi w małej funkcji wklejonej w większą)
            execlp("addr2line", "addr2line", "-C", "-f", "-p", "-i", "-e", info.dli_fname, addrText, static_cast<char*>(nullptr));
        } else {
            execlp("addr2line", "addr2line", "-C", "-f", "-p", "-e", info.dli_fname, addrText, static_cast<char*>(nullptr));
        }
        _exit(127);   // brak addr2line w systemie
    }
    if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) put(fd, "(brak addr2line - zainstaluj binutils)\n");
    }
}

void writeReport(int fd, const char* reason, const siginfo_t* info, void* faultPc) {
    put(fd, "=== Awaria programu ");
    put(fd, g_program);
    put(fd, " (PID ");
    putInt(fd, static_cast<long>(::getpid()));
    put(fd, ") ===\nPrzyczyna: ");
    put(fd, reason);
    put(fd, "\n");
    if (info && (info->si_signo == SIGSEGV || info->si_signo == SIGBUS)) {
        put(fd, "Adres pamięci: ");
        putHex(fd, reinterpret_cast<uintptr_t>(info->si_addr));
        put(fd, info->si_addr == nullptr ? "  (wskaźnik pusty - nullptr)\n" : "\n");
    }

    // 0. Dokładne miejsce: instrukcja, która spowodowała błąd (z rejestrów procesora w chwili sygnału).
    if (faultPc) {
        put(fd, "Miejsce awarii: ");
        writeSourceLine(fd, faultPc, false, true);
    }

    // 1. Surowy stos (bezpieczny: bez malloc) - moduł(funkcja+przesunięcie) [adres].
    void* frames[64];
    const int count = backtrace(frames, 64);
    put(fd, "\n--- Stos wywołań, surowy (najnowsze wywołanie na górze) ---\n");
    backtrace_symbols_fd(frames, count, fd);

    // 2. Czytelny stos: nazwy funkcji C++ i plik:linia (addr2line). Pomijamy ramki samej obsługi awarii.
    put(fd, "\n--- Stos wywołań: funkcja, plik:linia ---\n");
    for (int i = 0; i < count; ++i) {
        Dl_info dl{};
        if (dladdr(frames[i], &dl) && dl.dli_fname && std::strstr(dl.dli_fname, "libc.so")) {
            // Funkcje biblioteki C (np. strlen, snprintf) - nazwa wystarczy, linii i tak nie ma.
            putInt(fd, i);
            put(fd, ": ");
            put(fd, dl.dli_sname ? dl.dli_sname : "?");
            put(fd, " (libc)\n");
            continue;
        }
        putInt(fd, i);
        put(fd, ": ");
        // Adres powrotu (cofamy o 1 do instrukcji wywołania) - poza ramką awarii, gdzie adres to sama instrukcja.
        writeSourceLine(fd, frames[i], i > 0 && frames[i] != faultPc);
    }

    // 3. Ostatnie wpisy logu - co program robił tuż przed awarią.
    put(fd, "\n--- Ostatnie wpisy logu ---\n");
    engine::core::Logger::writeRecentLines(fd);
    put(fd, "\nJak czytać: pierwsza ramka spoza libc/obsługi awarii to zwykle miejsce błędu w naszym kodzie.\n");
}

void onSignal(int sig, siginfo_t* info, void* context) {
    void* faultPc = nullptr;
#if defined(__x86_64__)
    if (context && sig != SIGABRT) faultPc = reinterpret_cast<void*>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    if (context && sig != SIGABRT) faultPc = reinterpret_cast<void*>(static_cast<ucontext_t*>(context)->uc_mcontext.pc);
#endif
    const int fd = openCrashFile();
    if (fd >= 0) {
        writeReport(fd, signalDescription(sig), info, faultPc);
        ::close(fd);
    }
    // SA_RESETHAND przywrócił domyślną obsługę - sygnał jeszcze raz: system kończy program (i robi coredump).
    ::raise(sig);
}

// Nieobsłużony wyjątek C++: zapisujemy jego treść, potem abort() -> SIGABRT -> pełny raport ze stosem.
void onTerminate() {
    const char* what = "nieznany wyjątek";
    try {
        if (auto e = std::current_exception()) std::rethrow_exception(e);
    } catch (const std::exception& e) {
        what = e.what();
    } catch (...) {
    }
    LOG_CRITICAL("Nieobsłużony wyjątek C++: {}", what);
    std::abort();
}

} // namespace

void installCrashHandler(const std::string& directory) {
    std::snprintf(g_directory, sizeof(g_directory), "%s", directory.c_str());
    ::mkdir(g_directory, 0755);   // katalog na raporty (błąd "już istnieje" jest w porządku)
    if (const char* name = program_invocation_short_name) std::snprintf(g_program, sizeof(g_program), "%s", name);

    // backtrace() przy pierwszym użyciu ładuje bibliotekę (malloc) - robimy to teraz, nie w obsłudze sygnału.
    void* warmup[2];
    backtrace(warmup, 2);

    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(sig, &sa, nullptr);
    std::set_terminate(onTerminate);
}

#else
void installCrashHandler(const std::string&) {}
#endif

} // namespace engine3d
