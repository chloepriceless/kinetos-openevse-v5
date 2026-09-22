#!/usr/bin/env python3
"""Apply the Kinetos changes to an OpenEVSE gui-v2 checkout (argv[1])."""
import json, os, shutil, sys

gui = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))

def patch(rel, old, new):
    p = os.path.join(gui, rel)
    s = open(p).read()
    assert old in s, (rel, old[:60])
    open(p, "w").write(s.replace(old, new, 1))

shutil.copy(os.path.join(here, "Kinetos.svelte"), os.path.join(gui, "src/components/blocks/configuration/Kinetos.svelte"))
shutil.copy(os.path.join(here, "KinetosRoute.svelte"), os.path.join(gui, "src/routes/Kinetos.svelte"))

# Route + menu entry; OhmConnect (US-only demand response) removed
patch("src/lib/routes.js", "import OhmConnect       from '../routes/OhmConnect.svelte'\n", "import Kinetos          from '../routes/Kinetos.svelte'\n")
patch("src/lib/routes.js", "    '/configuration/ohmconnect': OhmConnect,\n", "    '/configuration/kinetos': Kinetos,\n")
patch("src/routes/Configuration.svelte",
      '				<ConfigMenuButton url="/configuration/network"',
      '				<ConfigMenuButton url="/configuration/kinetos" icon="mdi:ev-station" name={$_("config.titles.kinetos")} />\n				<ConfigMenuButton url="/configuration/network"')
patch("src/routes/Configuration.svelte",
      '				<ConfigMenuButton url="/configuration/ohmconnect" icon="mdi:energy-circle" name={$_("config.titles.ohm")} />\n', "")

# Languages: only English (FR/ES/HU removed)
for lang in ("fr", "es", "hu"):
    patch("src/lib/i18n.js", 'register("%s", () => import("./i18n/%s.json"))\n' % (lang, lang), "")

# Tesla owner API data source removed (use TeslaMate via MQTT)
patch("src/components/blocks/configuration/Vehicle.svelte",
      '{name:$_("config.vehicle.mode.tesla"), value: 1}, ', "")
patch("src/components/blocks/configuration/Vehicle.svelte",
      "					{:else if $config_store.vehicle_data_src == 1}\n					<VehicleTesla />\n", "")
patch("src/components/blocks/configuration/Vehicle.svelte",
      '	import VehicleTesla 	from "./VehicleTesla.svelte";\n', "")

# Strings
p = os.path.join(gui, "src/lib/i18n/en.json")
en = json.load(open(p))
en["config"]["titles"]["kinetos"] = "Kinetos"
en["config"]["kinetos"] = json.load(open(os.path.join(here, "i18n-en-kinetos.json")))
json.dump(en, open(p, "w"), indent=4, ensure_ascii=False)
print("gui patched")
