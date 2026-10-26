import subprocess

import pytest

from mobarchy_mcp import keyboard

EVDEV = """<?xml version="1.0"?>
<xkbConfigRegistry version="1.1">
  <layoutList>
    <layout>
      <configItem><name>no</name><description>Norwegian</description></configItem>
      <variantList>
        <variant><configItem><name>mac</name><description>Norwegian (Macintosh)</description></configItem></variant>
        <variant><configItem><name>dvorak</name><description>Norwegian (Dvorak)</description></configItem></variant>
      </variantList>
    </layout>
    <layout>
      <configItem><name>us</name><description>English (US)</description></configItem>
    </layout>
  </layoutList>
  <optionList>
    <group allowMultipleSelection="true">
      <configItem><name>lv3</name><description>Key to choose the 3rd level</description></configItem>
      <option><configItem><name>lv3:rwin_switch</name><description>Right Win</description></configItem></option>
      <option><configItem><name>lv3:ralt_switch</name><description>Right Alt</description></configItem></option>
    </group>
  </optionList>
</xkbConfigRegistry>
"""

OWN = """// the owner's
default  partial alphanumeric_keys
xkb_symbols "basic" {
    include "latin(type2)"
    name[Group1]="Assiatisk";
};

partial alphanumeric_keys
xkb_symbols "mac" {
    include "latin(type4)"
    name[Group1]= "Assiatisk (Macintosh)";
};
"""


@pytest.fixture
def xkb(tmp_path, monkeypatch):
    system = tmp_path / "xkb"
    (system / "rules").mkdir(parents=True)
    (system / "rules" / "evdev.xml").write_text(EVDEV)
    monkeypatch.setattr(keyboard, "SYSTEM_XKB", system)
    config = tmp_path / "config"
    (config / "xkb" / "symbols").mkdir(parents=True)
    (config / "xkb" / "symbols" / "assi").write_text(OWN)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config))
    monkeypatch.setattr(keyboard, "hardware_keyboards", lambda: [])
    # (xkbcli: compiles anything here)
    monkeypatch.setattr(keyboard.subprocess, "run",
                        lambda *a, **k: subprocess.CompletedProcess(a, 0, stdout="xkb_keymap {};", stderr=""))
    return tmp_path


def test_own_layouts_are_read_with_their_variants(xkb):
    own = keyboard.own_layouts()
    assert own == [{"layout": "assi", "description": "Assiatisk", "own": True,
                    "variants": [{"variant": "mac", "description": "Assiatisk (Macintosh)"}]}]


def test_search_finds_layouts_variants_and_options(xkb):
    found = keyboard.search("mac")
    by_name = {l["layout"]: l for l in found["layouts"]}
    assert set(by_name) == {"assi", "no"}
    assert [v["variant"] for v in by_name["no"]["variants"]] == ["mac"]
    assert keyboard.search("right win")["options"][0]["option"] == "lv3:rwin_switch"
    # a whole layout matched: all its variants
    assert len({l["layout"]: l for l in keyboard.search("norw")["layouts"]}["no"]["variants"]) == 2


def test_set_writes_the_conf_and_status_reads_it(xkb):
    keyboard.set_layout("assi", "mac", ["lv3:rwin_switch"])
    text = keyboard.conf_path().read_text()
    assert "kb_layout = assi" in text and "kb_variant = mac" in text and "kb_options = lv3:rwin_switch" in text
    assert keyboard.status()["setting"] == {"layout": "assi", "variant": "mac", "options": ["lv3:rwin_switch"]}
    keyboard.reset()
    assert keyboard.status()["setting"]["layout"] == "us"
    assert "kb_layout" not in keyboard.conf_path().read_text()


def test_unknown_names_are_refused_before_anything_is_written(xkb):
    for args, message in [(("qwerty",), "no keyboard layout"),
                          (("no", "colemak"), "no variant 'colemak'"),
                          (("no", "", ["caps:nope"]), "unknown option")]:
        with pytest.raises(ValueError, match=message):
            keyboard.set_layout(*args)
    assert not keyboard.conf_path().exists()


def test_a_keymap_that_does_not_compile_is_refused(xkb, monkeypatch):
    monkeypatch.setattr(keyboard.subprocess, "run",
                        lambda *a, **k: subprocess.CompletedProcess(a, 1, stdout="", stderr="bad symbols"))
    with pytest.raises(ValueError, match="doesn't compile"):
        keyboard.set_layout("assi")


def test_a_layout_in_local_conf_is_reported_as_winning(xkb):
    local = keyboard.config_home() / "hypr" / "local.conf"
    local.parent.mkdir(parents=True, exist_ok=True)
    local.write_text("input {\n    kb_layout = no\n}\n")
    assert keyboard.status()["overridden_by"]["layout"] == "no"
