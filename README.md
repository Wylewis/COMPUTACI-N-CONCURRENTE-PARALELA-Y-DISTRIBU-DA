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

### Estado del repartidor

El módulo `courier` guarda el estado de una moto: inactiva, yendo al restaurante, en fila,
recogiendo, llevando pedidos o averiada; el nodo donde está; el viaje en curso; y la mochila. Es
un monitor con su propio mutex, porque lo tocan varios hilos (el de la moto, los despachadores y la
ventana) y cada moto es independiente de las demás: un cerrojo por moto deja que dos despachadores
asignen a motos distintas a la vez. La mochila ocupa lugar desde la asignación (reserva), así dos
despachadores no pueden asignarle entre los dos más de `bagCapacity` pedidos; comprobar si hay
lugar y guardar el pedido es un único paso bajo el cerrojo. Un pedido entra y sale de la mochila
por `std::move`. Lo que necesitan otros hilos sale como copia: `estimateSnapshot` da el nodo donde
quedará libre y cuándo (el destino y la llegada del viaje en curso, o su nodo y ahora), y `view`
da lo que la ventana dibuja. El viaje es un `RouteMotion` inmutable compartido con `shared_ptr`,
así la ventana calcula la posición en cualquier instante sin volver a tomar el cerrojo. Entre los
pedidos ya recogidos, `nearestDelivery` elige el destino más cercano por calles desde el nodo actual.

### Selección del repartidor

El módulo `dispatch_select` decide a qué moto se le ofrece un pedido, trabajando solo sobre copias
(`CourierSnapshot`) y sin cerrojos. Alrededor del restaurante traza anillos de radio creciente
(300 m, 600 m, 1200 m, …, hasta cubrir a toda la flota), midiendo en línea recta hasta el nodo
donde cada moto quedará libre. En cada anillo aplica un map (una estimación de entrega por moto con
lugar en la mochila; las que no tienen ruta se descartan) y un reduce (la de menor hora de entrega,
y en empate el id menor, para que el resultado sea determinista). El primer anillo con alguna
estimación válida decide: una moto más rápida pero más lejana no se considera, que es la regla de
los apuntes. Cada moto se estima una sola vez porque los anillos son disjuntos. El resultado trae
el radio, los anillos recorridos y cuántas estimaciones costó, para los experimentos.

### Restaurante

El módulo `restaurant` es el restaurante como monitor: `pickupSlots` puestos de recogida y una fila
por orden de llegada. Un repartidor entra en la fila (`enterQueue`, que devuelve su posición),
espera a ser el primero y a que haya un puesto libre (`waitForSlot`, dormido en la variable de
condición del restaurante), recoge y deja el puesto (`leaveSlot`), lo que despierta al siguiente.
La parada saca de la fila a quien esperaba. La espera de preparación del pedido es asunto del
repartidor, fuera de la fila, para no ocupar un puesto antes de tiempo. `expectedStartMs` da la
espera prevista de cada uno en la fila con un scan exclusivo de los tiempos de servicio repartido
entre los puestos, y `snapshot` copia la fila para la ventana. El `scan` de `scan.h` pasa a ser
genérico para servir tanto a posiciones como a tiempos.

### Sorteo de averías

El módulo `breakdown` decide si una moto se avería en un viaje. Cada moto tiene su propio generador,
derivado de la semilla global y de su índice con `std::seed_seq`, así el sorteo no comparte estado
entre hilos y la misma semilla reproduce las mismas averías para el mismo ritmo de viajes. Se
sortea una vez por viaje con `incidents.breakdownProbability` y, si toca, un tramo del viaje al
azar donde ocurre, para que la avería caiga "en un punto aleatorio del trayecto". Una moto averiada
vuelve al servicio tras `kRepairMs` (5 min simulados), un valor fijo porque la configuración del
enunciado no lo contempla. La avería en el hilo de la moto llega en el PR siguiente.

### Registro de restaurantes

`restaurants` guarda un `Restaurant` por entrada de la configuración, por id, y despierta a todos al
parar. El hilo de la moto y la flota lo reciben por referencia.

### Flota

El módulo `fleet` agrupa las motos y sus hilos: crea `couriers` repartidores `c0…cN-1` en el nodo de
partida con la mochila de la configuración, los arranca y los une. Ofrece copias de toda la flota
(`snapshots` para estimar, `views` para dibujar) y la asignación por id. Lleva además una versión
que cada entrega incrementa: un despachador que no encontró moto duerme en `waitForChange` hasta
que la flota cambie o se pida parar, sin sondear. El aviso llega del hilo de la moto por un gancho
`onFreed` que la flota instala antes de arrancar.

### Hilo despachador

El módulo `dispatcher_thread` es el despachador de un restaurante. Toma pedidos de su cola del libro
(y duerme ahí si no hay), copia la flota, elige con `selectCourier` y asigna. Si nadie puede
tomarlo, anota la versión de la flota que vio y duerme hasta que cambie o hasta que venza
`acceptTimeoutMs` contado desde la creación del pedido; si vence sin moto, lo rechaza con `noCourier`
(`orderRejected`). Ninguna espera activa.
Si la reserva falla porque otro despachador ganó esa moto entre la copia y la reserva, reintenta con
copias frescas. Al pedir parar devuelve al libro el pedido que tenía en la mano, así cuenta como
`pending`. Lleva contadores de asignaciones, reintentos y esperas para los experimentos.

### Hilo del repartidor

El módulo `courier_thread` es el hilo de cada moto. Sin pedidos duerme en su propia variable de
condición, sin consumir CPU; `assign` reserva el pedido, publica `orderAssigned` y lo despierta,
todo bajo el mutex del hilo para que ningún evento posterior del pedido se adelante en el log. Con
pedidos, va al restaurante del más antiguo, espera fuera de la fila a que estén listos los que lleva
de ese restaurante, hace fila por un puesto de recogida (`Queued`), recoge uno a uno con 60 s
simulados de mostrador cada uno (`PickingUp`) y suelta el puesto; después entrega los recogidos
empezando por el destino más cercano por calles, con 60 s en la puerta. El puesto se devuelve
también si la parada interrumpe la recogida (RAII). Si el sorteo de averías toca en un
tramo, al terminar ese tramo la moto queda `BrokenDown` en el nodo, publica `courierBreakdown`,
devuelve los pedidos aún no recogidos (sus reservas se liberan y vuelven al libro para que otro
los tome, lo que se registra como `orderReassigned` al asignarse de nuevo), espera `kRepairMs` y
vuelve al servicio avisando a la flota. Una moto averiada no se considera en la selección: la copia
para estimar la marca como tal. Si además lleva pedidos ya recogidos, pide
rescate: la flota elige la moto más cercana en línea recta con lugar en la mochila y le encarga ir
hasta la averiada, que se trata como un punto de recogida; al llegar, el traspaso entre las dos
mochilas es atómico (`std::scoped_lock` sobre los dos mutex) y publica `orderReassigned` por pedido.
Si ninguna moto puede, el rescate queda pendiente y se reintenta en cada cambio de la flota; si la
averiada se repara antes, entrega ella misma. La rescatista que no llega (se avería ella) devuelve el
encargo a la flota. Cada tramo y cada espera es un `StopSignal::waitUntil`
sobre el instante simulado: no hay sondeo y la parada lo interrumpe en el acto, dejándolo en el
último nodo alcanzado. La fila y los puestos de recogida llegan con el monitor del restaurante.

### Ejecución y apagado

Al arrancar, `main` carga y valida el config, construye el grafo y la tabla de distancias, crea el
pool de estimación, el reloj simulado, el log con su hilo escritor, el libro de pedidos y el vigía de
señales, lanza el hilo generador e imprime `Simulation has started...`. Después duerme hasta que se
cumpla `simulation.durationS` o llegue SIGINT o SIGTERM, lo que ocurra primero. El apagado es
ordenado: se pide la parada, el generador termina y se une, se registra `simulationStopping`, el
libro se cierra y lo que quedaba en espera es `pending`, el log se cierra y el resumen final se
imprime como última línea de stdout. `tests/run_sim_tests.sh` corre el programa por tiempo
cumplido, por SIGINT y por SIGTERM y verifica cada corrida con `check_log.py`.

El hilo generador (`generator_thread`) itera sobre las ráfagas: duerme hasta el instante simulado de
la próxima con la espera interrumpible de la señal de parada, entrega la ráfaga al libro y publica
`orderCreated` por cada pedido y `orderRejected` por los rechazados.

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
