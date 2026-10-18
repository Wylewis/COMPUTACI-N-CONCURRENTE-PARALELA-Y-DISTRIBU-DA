# Equipetrol Delivery

Simulación concurrente de despacho de pedidos en la Zona Equipetrol, Santa Cruz de la Sierra.
Proyecto final del curso LIA-221 Computación Concurrente, Paralela y Distribuida.

## Compilación

```sh
mkdir build && cd build
cmake .. && make
./delivery_sim ../config/equipetrol.json
```

El único flag opcional es `--log <ruta>`, que cambia el archivo del log de eventos (por defecto `events.log`).

### Validación del archivo de configuración

El programa rechaza el archivo, con un mensaje en stderr y estado de salida distinto de cero y sin
iniciar ninguna simulación, cuando:

- falta el argumento, el archivo no se puede leer o el JSON está mal formado;
- falta una propiedad obligatoria o tiene un tipo inválido;
- `nodes` está vacío, o una calle, un restaurante o `fleet.startNode` apunta a un nodo que no existe;
- un valor sale del rango que declara la tabla de la sección 5: `fleet.couriers`, `fleet.bagCapacity`,
  `orders.burstMax` y `pickupSlots` deben ser ≥ 1, y `breakdownProbability` estar entre 0 y 1;
- `prepTimeMs` no es un array `[min, max]`;
- la imagen `map.image` no se puede leer.

No se comprueba nada más, para no rechazar archivos válidos que sigan el formato del enunciado.
Un archivo mínimo y válido está en `tests/config/minimal.json`; los casos inválidos están en
`tests/invalid/` y `ctest` los ejecuta con `tests/run_config_tests.sh`.

### Proyección del mapa

El módulo `map_projection` convierte latitud y longitud a píxeles de la imagen y viceversa con los
límites de `map.bounds`: la esquina noroeste es el píxel (0, 0) y la sureste es (ancho, alto). Como
la imagen se recortó exactamente a esos límites, la conversión es lineal en cada eje. Es lo que la
ventana usa para dibujar restaurantes, motos y puntos de entrega en su posición real.

### Tabla de distancias

El módulo `distance_table` calcula una vez, al arrancar, la distancia más corta desde cada
restaurante hasta todos los nodos (un Dijkstra uno-a-todos por restaurante). Después cada consulta
es una lectura. La función `computeRow` es la unidad de trabajo: una fila por origen, lo que
permite construir la tabla en paralelo repartiendo los orígenes entre tareas. La tabla es inmutable
y se consulta desde cualquier hilo sin cerrojo.

La tabla se construye en paralelo (`distance_table_parallel`): los orígenes se reparten entre
tantas tareas como núcleos informe `hardware_concurrency`, cada tarea lanzada con `std::async`
calcula las filas de su grupo y las escribe en posiciones disjuntas de un vector ya dimensionado,
sin ningún cerrojo, y al final se esperan todas las tareas. Si una tarea lanza una excepción, llega
a quien construye la tabla a través de su future. El test compara la tabla paralela con la serial
celda por celda y mide los tiempos de ambas.

### Pool de hilos

El módulo `thread_pool` crea N hilos una sola vez y los reutiliza: los trabajos entran a una cola
protegida por un mutex y una variable de condición, cada hilo duerme sin consumir CPU hasta que
haya trabajo, y cada trabajo enviado con `submit` devuelve un future con su resultado o con la
excepción que lanzó (`packaged_task`). El mutex cubre solo la cola; los trabajos corren fuera del
cerrojo. `stop` termina lo pendiente, despierta a todos los hilos y hace `join`. Es la base de las
estimaciones concurrentes del despachador.

El tamaño del pool lo decide el módulo `hardware`: un hilo por hilo de hardware según
`hardware_concurrency`, nunca menos de 2 (para que haya concurrencia real aunque la máquina tenga
un núcleo o no informe) ni más de 32 (los candidatos de un pedido son pocas decenas). El programa
registra la decisión al arrancar. El mismo módulo decide cuántas tareas usan los precálculos.

### Parada y señales

El módulo `stop_signal` tiene la orden de parar compartida por todos los hilos. Leerla es una
bandera atómica; esperarla es una variable de condición, así los hilos duermen sin CPU y una sola
llamada a `request` los despierta a todos. Toda espera de tiempo simulado (un viaje, una
preparación) se hace con `waitUntil`, que termina al vencer el plazo o al pedirse la parada, lo que
ocurra primero. `SignalWatcher` convierte SIGINT y SIGTERM en esa orden: el manejador de señal solo
escribe un byte en un pipe, lo único seguro dentro de un manejador, y un hilo vigía que duerme en
`read` sobre ese pipe pide la parada al recibirlo.

### Movimiento de las motos

El módulo `route_motion` describe el viaje de una moto por una ruta a `fleet.speedKmh`, a partir
de un instante simulado de salida. Con eso, la posición en cualquier instante se calcula
(`positionAt`) sin que el repartidor tenga que actualizarla cuadro a cuadro: la ventana consulta
el viaje y dibuja el punto avanzando suave por la calle. También da la duración de cada tramo, que
es lo que el repartidor espera entre nodo y nodo.

### Estimación de entrega

El módulo `estimate` calcula, como función pura, cuándo entregaría un repartidor un pedido si lo
tomara ahora: trayecto hasta el restaurante desde el nodo donde quedará libre, espera hasta que el
pedido esté preparado, recogida (60 s simulados), trayecto hasta el cliente y entrega en la puerta
(60 s simulados). Recibe una copia de los datos del repartidor, así la tarea que estima no comparte
nada con el hilo de la moto. Si la mochila está llena o no hay ruta, lanza `EstimateError`: ese es
el error que el despachador recibe de una estimación fallida y sobrevive.

### Generación de pedidos

El módulo `order_generator` produce la secuencia de pedidos a partir de `orders.seed`, con un
único generador aleatorio y un orden fijo de sorteos, así la misma semilla da exactamente la misma
secuencia. Las llegadas siguen intervalos exponenciales con media `meanIntervalMs`; una de cada
cinco llegadas es una ráfaga de 2 a `burstMax` pedidos en el mismo instante. Cada pedido recibe
un restaurante y un nodo de entrega al azar, y su hora de preparación lista dentro de `prepTimeMs`.
Si no existe camino dirigido del restaurante a la entrega, el pedido nace rechazado con
`unreachable`. Con el config de Equipetrol, cerca del 3 % de los pedidos son `unreachable`.

### Libro de pedidos

El módulo `order_book` es el único lugar donde esperan los pedidos aún no asignados. Es un monitor
con un solo mutex, porque el límite `orders.maxPending` es global y comprobar si hay lugar e insertar
deben ser un único paso; dentro hay una cola por restaurante con su propia variable de condición,
así el despachador de cada restaurante duerme hasta que llega un pedido suyo. Al recibir una ráfaga,
los pedidos que ya venían rechazados por `unreachable` pasan sin ocupar lugar, los demás entran
mientras haya lugar y el resto se rechaza con `queueFull`. La posición de cada pedido en la cola de
su restaurante se calcula con un scan exclusivo (`scan.h`) sobre la ráfaga: por restaurante, un
vector de ceros y unos y su suma prefija. Cada cola se atiende por antigüedad; un pedido devuelto
tras una avería vuelve al frente.

### Log de eventos y resumen

El log (`events.log` o la ruta de `--log`) lleva un objeto JSON por línea, con `t` en milisegundos
simulados y solo los campos que corresponden a cada evento, por ejemplo:

```json
{"t":15230,"event":"orderCreated","order":"o17","restaurant":"r3"}
{"t":15410,"event":"orderAssigned","order":"o17","courier":"c5"}
{"t":322900,"event":"orderRejected","order":"o21","reason":"unreachable"}
```

Los contadores del resumen final se actualizan al escribir cada línea, así que el resumen coincide
con el log por construcción: `created = delivered + rejected + pending`, y `avgDeliveryMs` y
`maxDeliveryMs` se miden desde `orderCreated` hasta `orderDelivered`.

El módulo `event_logger` pone ese log detrás de una cola productor-consumidor: todos los hilos
publican eventos con `publish`, y un único hilo escritor los saca y los escribe con `EventLog`. El
mutex cubre solo la cola; el escritor toma todo lo pendiente de una vez y escribe a disco fuera del
cerrojo, así quien publica nunca espera al disco. `close` escribe lo pendiente, une el hilo y deja
el resumen definitivo.

### Verificador del log

`tools/check_log.py` comprueba un log y su resumen contra las reglas de las secciones 4, 6 y 7,
como lo hará el programa de los evaluadores:

```sh
python3 tools/check_log.py events.log --summary resumen.json --config config/equipetrol.json
```

Revisa que cada línea sea un objeto JSON con exactamente las propiedades de su evento; que cada
pedido siga `created → assigned → pickedUp → delivered` sin saltos ni retrocesos y termine en un
solo estado final; que `t` nunca disminuya para un pedido; que cada pedido tenga un único
repartidor a la vez y los traspasos vayan del repartidor actual a otro; con `--config`, que
ninguna mochila supere `bagCapacity` ni ningún restaurante tenga más repartidores recogiendo que
`pickupSlots`; que no se creen pedidos después de `simulationStopping`; y con `--summary`, que
cada contador del resumen sea igual a las líneas del log. Sale con 0 si no hay errores.

Con ThreadSanitizer (carreras de datos) o con AddressSanitizer (errores y fugas de memoria). Las
dos opciones instrumentan `delivery_sim` y todas las pruebas, y no pueden combinarse:

```sh
cmake -DENABLE_TSAN=ON .. && make
cmake -DENABLE_ASAN=ON .. && make
```

LeakSanitizer viene incluido con AddressSanitizer en Linux; en macOS con Apple Silicon no está
disponible, así que la detección de fugas se hace en Linux.

Pruebas unitarias (se construyen con `make` dentro de `build/`):

```sh
ctest --output-on-failure
```

## Dependencias

- cmake ≥ 3.11
- make
- g++ ≥ 7 o clang equivalente, con soporte C++17
- nlohmann/json 3.12.0 (incluida en `third_party/nlohmann/`, licencia MIT; no requiere instalación)

## Mapa

La zona de trabajo de la simulación es la franja de Equipetrol a lo largo de la Av. San
Martín, desde el Segundo Anillo (Av. Cristóbal de Mendoza) hasta el Cuarto Anillo (Av.
Antonio Vaca Díez), cruzada por el Tercer Anillo (Av. Noel Kempff Mercado) y limitada al
este por la Av. La Salle. Está definida por un polígono de 58 vértices en `data/equipetrol_zone.json`.

La imagen de fondo `data/equipetrol.png` es el recorte de OpenStreetMap a ese polígono:
todo lo que queda fuera de la zona está pintado de blanco. Tamaño: 712 × 991 píxeles,
zoom 16.

Bounding box geográfico de la imagen, igual al del polígono (grados decimales, también en
`data/equipetrol_bounds.json`):

| Lado  | Valor       |
|-------|-------------|
| north | -17.752704  |
| south | -17.772971  |
| west  | -63.203708  |
| east  | -63.188420  |

La imagen se genera con `tools/make_map.py`, que solo necesita Python 3 (sin librerías
externas): descarga los tiles de OpenStreetMap que cubren el bounding box del polígono, los
une, recorta el resultado exactamente a esos límites y pinta de blanco el exterior del
polígono. Cualquier latitud y longitud dentro de la zona se convierte a píxel con una
interpolación lineal sobre los cuatro valores de arriba.

```sh
python3 tools/make_map.py --zone data/equipetrol_zone.json --zoom 16 --out data/equipetrol.png
```

## Red de calles

`tools/extract_network.py` construye la red de calles de la zona a partir de OpenStreetMap,
también con solo Python 3:

```sh
python3 tools/extract_network.py --zone data/equipetrol_zone.json --out data/equipetrol_network.json
```

Descarga de la API de OpenStreetMap todos los elementos del rectángulo envolvente de la zona
(la respuesta se guarda en `tools/osm_cache/` para repetir la extracción sin red), conserva las
vías transitables en moto cuyos nodos caen dentro del polígono, y produce en
`data/equipetrol_network.json`:

- `nodes`: las intersecciones y, en calles curvas, los quiebres donde el trazado se aparta más
  de 12 m de la recta (`--tolerance`), para que una moto dibujada entre dos nodos nunca cruce
  una manzana;
- `streets`: un segmento por cada par de nodos consecutivos, con `oneWay` según la etiqueta
  `oneway` de OpenStreetMap; las rotondas son siempre de un solo sentido;
- `restaurantCandidates`: los restaurantes reales de la zona con su nodo más cercano, de donde
  se eligen los del archivo de configuración.

Solo se conserva la componente conexa más grande. Con `--prune-unreachable` se conservan además
solo los nodos con ida y vuelta respetando sentidos, descartando los callejones de un solo sentido
cortados por el borde de la zona.

## Modelo de la ciudad

`config/equipetrol.json` es el archivo de configuración principal. Lo genera `tools/make_config.py`
a partir de la red extraída y de los límites del mapa:

```sh
python3 tools/make_config.py
```

Contiene los 320 nodos y 472 segmentos de `data/equipetrol_network.json`, y nueve restaurantes
reales de la Av. San Martín, tomados de OpenStreetMap y ubicados en el nodo más cercano de la
avenida, repartidos desde el Cuarto Anillo hasta el Segundo: Pizza Hut, Jardín de Asia,
Bali Parrilla Bar, Botánica, Puerto Milanesa, Slatkis, Tony Roma's, Pícolo y RestoBar. La flota
parte del nodo de la avenida más cercano a su punto medio. Los demás valores (flota, pedidos,
despacho, incidentes y simulación) son los del ejemplo de la sección 5 del enunciado.

### Rutas

El módulo `graph` convierte nodos y calles en un grafo dirigido: una calle de doble sentido
aporta dos aristas y una de sentido único, una. El peso de cada arista es la distancia geodésica
entre sus nodos. Las rutas se calculan con Dijkstra respetando los sentidos, y un punto de
entrega es `unreachable` cuando no existe camino dirigido desde el restaurante hasta él. Con el
config de Equipetrol, desde el punto de partida de la flota se alcanzan 310 de los 320 nodos; los
10 restantes son callejones de un solo sentido cortados por el borde de la zona.

## Atribución del mapa

Los datos y la imagen del mapa provienen de OpenStreetMap, publicados bajo la licencia ODbL.
© OpenStreetMap contributors
