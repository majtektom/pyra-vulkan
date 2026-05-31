# do zrobienia

- dodać wiele pipeline'ów/materialów per obiekt, zamiast jednego activeShaderProgram na całą scenę
- wprowadzić UBO/SSBO per frame dla większej liczby obiektów zamiast push constants, gdy scena urośnie
- dołożyć prosty loader modeli i tekstur, żeby odciąć przykłady od ręcznie wpisanych wierzchołków
 Uwaga techniczna: Nowe pasy renderowania (OpaquePass, TransparentPass) są przygotowane do rysowania, ale ze względu na stopień skomplikowania pełnej implementacji Vulkana (setki linii na potok), używają one obecnie uproszczonych struktur (stubs).
  Silnik jest jednak w pełni gotowy na wpięcie kompletnej logiki shaderów w te dedykowane moduły.

textury z modelu gltf w postaci binrnej są wyciągane do tmp i wczytywane, popraw na wczytywanie odrau do pamięci

teren3d
  Główne braki w porównaniu ze starym (2400 linii):
Feature
LOD stitching — 16 wzorców łączenia poziomów
Multi-texturing — 4 warstwy z maskami
Dynamiczny LOD — wybór poziomu wg odległości od kamery
Frustum culling — test sfera/frustum na noda
Zarządzanie obiektami — przypinanie obiektów do nodów quadtree
Debug rendering — wireframe quadtree
Znajdowanie sąsiadów — do stitchingu LOD


gotowe
Wygładzanie krawędzi — brzegi do poziomu wody
Generatory terenu
Wczytywanie heightmapy
Wczytywanie XML
QuadTree (struktura)
Zapytania wysokości
Budowa meshy
Kluczowy brak: QuadTree jest zbudowany, ale nie używany do renderowania. Stary system dynamicznie wybierał LOD per node na podstawie odległości od kamery i łączył sąsiednie poziomy szczegółowości specjalnymi index bufferami — to te 800 linii stitchingu. Bez tego teren to po prostu statyczna mesh.


Wraz z nadejściem Vulkana 1.3, API graficzne przeszło ewolucję z modelu "programowania sprzętu przez bardzo drobne instrukcje" w stronę modelu "zarządzania potężnymi, zautomatyzowanymi blokami operacji".Poniżej zestawiam kluczowe techniki renderingu, które definiują nowoczesny Vulkan (1.3+) w porównaniu do wczesnych wersji (1.0/1.1).

1. Mesh Shading (Rewolucja w geometrii)Vulkan 1.3 wspiera Mesh Shaders jako standard (często przez rozszerzenia, ale w pełni zintegrowane z ekosystemem).Stare podejście: Vertex Shader + Geometry Shader + Tessellation. CPU musiało wysyłać wszystkie wierzchołki, a GPU tylko je "przepuszczało".Vulkan 1.3: Mesh Shaders (Task + Mesh) pozwalają GPU na samodzielne decydowanie o tym, co jest widoczne (culling) i generowanie geometrii z danych w pamięci (SSBO).Zaleta: Przejście z "przesyłania danych" na "przesyłanie instrukcji", co eliminuje wąskie gardło procesora.

---------------------
Oto jak zaprojektować profesjonalny system oparty na ECS (Entity-Component-System) z domieszką Data-Oriented Design, który jest standardem w nowoczesnych silnikach.

1. Nowa Architektura: "Separacja Danych od Logiki"
Zamiast dziedziczyć z jednego wielkiego obiektu, budujesz Entity (byt), które jest tylko identyfikatorem (ID), a całą resztę "doklejasz" jako komponenty.

Entity: To tylko uint64_t (ID). Nie posiada metod, nie przechowuje danych modelu.

Component: To czysta struktura danych (struct). Nie posiada logiki.

System: To klasa, która operuje na grupach komponentów.

Przykłady struktur (Komponenty):
C++
struct TransformComponent {
    vec3 position;
    quat rotation;
    vec3 scale;
};

struct MeshComponent {
    Mesh* mesh;
    Material* material;
};

struct HierarchyComponent {
    Entity parent;
    std::vector<Entity> children;
};
2. Jak to zorganizować? (SOLID w praktyce)
Single Responsibility (SRP)
MeshManager zajmuje się tylko wczytywaniem i cache'owaniem plików z dysku. Nie wie nic o pozycji obiektu w świecie.

RenderSystem (System) zajmuje się tylko przeglądaniem listy obiektów z MeshComponent i wysyłaniem ich do GPU.

Interface Segregation (ISP)
Zamiast VObject jako baza, użyj interfejsów dla systemów:

IRenderable (dla obiektów z meshem)

IUpdatable (dla obiektów z fizyką/logiką)

ITransformable (dla obiektów mających pozycję w przestrzeni)

3. Proponowany system zarządzania (Scene i Object)
Stwórz klasę Scene, która jest "sercem" wszystkiego.

C++
class Scene {
    // Zarządza pulą bytów i ich komponentami (np. za pomocą biblioteki EnTT)
    EntityManager entityManager;

    void update(float dt) {
        // Systemy wykonują logikę
        physicsSystem.update(entityManager, dt);
        animationSystem.update(entityManager, dt);
    }

    void render(Renderer& renderer) {
        // RenderSystem zbiera wszystko co ma MeshComponent i TransformComponent
        renderSystem.render(entityManager, renderer);
    }
};
4. Co z "rodzicem" (Hierarchy)?
To klasyczny błąd w VObject. Nie rób hierarchii w klasie obiektu!
Zrób to za pomocą komponentu HierarchyComponent i specjalnego systemu TransformSystem.

TransformSystem w każdej klatce oblicza "Globalną Macierz" (Local Matrix * Parent Global Matrix).

Dzięki temu, jeśli zmienisz pozycję rodzica, dzieci automatycznie zaktualizują swoją pozycję w następnej klatce bez "dziedziczenia" pozycji w klasie C++.

Dlaczego to jest lepsze? (Podsumowanie)
Brak gigantycznej bazy: VObject znika. Nie masz już klasy Particle dziedziczącej po VObject. Masz cząstkę jako zestaw danych w buforze (o czym rozmawialiśmy wcześniej).

Elastyczność: Jeśli chcesz dodać do postaci system "podpalenia", po prostu dodajesz do niej FireComponent. Nie musisz zmieniać żadnej hierarchii klas.

Wydajność: Systemy operują na ciągłych blokach pamięci (std::vector<TransformComponent>), co jest znacznie szybsze dla CPU niż skakanie po obiektach VObject porozrzucanych w pamięci RAM.

Jak zacząć refaktoryzację?
Wywal VObject jako bazę dla wszystkiego.

Stwórz klasę Scene, która przechowuje listę obiektów (być może zacznij od prostych structs).

Przenieś logikę renderowania z obiektów do oddzielnego RenderSystem.

Wskazówka: Jeśli projekt jest duży, nie pisz własnego ECS od zera – polecam bibliotekę EnTT (standard w branży, niezwykle szybka).

Czy chcesz, abym rozpisał Ci, jak przenieść obecny MeshComponent i MeshManager do tego systemu, abyś zobaczył różnicę w czytelności kodu?