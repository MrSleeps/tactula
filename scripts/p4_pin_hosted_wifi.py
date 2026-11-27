"""Keep the cache-safe P4 SDK matched to the Guition board's C6 firmware."""

from pathlib import Path
import shutil

import yaml

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
template = Path(env.PioPlatform().get_dir()) / "builder" / "build_lib"
sdk_project = project / ".dummy"
if not sdk_project.exists():
    shutil.copytree(template, sdk_project)

manifest = sdk_project / "idf_component.yml"
original = (manifest if manifest.exists() else template / manifest.name).read_text(encoding="utf-8")
config = yaml.safe_load(original)
dependencies = config.setdefault("dependencies", {})
# The temporary SDK component constrains Arduino's wider ranges without
# removing the components (which would also remove their installed headers).
pins = {"espressif/esp_hosted": "2.12.3", "espressif/esp_wifi_remote": "1.4.2"}
if any(dependencies.get(name) != version for name, version in pins.items()):
    dependencies.update(pins)
    manifest.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
    print("[P4] SDK manifest pins ESP-Hosted 2.12.3 and Wi-Fi Remote 1.4.2")
