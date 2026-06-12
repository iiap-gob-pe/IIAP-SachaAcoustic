#!/usr/bin/env python3
# Limpia un COCO del etiquetador: quita los "anillos sueltos".
#  1) BORRA los poligonos de ancho casi-completo (>=80% del ancho de la imagen):
#     son la "basura de fondo" del autoetiquetado (abarcan toda la grabacion) y
#     son los que tienen cientos de huecos/anillos.
#  2) Le QUITA los anillos (huecos) a TODAS las demas etiquetas (deja solo el
#     contorno exterior) -> no se borra ninguna deteccion buena, solo los anillos.
# Hace un backup .bak antes de tocar nada.
#
# Uso:  python scripts/limpiar_anillos.py <archivo.json>
#       (por defecto 20240430_165000.json en la carpeta actual)
import json, shutil, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else "20240430_165000.json"
if not os.path.exists(path):
    print("No existe:", path); sys.exit(1)

bak = path + ".bak"
shutil.copy(path, bak)

d = json.load(open(path, encoding="utf-8"))
anns = d["annotations"]
W = d["images"][0]["width"]

kept = []
deleted_wide = []
holes_removed = 0
polys_stripped = 0
for a in anns:
    bb = a.get("bbox", [0, 0, 0, 0])
    seg = a.get("segmentation", [])
    nh = max(0, len(seg) - 1)
    # 1) basura de fondo: ancho >= 80% del ancho de la imagen
    if bb[2] >= 0.8 * W:
        deleted_wide.append((a.get("id"), nh))
        continue
    # 2) quita anillos de las demas
    if nh > 0:
        a["segmentation"] = [seg[0]]
        holes_removed += nh
        polys_stripped += 1
    kept.append(a)

d["annotations"] = kept
json.dump(d, open(path, "w", encoding="utf-8"), ensure_ascii=False)

print("Backup guardado en:", bak)
print("Etiquetas de ancho completo BORRADAS (basura de fondo):", len(deleted_wide))
for id_, nh in deleted_wide:
    print("   id=%s (tenia %d anillos)" % (id_, nh))
print("Anillos quitados de otras etiquetas: %d (en %d poligonos)" % (holes_removed, polys_stripped))
print("Etiquetas que quedan:", len(kept))
print("LISTO. Reabri el audio en la app para ver el resultado limpio.")
