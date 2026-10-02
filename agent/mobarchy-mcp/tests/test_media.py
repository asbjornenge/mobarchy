from mobarchy_mcp import media


def test_parse_status():
    line = "Playing\tHjerteknuser\tKaizers Orchestra\tVioleta Violeta Volume I\t200093000\t61000000\n"
    assert media.parse_status(line) == {
        "status": "Playing", "title": "Hjerteknuser",
        "artist": "Kaizers Orchestra", "album": "Violeta Violeta Volume I",
        "length_s": 200, "position_s": 61}


def test_parse_status_missing_fields():
    assert media.parse_status("Paused\tA video\t\t\t\t3000000") == {
        "status": "Paused", "title": "A video", "position_s": 3}


def test_summarize_search():
    data = {"tracks": [{"id": "t1", "name": "Hjerteknuser", "artists": [{"id": "a", "name": "Kaizers Orchestra"}],
                        "album": {"id": "al", "name": "Violeta Violeta Volume I"}}],
            "artists": [{"id": "a", "name": "Kaizers Orchestra"}], "albums": [], "playlists": []}
    assert media.summarize_search(data) == {
        "tracks": [{"id": "t1", "name": "Hjerteknuser", "artists": "Kaizers Orchestra",
                    "album": "Violeta Violeta Volume I"}],
        "artists": [{"id": "a", "name": "Kaizers Orchestra"}]}


def test_play_needs_kind():
    try:
        media.spotify_play("song", name="x")
    except ValueError as e:
        assert "kind" in str(e)
    else:
        raise AssertionError("expected ValueError")
