# Mejoras de Bruma Reader

Lista de trabajo para guardar ideas, elegir la siguiente mejora y conservar lo que se ha hecho.

Actualizado: 20 de octubre de 2026.

## Cómo usarlo

1. Añadir cada mejora con un ID permanente y una descripción concreta.
2. Antes de empezar, acordar su alcance, prioridad y cómo comprobar que está terminada.
3. Pasarla a **En curso** y anotar aquí las decisiones relevantes.
4. Marcarla **Hecha** cuando esté implementada y verificada, dejando la referencia al cambio. Indicar por separado si está publicada o pendiente de probar en el lector.

Estados: **Idea**, **Lista**, **En curso**, **Hecha**, **Aparcada**.

Prioridades: **Alta**, **Media**, **Baja**, **Por decidir**. Las ideas registradas no implican iniciar su implementación.

## Priorización recomendada — 20 de octubre de 2026

El usuario confirma que la primera preview funciona bien en su prueba. Con esa primera experiencia positiva, el orden recomendado es:

1. **BR-002 — Canal de actualización propio.** El actualizador todavía consulta CPR-vCodex. Resolver la continuidad de las actualizaciones de Bruma antes de ampliar su distribución. Como primer alcance, separar claramente los canales y conservar la actualización manual desde SD; no asumir que basta con cambiar una URL para ofrecer una OTA validada.
2. **BR-009 — Biblioteca más directa.** Es la fricción de uso diario más concreta: cambiar el orden mediante pulsación corta y un indicador visible. Empezar por el control y la presentación; añadir fechas y agrupaciones después de resolver su persistencia y los datos desconocidos.
3. **BR-005 — Apps completo aunque haya shortcuts.** Corregir la relación entre accesos directos y catálogo de aplicaciones: un acceso adicional no debe ocultar la aplicación de Apps. Es un cambio acotado y verificable.

Después: Bruma Home predeterminado (BR-007), identidad y nombre de descarga (BR-006 / BR-001), y guía específica (BR-003). Los dashboards adicionales (BR-008) quedan detrás de consolidar estas funciones. La validación física (BR-004) continúa acompañando los cambios, con la primera prueba positiva ya registrada.

El alcance inicial está implementado en la preview 1.6.0.40. El 20 de octubre se autoriza revisar, hacer commit y publicar la preview y su canal. La validación física del nuevo firmware sigue pendiente.

## Pendientes

Pendientes recogidos durante la preparación de la landing, la instalación y el uso de Bruma. Las nuevas propuestas del usuario se registran aquí antes de elegir cuál implementar.

| ID | Mejora | Estado | Prioridad |
| --- | --- | --- | --- |
| BR-001 | Identificar claramente el archivo de instalación como Bruma | Hecha (nombre para SD) | Alta |
| BR-002 | Definir un canal de actualización propio de Bruma | Hecha (preview 2) | Alta |
| BR-003 | Añadir una guía de instalación y migración desde vCodex | Idea | Por decidir |
| BR-004 | Completar la validación física de la preview en X4 | Idea | Por decidir |
| BR-005 | Mantener todas las aplicaciones en Apps aunque tengan un acceso directo | Hecha (preview 2) | Alta |
| BR-006 | Dar al firmware una identidad visual y de marca propia de Bruma | Idea | Por decidir |
| BR-007 | Usar Bruma Home como dashboard predeterminado | Idea | Por decidir |
| BR-008 | Crear más dashboards con una visión propia de Bruma | Idea | Por decidir |
| BR-009 | Cambiar el orden de la biblioteca directamente y mejorar su presentación | Hecha (preview 2) | Alta |

### BR-001 — Identificar el archivo de instalación

- **Problema:** el nombre `1.6.0.39-cpr-vcodex.bin` hace dudar de si se está descargando Bruma.
- **Resultado deseado:** reconocer el firmware de Bruma antes de instalarlo.
- **Implementado el 20 de octubre de 2026:** descarga para SD `bruma-1.6.0.40.bin`, con Bruma delante y sin el sufijo CPR-vCodex. El empaquetador genera automáticamente `artifacts/bruma-<version>.bin` en las compilaciones C3.
- **Compatibilidad:** mismos bytes, SHA-256 y versión interna `1.6.0.40`; el selector heredado acepta `.bin` y verifica el contenido, sin exigir el nombre vCodex. No cambia el actualizador del lector.
- **Alcance:** nombre de descarga para SD. Los tags conservan su formato compatible; el asset público y la URL OTA usan el nombre Bruma; la marca de la interfaz se trata en BR-006. Publicación y prueba física siguen pendientes.

### BR-002 — Canal de actualización propio

- **Problema:** los enlaces y el actualizador heredados apuntan a CPR-vCodex.
- **Resultado deseado:** que el usuario pueda distinguir y obtener las actualizaciones de Bruma sin perder sus funciones por confusión de canal.
- **Alcance implementado:** OTA de previews y estables publicadas en `fresquer/bruma-reader`, con manifiesto independiente en `docs/firmware/bruma/`, identidad de proyecto, manifiesto con certificado HTTPS verificado, tamaño, SHA-256 y validación de chip/placa. Respaldo del manifiesto por GitHub Raw; ningún respaldo a vCodex.
- **Sincronización:** workflow manual separado que selecciona la versión completa más alta, verifica los assets publicados y copia el binario antes del manifiesto; conserva cada ruta por versión. El canal local se ha sembrado con la preview .39 realmente publicada y verificada.
- **Transición:** instalar el primer candidato .40 desde SD; la preview .39 instalada conserva su OTA heredada. Publicar el canal y una futura versión superior antes de validar la OTA en el lector.
- **Verificación local:** pruebas nativas de identidad, comparación de versiones, respaldo, integridad y fallos; regresiones del sincronizador y descarga real de la preview .39. No equivale a una OTA física de este candidato.

### BR-003 — Guía de instalación y migración

- **Problema:** los pasos y límites de instalación se han explicado en el chat, pero falta una guía específica de Bruma.
- **Resultado deseado:** saber qué descargar, cómo instalarlo desde vCodex y cómo activar Bruma Home.
- **Por concretar:** modelos y versiones de origen comprobados, copia de seguridad, conservación de datos y procedimiento de vuelta atrás.
- **Comprobación pendiente de definir:** completar la guía con un recorrido real en un dispositivo identificado, sin presentar las pruebas de simulador como validación física.

### BR-004 — Validación física en X4

- **Situación:** además de las pruebas locales y de simulador, el usuario confirma el 20 de octubre de 2026 que la primera preview funciona bien en su prueba. Se registra como validación inicial positiva, sin atribuir recorridos concretos que no se hayan comunicado.
- **Resultado deseado:** documentar qué funciona en un X4 concreto y qué incidencias aparecen.
- **Por documentar:** dispositivo, versión de origen, binario instalado y recorridos realizados. La confirmación general no cierra por sí sola toda la matriz de pruebas.
- **Recorrido propuesto para acordar:** instalación, arranque, biblioteca, lectura, tiempo restante, fuentes, suspensión y reanudación.
- **Evidencia:** registrar resultados y limitaciones por recorrido. Distinguir el funcionamiento en X4 del soporte heredado para otros modelos.

### BR-005 — Los accesos directos no sustituyen a Apps

- **Petición del usuario:** Apps debe mostrar todas las aplicaciones disponibles, aunque alguna tenga también un acceso directo en el inicio.
- **Comportamiento deseado:** un shortcut es una vía adicional para abrir una aplicación. Añadirlo no oculta ni retira esa aplicación de Apps; quitarlo tampoco cambia su disponibilidad en Apps.
- **Alcance implementado:** Apps y su ordenación usan las 17 aplicaciones registradas, independientemente de la ubicación o visibilidad en Inicio. OPDS aparece sin servidores; Flashcards sigue excluido. Los ajustes distinguen Inicio + Apps de Solo Apps y conservan los valores persistidos. El regreso conserva selección y página, también al pasar por aplicaciones que reemplazan la actividad.
- **Cómo comprobarlo:** asignar, quitar y reordenar accesos directos; comprobar que las mismas aplicaciones siguen disponibles desde Apps, también después de reiniciar.

### BR-006 — Marca e icono propios de Bruma

- **Petición del usuario:** crear un icono propio de Bruma y presentar el firmware como **Bruma** en todos los lugares donde aparece la marca del producto.
- **Alcance deseado:** pantalla de arranque, loader y pantallas de carga, nombre del producto y demás superficies visibles del firmware que todavía muestran vCodex. Revisar también la interfaz web del dispositivo.
- **Dirección:** una identidad propia, coherente con el lector y adecuada para tinta electrónica. El diseño del icono se elegirá antes de integrarlo.
- **Atribuciones:** conservar el reconocimiento de CPR-vCodex y CrossPoint en los créditos y licencias, diferenciado de la marca del producto.
- **Dependencias:** coordinar con BR-001 para que el nombre visible del firmware y el archivo descargado sean coherentes. Revisar por separado los identificadores internos y rutas persistentes para conservar la compatibilidad de los datos.
- **Cómo comprobarlo:** recorrer arranque, carga, menús, información del sistema e interfaz web; confirmar la marca Bruma y la legibilidad del icono en los tamaños y estados utilizados.

### BR-007 — Bruma Home por defecto

- **Petición del usuario:** al cargar Bruma, su dashboard debe ser el predeterminado. El usuario podrá elegir otro después desde los ajustes.
- **Comportamiento deseado:** Bruma Home es la experiencia inicial del firmware; la selección posterior de otro dashboard se guarda y se respeta tras reiniciar.
- **Por concretar antes de implementar:** decidir cómo aplicar este cambio al actualizar desde vCodex o desde una instalación de Bruma que ya tenga un tema elegido, y cómo encaja con la reanudación automática del libro.
- **Cómo comprobarlo:** instalación con ajustes nuevos, actualización con ajustes existentes, selección de otro dashboard y reinicio; comprobar cada caso según la política de migración acordada.

### BR-008 — Más dashboards de Bruma

- **Petición del usuario:** desarrollar distintos dashboards que expresen su propia visión de cómo debe ser el inicio de Bruma.
- **Primeras direcciones:** uno minimalista y otro más visual, con mayor protagonismo de las portadas.
- **Resultado deseado:** poder elegir entre experiencias de inicio distintas, conservando la elección y manteniendo claras las acciones principales.
- **Por concretar:** contenido, distribución, navegación y aspecto de cada propuesta. Diseñar y seleccionar las variantes antes de implementarlas; estas dos direcciones no constituyen todavía diseños cerrados.
- **Dependencias:** coordinar con BR-007. La variante con portadas requiere comprobar la disponibilidad de imágenes, su alternativa cuando falten y el presupuesto de memoria y refresco del X4.
- **Cómo comprobarlo:** biblioteca vacía y poblada, libros con y sin portada, títulos largos, selección de acciones y persistencia del dashboard elegido.

### BR-009 — Orden directo y presentación contextual de la biblioteca

- **Petición del usuario:** cambiar el orden sin mantener pulsado el segundo botón ni abrir el menú de ordenación. El control debe estar visible arriba y requerir el mínimo de pasos, preferiblemente sin mover la selección del libro hasta la cabecera.
- **Interacción implementada:** segunda línea de cabecera con orden activo y ayuda. Lateral superior: anterior; inferior: siguiente; ciclo **Título → Autor → Última lectura → Añadidos**. Un cambio por pulsación, sin repetición al mantener ni cambio de foco. Los frontales navegan por libros y paginan al mantener. El menú largo conserva actualización, filtros y estados, sin ordenación.
- **Selección y persistencia:** conservar el libro seleccionado cuando siga visible, mantener el filtro activo y guardar el orden elegido al salir, volver de leer o reiniciar. No enviar el foco al selector para cada cambio de orden.
- **Fase posterior, pendiente (no incluida en .40):** fechas y agrupaciones, según el orden:
  - **Título / Autor:** presentación limpia de título y autor, sin añadir fechas innecesarias.
  - **Última lectura:** mostrar debajo del libro cuándo se leyó por última vez, con una etiqueta breve y legible. Diferenciar los libros nunca abiertos de aquellos cuya fecha no se conoce.
  - **Añadidos:** explorar agrupaciones sencillas por incorporación, como hoy, esta semana, este mes y anteriores, o por lote de incorporación si no existe una fecha fiable. Elegir una presentación antes de implementarla; los grupos temporales son una propuesta, no un diseño cerrado.
- **Fechas y datos existentes:** el catálogo actual ordena las incorporaciones mediante una secuencia de lotes, no mediante una fecha de alta. Para ofrecer grupos por mes habrá que registrar fechas fiables y definir la migración. No presentar la fecha del archivo como fecha de incorporación ni inventar fechas para libros antiguos; comprobar también la fuente de la última lectura y los casos sin reloj sincronizado.
- **Filtros y estados:** el usuario comprobó que Reading/Pending ya se indica; no solicita rehacer ese comportamiento. Conservarlo y mejorar su acabado visual: jerarquía de título y subtítulo, espaciado, alineación y selección más cuidados, con buena legibilidad en tinta electrónica.
- **Cómo comprobarlo:** recorrer todos los órdenes con pulsaciones cortas, sin menús ni pulsaciones largas; probar filtros activos, regreso de lectura, reinicio, biblioteca vacía, títulos largos y paginación. Verificar que el dato contextual corresponde al orden, que los grupos no separan títulos de sus datos y que las fechas desconocidas se muestran honestamente.

## En curso

Revisión y publicación de preview 2 autorizadas por el usuario. La instalación y validación física serán posteriores.

## Hechas

### Preview 2 — 1.6.0.40 — 20 de octubre de 2026

- **BR-002, BR-009 y BR-005:** completado el alcance inicial descrito arriba; publicación autorizada tras revisión.
- **Referencia:** cambios locales en OTA/manifiesto y sincronizador, LibraryActivity, ShortcutRegistry, AppsActivity, ActivityManager, ajustes y traducciones EN/ES. Detalles operativos en `agent-docs/bruma-update-channel.md`.
- **Verificación:** pruebas nativas, simulador EN/ES, biblioteca de 506 libros y memoria reducida; 296 pruebas nativas aprobadas, compilaciones `default` y `gh_release` en dry-run y presupuesto del candidato registrados en la entrega local. Las 17 Apps regresan conservando selección y segunda página.
- **Datos:** no se cambia el formato del catálogo, no se reinician ajustes ni preferencias y no se modifican bootloader, particiones ni eFuses.
- **Pendiente:** prueba física de la preview .40. Fechas y grupos de BR-009 permanecen pendientes, igual que las restantes ideas del documento.

## Plantilla para nuevas entradas

Siguiente ID disponible: **BR-010**. No reutilizar IDs de mejoras terminadas o aparcadas.

```markdown
### BR-010 — Título concreto

- **Estado:** Idea
- **Prioridad:** Por decidir
- **Problema o necesidad:**
- **Resultado deseado:**
- **Alcance acordado:**
- **Cómo comprobar que está terminada:**
- **Decisiones y dependencias:**
- **Referencia al cambio y verificación:**
- **Publicación:**
```
