from mobarchy_mcp import clock

SHOW = """Timezone=Europe/Oslo
LocalRTC=no
CanNTP=yes
NTP=yes
NTPSynchronized=no
TimeUSec=Fri 2026-10-02 18:03:36 CEST
"""

ZONES = ["America/New_York", "America/North_Dakota/New_Salem", "Europe/Oslo", "Europe/London", "UTC"]


def test_parse_show():
    assert clock.parse_show(SHOW) == {"timezone": "Europe/Oslo", "network_time": True,
                                      "synchronized": False}


def test_match_timezones():
    assert clock.match_timezones("oslo", ZONES) == ["Europe/Oslo"]
    assert clock.match_timezones("New York", ZONES) == ["America/New_York"]
    assert clock.match_timezones("america new", ZONES) == ["America/New_York",
                                                           "America/North_Dakota/New_Salem"]


def test_set_unknown_timezone(monkeypatch):
    monkeypatch.setattr(clock, "timezones", lambda: ZONES)
    try:
        clock.set_timezone("Oslo")
    except ValueError as e:
        assert "Europe/Oslo" in str(e)
    else:
        raise AssertionError("expected ValueError")
