# do zrobienia

- dodać wiele pipeline'ów/materialów per obiekt, zamiast jednego activeShaderProgram na całą scenę
- wprowadzić UBO/SSBO per frame dla większej liczby obiektów zamiast push constants, gdy scena urośnie
- dołożyć prosty loader modeli i tekstur, żeby odciąć przykłady od ręcznie wpisanych wierzchołków
 Uwaga techniczna: Nowe pasy renderowania (OpaquePass, TransparentPass) są przygotowane do rysowania, ale ze względu na stopień skomplikowania pełnej implementacji Vulkana (setki linii na potok), używają one obecnie uproszczonych struktur (stubs).
  Silnik jest jednak w pełni gotowy na wpięcie kompletnej logiki shaderów w te dedykowane moduły.