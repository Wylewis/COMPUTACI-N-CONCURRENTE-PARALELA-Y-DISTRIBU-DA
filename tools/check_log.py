#!/usr/bin/env python3
"""Verifica el log de eventos y el resumen final de delivery_sim.

Uso:
    python3 tools/check_log.py events.log [--summary resumen.json] [--config config/equipetrol.json]

Comprueba las reglas de las secciones 4, 6 y 7 del enunciado, igual que lo
hara el programa de los evaluadores:

  - cada linea es un objeto JSON completo con `t` entero y un `event` conocido,
    y exactamente las propiedades que corresponden a ese evento;
  - cada pedido empieza con orderCreated, sigue el orden
    created -> assigned -> pickedUp -> delivered sin saltos ni retrocesos,
    y termina en exactamente un estado final (delivered o rejected) o queda
    pending; nada ocurre despues de un final;
  - para un mismo pedido, `t` nunca disminuye;
  - un pedido tiene un solo repartidor a la vez: los eventos de recogida y
    entrega llevan al repartidor actual, y orderReassigned pasa de `from`
    (el actual) a `to` (otro distinto);
  - con --config: ningun repartidor lleva mas de bagCapacity pedidos a la vez
    (asignados o recogidos y no entregados), y en ningun restaurante hay mas
    repartidores recogiendo (entre pickupStarted y orderPickedUp) que
    pickupSlots;
  - despues de simulationStopping no se crean pedidos;
  - con --summary: created, delivered, rejected, pending, reassigned,
    breakdowns, deliveredPerCourier, avgDeliveryMs y maxDeliveryMs coinciden
    con las lineas del log.

El orden de llegada a los restaurantes no se puede verificar solo con el log,
porque no existe un evento de llegada a la fila; queda fuera.

Sale con 0 si no hay errores y con 1 si hay alguno. Solo usa la libreria
estandar de Python.
"""

import argparse
import json
import sys
from collections import defaultdict

EVENTS = {
    "orderCreated": {"order", "restaurant"},
    "orderAssigned": {"order", "courier"},
    "pickupStarted": {"order", "courier", "restaurant"},
    "orderPickedUp": {"order", "courier", "restaurant"},
    "orderDelivered": {"order", "courier"},
    "orderRejected": {"order", "reason"},
    "courierBreakdown": {"courier"},
    "orderReassigned": {"order", "from", "to"},
    "simulationStopping": set(),
}
REASONS = {"queueFull", "unreachable", "noCourier"}
FINAL = {"delivered", "rejected"}


def check(lines, summary=None, config=None):
    """Devuelve la lista de errores (vacia si el log es correcto) y las estadisticas."""
    errors = []

    def err(lineno, msg):
        errors.append(f"linea {lineno}: {msg}")

    bag_capacity = config["fleet"]["bagCapacity"] if config else None
    slots = {r["id"]: r["pickupSlots"] for r in config["restaurants"]} if config else {}
    restaurant_ids = set(slots) if config else None

    state = {}        # pedido -> estado
    courier_of = {}   # pedido -> repartidor actual
    last_t = {}       # pedido -> ultimo t
    created_at = {}
    restaurant_of = {}
    bag = defaultdict(set)         # repartidor -> pedidos que lleva
    collecting = defaultdict(set)  # restaurante -> repartidores recogiendo
    counts = defaultdict(int)
    per_courier = defaultdict(int)
    delivery_times = []
    stopping_seen = False
    prev_global_t = None
    global_backwards = 0

    for lineno, raw in enumerate(lines, 1):
        raw = raw.strip()
        if not raw:
            err(lineno, "linea vacia")
            continue
        try:
            e = json.loads(raw)
        except json.JSONDecodeError as ex:
            err(lineno, f"JSON invalido: {ex.msg}")
            continue
        if not isinstance(e, dict):
            err(lineno, "la linea no es un objeto JSON")
            continue

        t = e.get("t")
        kind = e.get("event")
        if not isinstance(t, int) or isinstance(t, bool) or t < 0:
            err(lineno, f"'t' debe ser un entero no negativo, es {t!r}")
            continue
        if kind not in EVENTS:
            err(lineno, f"evento desconocido {kind!r}")
            continue
        expected = EVENTS[kind] | {"t", "event"}
        keys = set(e)
        if keys != expected:
            missing = expected - keys
            extra = keys - expected
            if missing:
                err(lineno, f"{kind}: faltan propiedades {sorted(missing)}")
            if extra:
                err(lineno, f"{kind}: propiedades de mas {sorted(extra)}")
            continue

        counts[kind] += 1
        if prev_global_t is not None and t < prev_global_t:
            global_backwards += 1
        prev_global_t = t

        if kind == "simulationStopping":
            stopping_seen = True
            continue
        if kind == "courierBreakdown":
            continue

        order = e["order"]
        if kind == "orderCreated":
            if order in state:
                err(lineno, f"pedido {order} creado dos veces")
                continue
            if stopping_seen:
                err(lineno, f"pedido {order} creado despues de simulationStopping")
            if restaurant_ids is not None and e["restaurant"] not in restaurant_ids:
                err(lineno, f"pedido {order}: restaurante desconocido {e['restaurant']!r}")
            state[order] = "created"
            created_at[order] = t
            restaurant_of[order] = e["restaurant"]
            last_t[order] = t
            continue

        if order not in state:
            err(lineno, f"{kind} de un pedido no creado: {order}")
            continue
        if t < last_t[order]:
            err(lineno, f"pedido {order}: t retrocede de {last_t[order]} a {t}")
        last_t[order] = max(last_t[order], t)
        current = state[order]
        if current in FINAL:
            err(lineno, f"pedido {order}: {kind} despues del estado final {current}")
            continue

        if kind == "orderAssigned":
            if current != "created":
                err(lineno, f"pedido {order}: orderAssigned desde {current}, se esperaba created")
                continue
            c = e["courier"]
            state[order] = "assigned"
            courier_of[order] = c
            bag[c].add(order)
            if bag_capacity is not None and len(bag[c]) > bag_capacity:
                err(lineno, f"repartidor {c} lleva {len(bag[c])} pedidos, capacidad {bag_capacity}")

        elif kind == "orderRejected":
            if current != "created":
                err(lineno, f"pedido {order}: orderRejected desde {current}, solo se rechaza antes de asignar")
                continue
            if e["reason"] not in REASONS:
                err(lineno, f"pedido {order}: motivo de rechazo desconocido {e['reason']!r}")
            state[order] = "rejected"

        elif kind == "pickupStarted":
            if current != "assigned":
                err(lineno, f"pedido {order}: pickupStarted desde {current}, se esperaba assigned")
                continue
            c = e["courier"]
            if c != courier_of.get(order):
                err(lineno, f"pedido {order}: pickupStarted por {c}, pero lo lleva {courier_of.get(order)}")
            if e["restaurant"] != restaurant_of[order]:
                err(lineno, f"pedido {order}: pickupStarted en {e['restaurant']}, pero es de {restaurant_of[order]}")
            r = e["restaurant"]
            collecting[r].add(c)
            if r in slots and len(collecting[r]) > slots[r]:
                err(lineno, f"restaurante {r}: {len(collecting[r])} repartidores recogiendo, puestos {slots[r]}")
            state[order] = "collecting"

        elif kind == "orderPickedUp":
            if current not in ("assigned", "collecting"):
                err(lineno, f"pedido {order}: orderPickedUp desde {current}, se esperaba assigned")
                continue
            c = e["courier"]
            if c != courier_of.get(order):
                err(lineno, f"pedido {order}: orderPickedUp por {c}, pero lo lleva {courier_of.get(order)}")
            if e["restaurant"] != restaurant_of[order]:
                err(lineno, f"pedido {order}: orderPickedUp en {e['restaurant']}, pero es de {restaurant_of[order]}")
            collecting[e["restaurant"]].discard(c)
            state[order] = "pickedUp"

        elif kind == "orderDelivered":
            if current != "pickedUp":
                err(lineno, f"pedido {order}: orderDelivered desde {current}, se esperaba pickedUp")
                continue
            c = e["courier"]
            if c != courier_of.get(order):
                err(lineno, f"pedido {order}: orderDelivered por {c}, pero lo lleva {courier_of.get(order)}")
            bag[c].discard(order)
            state[order] = "delivered"
            per_courier[c] += 1
            delivery_times.append(t - created_at[order])

        elif kind == "orderReassigned":
            if current not in ("assigned", "collecting", "pickedUp"):
                err(lineno, f"pedido {order}: orderReassigned desde {current}")
                continue
            src, dst = e["from"], e["to"]
            if src != courier_of.get(order):
                err(lineno, f"pedido {order}: orderReassigned desde {src}, pero lo lleva {courier_of.get(order)}")
            if src == dst:
                err(lineno, f"pedido {order}: orderReassigned de {src} a si mismo")
            bag[src].discard(order)
            bag[dst].add(order)
            courier_of[order] = dst
            if bag_capacity is not None and len(bag[dst]) > bag_capacity:
                err(lineno, f"repartidor {dst} lleva {len(bag[dst])} pedidos tras el traspaso, capacidad {bag_capacity}")
            if current == "collecting":
                collecting[restaurant_of[order]].discard(src)
                state[order] = "assigned"

    pending = [o for o, s in state.items() if s not in FINAL]
    stats = {
        "lines": len(lines),
        "created": counts["orderCreated"],
        "delivered": counts["orderDelivered"],
        "rejected": counts["orderRejected"],
        "pending": len(pending),
        "reassigned": counts["orderReassigned"],
        "breakdowns": counts["courierBreakdown"],
        "stopping": counts["simulationStopping"],
        "globalBackwards": global_backwards,
        "avgDeliveryMs": round(sum(delivery_times) / len(delivery_times)) if delivery_times else 0,
        "maxDeliveryMs": max(delivery_times) if delivery_times else 0,
        "deliveredPerCourier": dict(per_courier),
    }
    if counts["simulationStopping"] != 1:
        errors.append(f"resumen: simulationStopping aparece {counts['simulationStopping']} veces, se esperaba 1")
    if stats["created"] != stats["delivered"] + stats["rejected"] + stats["pending"]:
        errors.append("resumen: created != delivered + rejected + pending en el log")

    if summary is not None:
        for key in ("created", "delivered", "rejected", "pending", "reassigned", "breakdowns"):
            if summary.get(key) != stats[key]:
                errors.append(f"resumen: {key} = {summary.get(key)!r}, el log tiene {stats[key]}")
        for key in ("avgDeliveryMs", "maxDeliveryMs"):
            value = summary.get(key)
            if not isinstance(value, (int, float)) or abs(value - stats[key]) > 1:
                errors.append(f"resumen: {key} = {value!r}, el log da {stats[key]}")
        spc = summary.get("deliveredPerCourier")
        if not isinstance(spc, dict):
            errors.append("resumen: falta deliveredPerCourier")
        else:
            if sum(spc.values()) != stats["delivered"]:
                errors.append(f"resumen: deliveredPerCourier suma {sum(spc.values())}, delivered es {stats['delivered']}")
            for c, n in per_courier.items():
                if spc.get(c) != n:
                    errors.append(f"resumen: deliveredPerCourier[{c}] = {spc.get(c)!r}, el log tiene {n}")
            for c, n in spc.items():
                if n != 0 and c not in per_courier:
                    errors.append(f"resumen: deliveredPerCourier[{c}] = {n}, pero {c} no entrego nada en el log")

    return errors, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log", help="archivo events.log")
    ap.add_argument("--summary", help="archivo con la linea JSON del resumen final")
    ap.add_argument("--config", help="archivo de configuracion, para capacidades y puestos")
    ap.add_argument("--quiet", action="store_true", help="no imprimir las estadisticas")
    args = ap.parse_args()

    with open(args.log, encoding="utf-8") as f:
        lines = f.read().splitlines()
    summary = None
    if args.summary:
        with open(args.summary, encoding="utf-8") as f:
            text = f.read().strip().splitlines()
            summary = json.loads(text[-1]) if text else {}
    config = None
    if args.config:
        with open(args.config, encoding="utf-8") as f:
            config = json.load(f)

    errors, stats = check(lines, summary, config)
    if not args.quiet:
        for k, v in stats.items():
            print(f"  {k}: {v}")
    if errors:
        print(f"check_log: {len(errors)} error(es)")
        for e in errors[:50]:
            print("  " + e)
        if len(errors) > 50:
            print(f"  ... y {len(errors) - 50} mas")
        sys.exit(1)
    print("check_log: sin errores")


if __name__ == "__main__":
    main()
