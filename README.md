<p align="center">
  <img src="docs/assets/bruma-reader-banner.png" alt="Bruma Reader: un libro abierto entre paisajes de bruma" width="100%" />
</p>

# Bruma Reader

**Tu biblioteca, un momento de calma.**

Bruma Reader es un firmware de lectura para **Xteink X4**. Reúne los libros de la SD en una biblioteca, muestra el tiempo estimado que queda de lectura y ofrece un inicio con el libro actual y tus estadísticas.

Es un fork de **[CPR-vCodex](https://github.com/franssjz/cpr-vcodex)**, basado a su vez en **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**. Bruma conserva esa base y sus herramientas de lectura, estadísticas y personalización. Este repositorio no es el proyecto oficial de CrossPoint ni está afiliado con Xteink.

[Guía de uso](USER_GUIDE.md) · [Simulador](agent-docs/simulator.md) · [Fuentes SD](docs/sd-card-fonts.md) · [Documentación original](README.cpr-vcodex.md)

## Qué añade Bruma

| Función | Qué puedes hacer |
|---|---|
| **Biblioteca** | Ver los libros de distintas carpetas de la SD en una sola lista paginada, con título y autor. |
| **Orden y filtros** | Ordenar por título, autor, últimos leídos o recién añadidos; ver todos, pendientes, leyendo o terminados. |
| **Estados de lectura** | Marcar un libro como terminado o deshacer esa marca sin alterar su progreso ni el tiempo leído. |
| **Tiempo restante** | Mostrar una estimación del libro o capítulo en la barra de lectura, o dejarla oculta. |
| **Inicio Bruma** | Consultar el libro actual, lecturas recientes, objetivo diario y estadísticas, con accesos a Biblioteca y Apps. |
| **Fuentes desde la SD** | Instalar archivos `.cpfont` sin Wi-Fi, desde Ajustes o el navegador de archivos. |

### Todos tus libros en un listado

Biblioteca recorre la SD y sus subcarpetas visibles. Incluye **EPUB, XTC, TXT y Markdown**, omite imágenes y directorios ocultos o de sistema, y abre cada libro desde su ubicación original.

- En la primera entrada, pulsa **Crear biblioteca**.
- Después, utiliza **Actualizar biblioteca** en el menú cuando añadas, cambies o borres archivos.
- La actualización muestra el progreso y permite cancelar. Un error o una cancelación conserva el catálogo anterior.
- El orden, el filtro y la selección se conservan al volver de la lectura.
- Si faltan metadatos, se muestra el nombre del archivo. Las copias en rutas distintas aparecen por separado.
- Los estados de lectura se obtienen del progreso y las estadísticas, con la posibilidad de marcarlos manualmente.

El catálogo se guarda en `/.crosspoint/`. La actualización no organiza, mueve ni modifica tus libros. **Archivos** sigue disponible en Apps para navegar por carpetas. Esta biblioteca no incorpora búsqueda por texto ni portadas.

### Saber cuánto queda

En la personalización de la barra, **Tiempo restante** permite elegir **Oculto**, **Libro** o **Capítulo**. El valor inicial es **Libro** y también se puede cambiar desde la configuración web.

La estimación se presenta como `~25 min` o `~1 h 20 min`. Empieza con un ritmo provisional de 60 segundos por página y aprende de avances manuales normales; descarta pausas largas, saltos, retrocesos y avance automático. El ritmo se guarda por libro.

Funciona en EPUB, XTC, TXT y Markdown. En documentos sin capítulos, la opción Capítulo representa el documento completo. Es una orientación: el tamaño de página y tu forma de leer afectan al resultado.

### Un inicio centrado en la lectura

El tema opcional **Bruma** reúne el libro actual, lecturas recientes, tiempo leído, objetivo del día, racha y libros terminados. Puedes seleccionarlo en los ajustes de tema de la interfaz. Los otros temas y la personalización de accesos siguen disponibles.

### Instalar tipografías sin conexión

1. Copia un archivo compatible a cualquier carpeta de la SD, por ejemplo `Lexend_14.cpfont`.
2. Abre **Ajustes > Lector > Instalar fuente desde archivo**, o selecciona el archivo desde **Archivos**.
3. Confirma el reemplazo si ese tamaño ya está instalado.
4. Elige la familia en **Ajustes > Lector > Tipografía**.

El archivo original se conserva. Se instala únicamente ese tamaño y se mantienen los otros tamaños de la familia. Esta opción utiliza el formato `.cpfont`; no convierte archivos TTF u OTF. Consulta la [guía de fuentes](docs/sd-card-fonts.md) para descargar paquetes o convertir tipografías.

## Lo que conserva de CPR-vCodex

Bruma mantiene las capacidades de la base, entre ellas:

- Estadísticas de lectura, objetivos, rachas, mapa de calor, historial de sesiones y logros.
- **Sync Day**, para disponer de una fecha de referencia en las estadísticas diarias.
- Diccionarios StarDict desde la SD, resaltados EPUB y marcas de página.
- Fuentes SD, temas, ajustes de lectura y pantallas de suspensión personalizadas.
- Transferencia de archivos y configuración web, OPDS y sincronización con KOReader.
- Herramientas de mantenimiento y actualización de firmware heredadas.

**Flashcards se ha retirado de Bruma** para centrar el proyecto en la lectura. Las preferencias heredadas de otros accesos se conservan.

La [documentación original de CPR-vCodex](README.cpr-vcodex.md) conserva los créditos, el historial técnico y las notas de recuperación de esa base. Sus enlaces a releases y Auto Flash pertenecen a CPR-vCodex; no distribuyen necesariamente las funciones añadidas por Bruma.

## Compatibilidad y estado

El desarrollo de Bruma se centra en **Xteink X4 / ESP32-C3**. La base conserva soporte de paneles X3, pero las novedades de Bruma requieren validación física antes de considerar ese dispositivo probado.

**X4 Pro no es un objetivo de distribución.** Se mantiene la retirada de sus binarios establecida por CPR-vCodex.

La revisión local de Bruma ha pasado **291 pruebas de host**, comprobaciones en el simulador X4 y compilaciones `default` y `gh_release`. La biblioteca se ha comprobado con más de 500 libros, incluyendo errores de almacenamiento y memoria insuficiente. La velocidad, legibilidad y comportamiento de las novedades en el X4 real siguen pendientes de validación física.

El nombre Bruma identifica este proyecto. Los identificadores internos, rutas `/.crosspoint/` y nombres de artefactos `cpr-vcodex` se mantienen por compatibilidad. El banner y este README no cambian la marca dentro del firmware.

## Probarlo en tu ordenador

Clona el repositorio con sus submódulos:

```bash
git clone --recurse-submodules https://github.com/fresquer/bruma-reader.git
cd bruma-reader
```

Necesitas [pioarduino PlatformIO Core](https://github.com/pioarduino/platformio-core) y Python 3.10–3.14. Para el simulador, instala SDL2; en macOS:

```bash
brew install sdl2
./bin/run-simulator.sh
```

Pon tus libros en `fs_/`, que funciona como la SD simulada. Pueden estar en distintas subcarpetas. Para abrir una compilación existente sin volver a compilar:

```bash
./bin/run-simulator.sh --no-build
```

| Tecla | Acción |
|---|---|
| Flechas | Navegar o cambiar de página, según la pantalla |
| Enter | Seleccionar o abrir |
| Enter mantenido | Abrir el menú de Biblioteca |
| Escape | Volver |
| P | Botón de encendido |
| S | Suspender |

En Linux utiliza `pio run -e simulator -t run_simulator` tras instalar `libsdl2-dev` y `libssl-dev`. En Windows, utiliza WSL. La [guía del simulador](agent-docs/simulator.md) incluye la configuración y las pruebas de pantallas.

El simulador permite revisar navegación y presentación; no reproduce el refresco físico de tinta electrónica ni valida una instalación de firmware.

## Compilar y comprobar

Desde la raíz del repositorio, con los submódulos inicializados:

```bash
VCODEX_RELEASE_DRY_RUN=1 pio run -e default -j 1
VCODEX_RELEASE_DRY_RUN=1 pio run -e gh_release -j 1
```

El modo de verificación evita avanzar los contadores de versión. En PowerShell, configura `$env:VCODEX_RELEASE_DRY_RUN = "1"` antes de ejecutar `pio`, y elimina la variable al terminar.

Pruebas de host:

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

Comprobaciones de Biblioteca y lectores, con el simulador ya compilado:

```bash
python3 test/simulator/library_catalog.py --large
python3 test/simulator/library_states.py
python3 test/simulator/reading_time.py
python3 test/simulator/home_dashboard.py
```

Consulta [la guía de contribución](docs/contributing/README.md) y [AGENTS.md](AGENTS.md) para las restricciones del firmware y los flujos de trabajo. Las compilaciones locales no publican ni instalan firmware por sí solas.

## Documentación

- [Guía de uso](USER_GUIDE.md)
- [Simulador y pruebas](agent-docs/simulator.md)
- [Fuentes desde la SD](docs/sd-card-fonts.md)
- [Diccionarios](docs/dictionary.md)
- [Servidor web](docs/webserver.md)
- [Traducciones](docs/i18n.md)
- [Auditoría de estabilidad de la base](agent-docs/stability-audit-2026-09.md)
- [README heredado de CPR-vCodex](README.cpr-vcodex.md)
- [Historial heredado](CHANGELOG.md)

## Origen, créditos y licencia

La cadena del proyecto es:

**[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) → [CPR-vCodex](https://github.com/franssjz/cpr-vcodex) → [Bruma Reader](https://github.com/fresquer/bruma-reader)**

Gracias a Dave Allie y los colaboradores de CrossPoint Reader por la base, y a **franssjz** y los colaboradores de CPR-vCodex por las ampliaciones sobre las que se construye Bruma.

Se conservan también los créditos a **zgredex** por Lyra Carousel, **erickosanchezj** por su adaptación a CPR-vCodex, **Julia Nguyen (uxjulia)** y [CrossInk](https://github.com/uxjulia/CrossInk) por el trabajo de resaltados incorporado en la base, y **Which-Estimate4566** por el logo original de CPR-vCodex. El nuevo banner de Bruma es una ilustración generada con IA para este proyecto.

El código mantiene la [licencia MIT original](LICENSE), incluido su aviso de copyright. Los componentes, tipografías y otros recursos de terceros conservan sus propias licencias.
