from mobarchy_mcp import network

SCAN = """bssid / frequency / signal level / flags / ssid
cc:f4:11:82:0b:c9\t2412\t-65\t[WPA2-PSK-CCMP][ESS]\thome
cc:f4:11:82:0b:c5\t5180\t-60\t[WPA2-PSK-CCMP][ESS]\thome
62:31:97:d3:cc:a0\t2412\t-88\t[ESS]\tcafe
aa:bb:cc:dd:ee:ff\t2437\t-70\t[WPA2-PSK-CCMP][ESS]\t
"""


def test_scan_keeps_strongest_bss_per_ssid_and_sorts():
    nets = network.parse_scan_results(SCAN)
    assert [n["ssid"] for n in nets] == ["home", "cafe"]   # hidden SSID dropped
    assert nets[0] == {"ssid": "home", "signal_dbm": -60, "freq_mhz": 5180, "secured": True}
    assert nets[1]["secured"] is False


def test_parse_status():
    s = network.parse_status("bssid=cc:f4\nfreq=5180\nssid=home\nwpa_state=COMPLETED\n")
    assert s["ssid"] == "home" and s["wpa_state"] == "COMPLETED" and s["freq"] == "5180"
