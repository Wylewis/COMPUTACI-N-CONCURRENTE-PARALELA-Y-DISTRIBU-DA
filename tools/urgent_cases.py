#!/usr/bin/env python3
"""Busca en un events.log los dos casos que pide la variante de pedidos urgentes:
un urgente creado despues de un normal del mismo restaurante y asignado mientras ese
normal seguia esperando, y un normal envejecido (espero al menos agingMs) asignado
mientras un urgente mas nuevo del mismo restaurante seguia esperando.

Uso: python3 tools/urgent_cases.py events.log [--aging-ms 300000]
Sale con 0 si encuentra los dos casos y con 1 si falta alguno."""

import argparse
import json
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log")
    ap.add_argument("--aging-ms", type=int, default=300000)
    args = ap.parse_args()

    events = [json.loads(l) for l in open(args.log, encoding="utf-8") if l.strip()]
    created = {e["order"]: e for e in events if e["event"] == "orderCreated"}
    assigned = {e["order"]: e["t"] for e in events if e["event"] == "orderAssigned"}
    rejected = {e["order"]: e["t"] for e in events if e["event"] == "orderRejected"}

    def restaurant(o):
        return created[o]["restaurant"]

    def urgent(o):
        return bool(created[o].get("urgent", False))

    def fate(o):
        return assigned.get(o, rejected.get(o, float("inf")))

    def show(o):
        c = created[o]
        kind = "urgente" if urgent(o) else "normal"
        if o in assigned:
            end = f"asignado t={assigned[o]}, espero {(assigned[o] - c['t']) // 1000} s"
        elif o in rejected:
            end = f"rechazado t={rejected[o]}"
        else:
            end = "pendiente al cerrar"
        return f"  {o} ({c['restaurant']}, {kind}) creado t={c['t']}, {end}"

    case1 = None
    for u, tu in sorted(assigned.items(), key=lambda kv: kv[1]):
        if not urgent(u):
            continue
        for n in created:
            if not urgent(n) and restaurant(n) == restaurant(u) and created[n]["t"] < created[u]["t"] < tu < fate(n):
                case1 = (u, n)
                break
        if case1:
            break

    case2 = None
    for n, tn in sorted(assigned.items(), key=lambda kv: kv[1]):
        if urgent(n) or tn - created[n]["t"] < args.aging_ms:
            continue
        for u in created:
            if urgent(u) and restaurant(u) == restaurant(n) and created[n]["t"] < created[u]["t"] < tn < fate(u):
                case2 = (n, u)
                break
        if case2:
            break

    print("caso 1: urgente creado despues de un normal y asignado mientras el normal esperaba")
    print("\n".join(show(o) for o in case1) if case1 else "  no encontrado")
    print("caso 2: normal envejecido asignado mientras un urgente mas nuevo esperaba")
    print("\n".join(show(o) for o in case2) if case2 else "  no encontrado")
    sys.exit(0 if case1 and case2 else 1)


if __name__ == "__main__":
    main()
