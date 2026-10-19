#!/usr/bin/env python3
"""Pruebas del verificador del log: un log correcto no da errores, y cada
regla rota produce el error esperado."""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from check_log import check  # noqa: E402

failures = 0


def expect(name, lines, needle, summary=None, config=None):
    """needle=None: no debe haber errores; si no, algun error debe contener needle."""
    global failures
    errors, _ = check(lines, summary, config)
    if needle is None:
        ok = not errors
    else:
        ok = any(needle in e for e in errors)
    if not ok:
        failures += 1
        print(f"FALLO: {name}\n   errores: {errors}")


def ev(t, event, **fields):
    d = {"t": t, "event": event}
    d.update(fields)
    return json.dumps(d)


CONFIG = {"fleet": {"bagCapacity": 2},
          "restaurants": [{"id": "r0", "pickupSlots": 1}, {"id": "r1", "pickupSlots": 2}]}

GOOD = [
    ev(1000, "orderCreated", order="o0", restaurant="r0"),
    ev(1000, "orderCreated", order="o1", restaurant="r1"),
    ev(1200, "orderCreated", order="o2", restaurant="r0"),
    ev(1200, "orderRejected", order="o2", reason="unreachable"),
    ev(1500, "orderAssigned", order="o0", courier="c0"),
    ev(1600, "orderAssigned", order="o1", courier="c1"),
    ev(4000, "pickupStarted", order="o0", courier="c0", restaurant="r0"),
    ev(4500, "orderPickedUp", order="o0", courier="c0", restaurant="r0"),
    ev(5000, "courierBreakdown", courier="c1"),
    ev(5000, "orderReassigned", order="o1", **{"from": "c1", "to": "c0"}),
    ev(9000, "orderDelivered", order="o0", courier="c0"),
    ev(9500, "orderCreated", order="o3", restaurant="r1"),
    ev(10000, "simulationStopping"),
]
GOOD_SUMMARY = {"created": 4, "delivered": 1, "rejected": 1, "pending": 2, "reassigned": 1, "breakdowns": 1,
                "avgDeliveryMs": 8000, "maxDeliveryMs": 8000, "deliveredPerCourier": {"c0": 1, "c1": 0}}

expect("log correcto sin resumen", GOOD, None, config=CONFIG)
expect("log correcto con resumen", GOOD, None, GOOD_SUMMARY, CONFIG)

# Liberacion por averia: los no recogidos salen de la mochila de la averiada
RELEASE = [
    ev(1, "orderCreated", order="a", restaurant="r0"),
    ev(1, "orderCreated", order="b", restaurant="r0"),
    ev(1, "orderCreated", order="c", restaurant="r0"),
    ev(1, "orderCreated", order="d", restaurant="r0"),
    ev(2, "orderAssigned", order="a", courier="c1"),
    ev(2, "orderAssigned", order="b", courier="c1"),
    ev(3, "courierBreakdown", courier="c1"),
]
expect("la averiada retoma su pedido liberado con orderAssigned",
       RELEASE + [ev(4, "orderAssigned", order="a", courier="c1"), ev(9, "simulationStopping")], None, config=CONFIG)
expect("otra moto toma el liberado con orderReassigned desde la averiada",
       RELEASE + [ev(4, "orderReassigned", order="a", **{"from": "c1", "to": "c0"}), ev(9, "simulationStopping")], None, config=CONFIG)
expect("un liberado no se asigna a otra moto con orderAssigned",
       RELEASE + [ev(4, "orderAssigned", order="a", courier="c0")], "debe ser orderReassigned")
expect("tras liberar, la averiada reparada puede llenar la mochila de nuevo",
       RELEASE + [ev(4, "orderAssigned", order="c", courier="c1"), ev(4, "orderAssigned", order="d", courier="c1"),
                  ev(9, "simulationStopping")], None, config=CONFIG)
expect("pero no mas que la capacidad contando lo que retoma",
       RELEASE + [ev(4, "orderAssigned", order="c", courier="c1"), ev(4, "orderAssigned", order="d", courier="c1"),
                  ev(5, "orderAssigned", order="a", courier="c1")], "lleva 3 pedidos, capacidad 2", config=CONFIG)
expect("un reasignado desde liberado vuelve a estar asignado y se recoge",
       RELEASE + [ev(4, "orderReassigned", order="a", **{"from": "c1", "to": "c0"}),
                  ev(5, "pickupStarted", order="a", courier="c0", restaurant="r0"), ev(9, "simulationStopping")],
       None, config=CONFIG)
expect("un liberado no se recoge sin volver a asignarse",
       RELEASE + [ev(4, "pickupStarted", order="a", courier="c1", restaurant="r0")], "pickupStarted desde released")

# Formato
expect("JSON invalido", GOOD[:1] + ['{"t": 5, "event": '] + GOOD[1:], "JSON invalido")
expect("t negativo", [ev(-1, "orderCreated", order="x", restaurant="r0")] + GOOD, "'t' debe ser un entero")
expect("evento desconocido", [ev(1, "orderTeleported", order="x")] + GOOD, "evento desconocido")
expect("falta propiedad", ['{"t": 1, "event": "orderCreated", "order": "x"}'] + GOOD, "faltan propiedades")
expect("propiedad de mas", [ev(1, "orderCreated", order="x", restaurant="r0", courier="c9")] + GOOD, "propiedades de mas")
expect("motivo desconocido", [ev(1, "orderCreated", order="x", restaurant="r0"),
                              ev(2, "orderRejected", order="x", reason="tired")] + GOOD, "motivo de rechazo desconocido")

# Maquina de estados
expect("creado dos veces", GOOD + [ev(9600, "orderCreated", order="o0", restaurant="r0")], "creado dos veces")
expect("evento de pedido no creado", [ev(1, "orderAssigned", order="zz", courier="c0")] + GOOD, "no creado")
expect("salto created a pickedUp", [ev(1, "orderCreated", order="x", restaurant="r0"),
                                    ev(2, "orderPickedUp", order="x", courier="c0", restaurant="r0")] + GOOD,
       "se esperaba assigned")
expect("entrega sin recoger", [ev(1, "orderCreated", order="x", restaurant="r0"),
                               ev(2, "orderAssigned", order="x", courier="c0"),
                               ev(3, "orderDelivered", order="x", courier="c0")] + GOOD, "se esperaba pickedUp")
expect("rechazo tras asignar", [ev(1, "orderCreated", order="x", restaurant="r0"),
                                ev(2, "orderAssigned", order="x", courier="c0"),
                                ev(3, "orderRejected", order="x", reason="noCourier")] + GOOD, "solo se rechaza antes")
expect("evento tras final", GOOD + [ev(9700, "orderAssigned", order="o0", courier="c1")], "despues del estado final")
expect("entregado dos veces", GOOD + [ev(9700, "orderDelivered", order="o0", courier="c0")], "despues del estado final")

# Tiempo
expect("t retrocede en un pedido", [ev(5000, "orderCreated", order="x", restaurant="r0"),
                                    ev(4000, "orderAssigned", order="x", courier="c0")] + GOOD, "t retrocede")

# Repartidores
expect("recoge otro repartidor", [ev(1, "orderCreated", order="x", restaurant="r0"),
                                  ev(2, "orderAssigned", order="x", courier="c0"),
                                  ev(3, "orderPickedUp", order="x", courier="c1", restaurant="r0")] + GOOD,
       "pero lo lleva c0")
expect("traspaso desde quien no lo lleva", [ev(1, "orderCreated", order="x", restaurant="r0"),
                                            ev(2, "orderAssigned", order="x", courier="c0"),
                                            ev(3, "orderReassigned", order="x", **{"from": "c1", "to": "c2"})] + GOOD,
       "pero lo lleva c0")
expect("traspaso a si mismo", [ev(1, "orderCreated", order="x", restaurant="r0"),
                               ev(2, "orderAssigned", order="x", courier="c0"),
                               ev(3, "orderReassigned", order="x", **{"from": "c0", "to": "c0"})] + GOOD,
       "a si mismo")

# Capacidades (requieren config)
over_bag = [ev(1, "orderCreated", order="a", restaurant="r0"), ev(1, "orderCreated", order="b", restaurant="r0"),
            ev(1, "orderCreated", order="c", restaurant="r0"),
            ev(2, "orderAssigned", order="a", courier="c0"), ev(2, "orderAssigned", order="b", courier="c0"),
            ev(2, "orderAssigned", order="c", courier="c0"), ev(3, "simulationStopping")]
expect("mochila excedida", over_bag, "capacidad 2", config=CONFIG)
expect("mochila excedida sin config no se revisa", over_bag, None)
over_slots = [ev(1, "orderCreated", order="a", restaurant="r0"), ev(1, "orderCreated", order="b", restaurant="r0"),
              ev(2, "orderAssigned", order="a", courier="c0"), ev(2, "orderAssigned", order="b", courier="c1"),
              ev(3, "pickupStarted", order="a", courier="c0", restaurant="r0"),
              ev(3, "pickupStarted", order="b", courier="c1", restaurant="r0"),
              ev(4, "simulationStopping")]
expect("puestos excedidos", over_slots, "puestos 1", config=CONFIG)
ok_slots = over_slots[:4] + [ev(3, "pickupStarted", order="a", courier="c0", restaurant="r0"),
                             ev(4, "orderPickedUp", order="a", courier="c0", restaurant="r0"),
                             ev(5, "pickupStarted", order="b", courier="c1", restaurant="r0"),
                             ev(6, "simulationStopping")]
expect("puestos respetados en turno", ok_slots, None, config=CONFIG)
expect("restaurante desconocido", [ev(1, "orderCreated", order="x", restaurant="r9")] + GOOD, "restaurante desconocido",
       config=CONFIG)

# Parada
expect("pedido creado tras stopping", GOOD + [ev(10001, "orderCreated", order="o9", restaurant="r0")], "despues de simulationStopping")
expect("sin simulationStopping", GOOD[:-1], "simulationStopping aparece 0 veces")

# Resumen
bad = dict(GOOD_SUMMARY, delivered=2)
expect("resumen con delivered mal", GOOD, "delivered = 2", bad, CONFIG)
bad = dict(GOOD_SUMMARY, avgDeliveryMs=7000)
expect("resumen con promedio mal", GOOD, "avgDeliveryMs = 7000", bad, CONFIG)
bad = dict(GOOD_SUMMARY, deliveredPerCourier={"c0": 0, "c1": 1})
expect("resumen con reparto por repartidor mal", GOOD, "deliveredPerCourier[c0]", bad, CONFIG)
bad = dict(GOOD_SUMMARY, pending=5)
expect("resumen con pending mal", GOOD, "pending = 5", bad, CONFIG)

if failures:
    print(f"check_log_test: {failures} prueba(s) fallaron")
    sys.exit(1)
print("check_log_test: todas las pruebas pasaron")
