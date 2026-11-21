#!/usr/bin/env python3
"""Saca del log de eventos de una corrida las medidas del sistema completo.

Uso:
    python3 tools/gpu_sim_metrics.py events.log

Imprime una linea JSON con los pedidos creados, asignados y entregados, los
asignados y entregados por hora simulada, y la demora de despacho de cada
pedido (de orderCreated a su primer orderAssigned) y el tiempo de entrega (de
orderCreated a orderDelivered), en milisegundos simulados: mediana y p90.
"""

import json
import sys


def percentile(values, fraction):
    return values[min(len(values) - 1, int(fraction * len(values)))] if values else -1


def main():
    created, assigned, delivery = {}, {}, []
    last = 0
    with open(sys.argv[1], encoding="utf-8") as log:
        for line in log:
            event = json.loads(line)
            t = event.get("t", 0)
            last = max(last, t)
            kind, order = event.get("event"), event.get("order")
            if kind == "orderCreated":
                created[order] = t
            elif kind == "orderAssigned" and order not in assigned:
                assigned[order] = t
            elif kind == "orderDelivered" and order in created:
                delivery.append(t - created[order])
    dispatch = sorted(assigned[o] - created[o] for o in assigned if o in created)
    delivery.sort()
    hours = last / 3600000.0 if last else 1.0
    print(json.dumps({
        "creados": len(created), "asignados": len(assigned), "entregados": len(delivery),
        "minutos_simulados": round(last / 60000, 1),
        "asignados_por_hora": round(len(assigned) / hours), "entregados_por_hora": round(len(delivery) / hours),
        "demora_de_despacho_ms": {"mediana": percentile(dispatch, 0.5), "p90": percentile(dispatch, 0.9)},
        "tiempo_de_entrega_ms": {"mediana": percentile(delivery, 0.5), "p90": percentile(delivery, 0.9)},
    }))


if __name__ == "__main__":
    main()
