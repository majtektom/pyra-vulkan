# Vulkan od podstaw na kodzie tego silnika

Ten rozdział wyjaśnia mechanizmy, które w opisach API łatwo sprowadzić do
samych nazw. Czytaj go przed [backendem](08_render_backend.md),
[przebiegami](09_render_potoki.md) i [teksturami](11_render_tekstury.md).
Przykłady są fragmentami do analizy, nie samodzielnymi programami.

## 1. CPU zleca, GPU wykonuje

Wywołanie `vkCmdDrawIndexed` **nagrywa polecenie** w buforze komend. Dopiero
`vkQueueSubmit` wysyła nagraną pracę do kolejki GPU. Powrót z tej funkcji nie
oznacza zakończenia rysowania. CPU może już przygotowywać następne dane,
podczas gdy GPU wciąż czyta poprzednie.

Dlatego renderer musi odpowiedzieć na trzy osobne pytania:

| Pytanie | Mechanizm |
|---|---|
| Kiedy CPU może nadpisać UBO lub ponownie nagrać command buffer? | fence lub oczekiwanie na wartość timeline |
| Kiedy GPU może rysować do pozyskanego obrazu, a prezentacja go pokazać? | semafory acquire → submit → present |
| Kiedy następny przebieg GPU może odczytać wcześniejsze zapisy? | bariery etapów i pamięci, przejścia layoutów |

Kolejność wywołań C++ nie rozwiązuje wszystkich tych zależności. Etapy GPU
mogą pracować potokowo, a zasoby mają pamięci podręczne. Kod odniesienia:
[`RendererImpl::render`](../engine/render/renderer_core.cpp).

## 2. Swapchain: jak obraz trafia na ekran

**Co to jest?** Swapchain to zestaw obrazów powiązany z powierzchnią okna
(`VkSurfaceKHR`) i mechanizmem prezentacji. Aplikacja pozyskuje obraz,
zleca rysowanie i oddaje go do prezentacji. Nie może dowolnie nadpisywać obrazu
używanego przez system wyświetlania.

**Po co?** Kilka obrazów pozwala przygotowywać następną klatkę, gdy inny obraz
czeka na wyświetlenie. Sposób oczekiwania zależy od trybu prezentacji.
[Opis swapchaina w dokumentacji Khronos](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html).

W [`VulkanSwapchain::createSwapchain`](../engine/render/backend/vulkan_swapchain.cpp)
silnik wybiera format (preferuje BGRA8 sRGB), rozmiar dopuszczony przez
powierzchnię i liczbę obrazów: żąda co najmniej `kMaxFrames`, z uwzględnieniem
`minImageCount` i `maxImageCount`. Faktyczną liczbę odczytuje z Vulkan API.
Osobno tworzy widoki i obrazy głębokości. **Głębia nie jest obrazem dostarczanym
przez swapchain** — to dodatkowy zasób zarządzany przez naszą klasę.

| Tryb | Zasada | Skutek |
|---|---|---|
| FIFO | gotowe obrazy czekają w kolejce na odświeżenie | VSync, możliwe oczekiwanie na wolny obraz |
| Mailbox | nowszy gotowy obraz zastępuje oczekujący | mniej opóźnienia kolejki, część klatek nie zostanie pokazana |
| Immediate | prezentacja bez oczekiwania na granicę odświeżenia | możliwe rozrywanie obrazu (tearing) |

Silnik wybiera żądany tryb, jeśli powierzchnia go zgłasza; w przeciwnym razie
wraca do FIFO. Trzy obrazy nie gwarantują braku oczekiwania w acquire.

### Indeks klatki a indeks obrazu

`currentFrame_` krąży po **3 slotach danych CPU/GPU**: UBO, command bufferach,
deskryptorach i obiektach synchronizacji. `imageIndex` zwrócony przez acquire
wskazuje **obraz swapchaina**. Nie należy zakładać, że są równe, nawet gdy obie
tablice mają po trzy elementy.

```text
slot klatki 0 → acquire zwraca obraz 2 → submit → present obrazu 2
slot klatki 1 → acquire zwraca obraz 0 → submit → present obrazu 0
```

Na początku użycia slotu renderer czeka na jego zakończenie, aktualizuje
deskryptory, pozyskuje obraz, nagrywa pracę i wysyła ją na GPU. Submit czeka na
`imageAvailableSemaphores_[currentFrame_]`. Prezentacja czeka na semafor
zakończenia renderowania przypisany do **obrazu**. Fence wysłanej pracy nie
oznacza jeszcze, że prezentacja skończyła używać jej semafora.

### Zmiana rozmiaru

Rozmiar obrazów jest ustalony przy utworzeniu. Resize wymaga odtworzenia
swapchaina oraz zależnych zasobów renderera. Obecna implementacja zatrzymuje
urządzenie przez `vkDeviceWaitIdle`, aby bezpiecznie usunąć stare zasoby.
Zminimalizowane okno jest pomijane w `render()`.

`VK_ERROR_OUT_OF_DATE_KHR` w acquire prowadzi do przebudowy i pominięcia
rysowania. Przy `VK_SUBOPTIMAL_KHR` obraz został pozyskany: kod kończy klatkę;
`present()` może następnie zgłosić potrzebę przebudowy. Nie wolno porzucić
pozyskanego obrazu i zasygnalizowanego semafora tak, jakby acquire się nie odbyło.

## 3. Fence, semafor binarny i timeline

**Fence** informuje CPU, że wysłana praca GPU się zakończyła. Przed ponownym
wykorzystaniem slotu silnik woła `vkWaitForFences`. Fence resetuje dopiero po
udanym acquire — reset przed acquire zakończonym błędem zostawiłby slot
czekający na pracę, której nigdy nie wysłano.

**Semafor binarny** łączy operacje wykonywane asynchronicznie: acquire sygnalizuje,
submit czeka; submit sygnalizuje, present czeka. Zwykle CPU nie czeka na niego
wprost tak jak na fence.

**Semafor timeline** ma rosnącą wartość. Submit sygnalizuje numer zakończonej
pracy, a CPU czeka przez `vkWaitSemaphores` na potrzebną wartość. Ten renderer
ma semafor timeline na slot klatki (`renderTimelineSemaphores_`), liczniki
slotów i wspólny rosnący licznik zgłoszeń. Nadal używa binarnych semaforów
do acquire i prezentacji.

Ważna zgodność z kodem: `SynchronizationMode::Off` **obecnie przechodzi przez
tę samą ścieżkę fence co Standard**. Nazwa nie oznacza rzeczywistego wyłączenia
czekania. Zobacz warunki `Timeline` / `else` w `render()`.

## 4. Bariery i synchronization2

**Problem:** G-Buffer jest zapisywany jako cel renderowania, a następnie czytany
jako tekstura w deferred. Trzeba określić producenta, konsumenta i wymagany
layout. Bariera opisuje tę zależność na GPU; nie jest oczekiwaniem CPU.

**Dlaczego synchronization2?** Nowsze API łączy etapy i dostępy w strukturze
każdej bariery oraz zbiera bariery w `VkDependencyInfo`. Ułatwia wyrażenie
zależności i rozszerza maski do 64 bitów. Weszło do rdzenia w Vulkanie 1.3;
ten silnik wymaga 1.4 i używa nazw rdzenia. Nie należy mylić wersji API
z `SynchronizationMode::Timeline` — są to różne zagadnienia.
[Zmiany synchronization2 według Khronos](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html).

### Jak dobrać pola

| Pole `vk::ImageBarrier` | Pytanie | Przykład G-Buffer → deferred |
|---|---|---|
| `srcStage` | który etap produkował dane? | `COLOR_ATTACHMENT_OUTPUT` |
| `srcAccess` | jaki dostęp ma zostać udostępniony? | `COLOR_ATTACHMENT_WRITE` |
| `dstStage` | który etap potrzebuje danych? | `FRAGMENT_SHADER` |
| `dstAccess` | jak będzie ich używał? | `SHADER_SAMPLED_READ` |
| `oldLayout` / `newLayout` | jakie są dotychczasowe i następne zastosowania obrazu? | attachment → shader read |
| `aspect`, `baseMip`/`mipCount`, `baseLayer`/`layerCount` | jakiej części obrazu dotyczy przejście? | kolor i odpowiednie poziomy/warstwy |

Layout jest stanem użytkowania obrazu z punktu widzenia API. Może wiązać się
z optymalizacjami reprezentacji danych przez sterownik. Samo wpisanie innego
layoutu w deskryptorze nie wykonuje przejścia.

```cpp
// Fragment edukacyjny: img to obraz G-Buffera po zapisie koloru.
vk::imageBarrier(cmd, {
    .image = img,
    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
});
```

`srcStage` i `dstStage` ograniczają kolejność wykonania. Maski dostępu określają
operacje pamięci objęte udostępnieniem i widocznością zapisów. Pozwala to
uniknąć sytuacji, w której shader widzi nieaktualną zawartość cache.

Trzy typy konfliktów:

* **RAW, read after write:** zapis → odczyt. Potrzebne są kolejność i widoczność
  zapisu, np. G-Buffer → deferred.
* **WAR, write after read:** odczyt → nadpisanie. Samą zależność wykonania
  można wyrazić bez masek dostępu; nie ma wcześniejszego zapisu do udostępnienia.
* **WAW, write after write:** dwa zapisy muszą być właściwie uporządkowane,
  aby późniejszy wynik był tym oczekiwanym.

Nie kopiuj jednej bariery do każdego zastosowania. Głębia powstaje w etapach
testów fragmentów, transfer w etapach kopiowania, a compute w compute shaderze.
Bariera fragment shader → fragment shader nie obejmie automatycznie odczytu
heightmapy w etapie teselacji. Przykłady do porównania:
[zależności pamięci Khronos](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

### `UNDEFINED`, `NONE` i zakres bariery

`oldLayout = UNDEFINED` oznacza, że **rezygnujesz z zachowania zawartości**.
Nadaje się do nowego obrazu lub celu, który zostanie w całości zastąpiony.
Nie oznacza „nie wiem, w jakim jest stanie” i nie usuwa obowiązku
uporządkowania wcześniejszych użyć tego samego zasobu.

`srcStage = NONE`, `srcAccess = NONE` jest poprawne np. przy pierwszym użyciu
nowego obrazu, gdy brak wcześniejszego producenta. Puste `srcAccess` nie jest
więc samo w sobie błędem. Analogicznie końcowe przejście do prezentacji ma
w kodzie `dstStage = NONE`: dalsze przekazanie do prezentacji zabezpiecza
semafor submit → present.

[`vk::imageBarriers`](../engine/render/vulkan_util.cpp) zamienia opisy na
`VkImageMemoryBarrier2` i wywołuje `vkCmdPipelineBarrier2`.
`vk::memoryBarrier` tworzy ogólną `VkMemoryBarrier2`, bez przejścia layoutu.
Helper obrazów nie jest kompletnym systemem transferów własności między
rodzinami kolejek; używa `VK_QUEUE_FAMILY_IGNORED`.

Ćwiczenie: znajdź w `renderer_gbuffer_pass.cpp` przejście do odczytu i wskaż
jego konsumenta w `renderer_deferred_pass.cpp`. Następnie prześledź kopiowanie
koloru w `renderer_scene_copy.cpp`. Dla każdego obrazu zapisz: poprzednie
użycie, kolejne użycie, etap, dostęp i layout.

## 5. Deskryptory i bindless

**Deskryptor** opisuje zasób dostępny dla shadera, np. bufor albo widok obrazu
z samplerem. `VkDescriptorSetLayout` określa układ takich wpisów, a
`VkDescriptorSet` zawiera konkretne wpisy. `VkPipelineLayout` określa zestawy
i zakresy push constants oczekiwane przez potok.

**Po co bindless?** Przy wielu materiałach każda zmiana zestawu tekstur może
wymagać kolejnego wiązania deskryptorów. Silnik zamiast tego udostępnia dużą
tablicę tekstur; obiekt przekazuje tylko indeksy albedo, normalnej, ORM itd.
Bindless ogranicza zmiany zasobów przy rysowaniu kolejnych obiektów. Nie
usuwa draw calli ani obowiązku wiązania globalnego zestawu.
[Mechanizm descriptor indexing w przykładzie Khronos](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html).

```text
TextureHandle (ID zasobu)
     ↓ textureHandleToIndex_
indeks tablicy bindless
     ↓ MaterialPushConstants / GlobalUBO
textures[indeks] w HLSL
```

**Uchwyt zasobu nie musi mieć tej samej liczby co indeks bindless.** Sprawdź
`registerTextureInBindless` w
[`renderer_textures.cpp`](../engine/render/renderer_textures.cpp).

Globalny set 0 ma binding 0 = UBO klatki, binding 1 = tablica 1024
`COMBINED_IMAGE_SAMPLER`, binding 2 = mapa cieni. Jego kopia istnieje dla
każdego slotu klatki. HLSL deklaruje `Texture2D textures[]` i sampler w
[`globals.hlsl`](../assets/shaders/shared/globals.hlsl).

Flagi bindingu tekstur wyjaśniają zasady aktualizacji:

| Flaga | Znaczenie dla tej implementacji |
|---|---|
| `PARTIALLY_BOUND` | nie każdy slot musi być wypełniony; shader nadal musi czytać prawidłowy wpis |
| `UPDATE_AFTER_BIND` | dopuszcza aktualizacje po związaniu zestawu, z zachowaniem zasad dostępu |
| `UPDATE_UNUSED_WHILE_PENDING` | pozwala dopisać nieużywany wpis, mimo że zestaw obsługuje pracę w locie |

Nowa nazwa otrzymuje nowy indeks i wpis we wszystkich setach. Podmiana
istniejącej tekstury zachowuje indeks, ale `queueDescriptorWrite` rozkłada
zmianę na poszczególne sloty. `applyDescriptorWrites` aktualizuje set dopiero
po oczekiwaniu na GPU tego slotu. `retireTexture` opóźnia zniszczenie starego
obrazu i samplera, bo starsze klatki wciąż mogą ich używać.

Koszt rozwiązania: deskryptory na każdy slot, obsługa czasu życia i stały limit
tablicy. Aktualny kod nie sprawdza `nextTextureIndex_` względem 1024 i nie
odzyskuje indeksów. Przekroczenie nie ma bezpiecznej obsługi błędu.

## 6. Push constants, UBO i push descriptors

| Mechanizm | Co przenosi | Dlaczego go użyto |
|---|---|---|
| UBO (`GlobalUBO`) | kamera, światła, parametry wspólne klatki | wiele obiektów czyta te same dane |
| push constants | model, parametry materiału, indeksy tekstur | mały zestaw danych zmieniany pomiędzy draw callami |
| push descriptors | opisy zasobów zapisane poleceniem w command bufferze | wygodne zestawy wejść konkretnego przebiegu, np. deferred |

Push descriptor opisuje **gdzie są dane**. Push constants niosą **same dane**.
Bindless jest sposobem organizacji zasobów, a nie zamiennikiem każdego z tych
mechanizmów. Wszystkie występują w tym rendererze równocześnie.

Układ C++ i HLSL musi zgadzać się bajt w bajt: kolejność pól, wyrównanie,
rozmiar i interpretacja macierzy. `MaterialPushConstants` ma 240 B; layouty
materiałów rezerwują 256 B. `static_assert` sprawdza rozmiar C++, a nie całą
zgodność ze shaderem ani limit fizycznego urządzenia.

## 7. Dynamic rendering, staging, VMA i host image copy

**Dynamic rendering** pozwala wskazać załączniki koloru i głębi bez tworzenia
osobnych obiektów `VkRenderPass` i `VkFramebuffer`. `vkCmdBeginRendering`
rozpoczyna przebieg z określonymi widokami, sposobem czyszczenia i zachowania
wyniku. To upraszcza tworzenie wariantów przebiegów. Nadal trzeba dopasować
formaty w pipeline i samodzielnie wykonać bariery — nie powstaje automatyczny
graf zależności.

**Staging** to bufor pośredni dostępny CPU: `memcpy` wypełnia staging,
polecenie transferu kopiuje do zasobu GPU, a odpowiednia zależność udostępnia
wynik shaderowi lub pobieraniu wierzchołków. Bufora pośredniego nie można
usunąć przed zakończeniem kopiowania. `DEVICE_LOCAL` i `HOST_VISIBLE` to
różne właściwości, które na części sprzętu mogą występować razem; sam napis
`DEVICE_LOCAL` nie dowodzi, że pamięć jest niemapowalna. Siatki statyczne tego
silnika są przesyłane przez staging.

**VMA** dobiera i poddziela alokacje pamięci dla zasobów, np. `VulkanBuffer`.
Nie steruje kolejnością pracy GPU ani życiem deskryptorów. Część zasobów,
w tym głębia swapchaina, nadal używa ręcznych alokacji Vulkan — nie wszystkie
obrazy przechodzą przez VMA.

**Host image copy** kopiuje dane CPU do obrazu bez nagrywania transferu GPU.
Użyto go do małych aktualizowanych obrazów (mgła wojny, GUI), żeby uprościć
upload. Obraz musi mieć obsługiwany format/layout i flagę host transfer.
Kopia nadal kosztuje czas i nadal wymaga, aby GPU nie korzystało równocześnie
z tego obrazu. Dlatego renderer trzyma kopie na sloty klatek i aktualizuje
tylko bezpieczny slot.

## 8. Sprawdź zrozumienie

1. Dlaczego po powrocie z `vkQueueSubmit` nie można od razu usunąć tekstury?
   GPU może jeszcze jej nie odczytać; potrzebne jest potwierdzenie zakończenia
   wszystkich użyć lub odroczone zwolnienie.
2. Czy bariera G-Buffer → deferred pozwala CPU nadpisać UBO? Nie. Ta bariera
   opisuje inne zasoby i zależność GPU, a slot UBO chroni fence/timeline.
3. Czy `UNDEFINED` jest sposobem na zachowanie poprzedniego koloru? Nie,
   pozwala odrzucić poprzednią zawartość.
4. Dlaczego po dodaniu bindless nadal są `vkCmdBindDescriptorSets`? Shader
   potrzebuje dostępu do globalnej tablicy, a inne zestawy obsługują np. kości.
5. Co trzeba sprawdzić przy nowym przebiegu? Jego wejścia i wyjścia, czas życia,
   formaty, layouty oraz zależności wobec wcześniejszych i późniejszych użyć.
