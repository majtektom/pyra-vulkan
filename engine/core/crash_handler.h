#pragma once

#include <string>

namespace engine3d {

/**
 * @brief Zapis przyczyny awarii programu do pliku (Linux).
 *
 * Po instalacji (Engine::initialize robi to sam) program, który się wywali - błąd pamięci (SIGSEGV, SIGBUS),
 * dzielenie przez zero (SIGFPE), nieprawidłowa instrukcja (SIGILL), abort/assert (SIGABRT) albo nieobsłużony
 * wyjątek C++ - zapisuje plik <katalog>/<program>_<data>.log z:
 *   - rodzajem awarii i adresem pamięci, którego dotyczyła,
 *   - stosem wywołań (które funkcje wywołały które - najnowsza na górze), z plikiem i linią, gdy w systemie jest
 *     addr2line (pakiet binutils), a binarka ma tablice linii (-g1 w buildzie),
 *   - ostatnimi wpisami logu (co gra robiła tuż przed awarią).
 * Potem sygnał trafia dalej do systemu - zrzut pamięci (coredump) powstaje jak wcześniej.
 *
 * Uwaga dla uczących się: w funkcji obsługi sygnału wolno wołać tylko funkcje "bezpieczne dla sygnałów"
 * (write, open, backtrace_symbols_fd...). Najpierw zapisujemy to, co bezpieczne, a dopiero potem próbujemy
 * rzeczy ryzykownych (nazwy funkcji C++, addr2line) - gdyby one zawiodły, plik i tak ma surowy stos.
 */
void installCrashHandler(const std::string& directory = "crash");

} // namespace engine3d
