import os
import time

import pytest

from mobarchy_mcp import photos


@pytest.fixture
def pictures(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_PICTURES_DIR", str(tmp_path))
    (tmp_path / "Camera").mkdir()
    (tmp_path / "Screenshots").mkdir()
    (tmp_path / ".thumbs").mkdir()
    now = time.time()
    for i, (rel, age) in enumerate([("Camera/a.jpg", 30), ("Screenshots/s.png", 10), ("Camera/v.mp4", 20),
                                    ("loose.png", 40), (".thumbs/x.png", 1), ("Camera/notes.txt", 1)]):
        f = tmp_path / rel
        f.write_bytes(b"x")
        os.utime(f, (now - age, now - age))
    return tmp_path


def test_newest_first_with_albums(pictures):
    listed = photos.list_pictures()
    assert [p["path"].split("/")[-1] for p in listed["pictures"]] == ["s.png", "v.mp4", "a.jpg", "loose.png"]
    assert listed["albums"] == ["Camera", "Screenshots"]
    assert [p["video"] for p in listed["pictures"]] == [False, True, False, False]
    assert [p["album"] for p in listed["pictures"]] == ["Screenshots", "Camera", "Camera", ""]


def test_one_album(pictures):
    assert [p["path"].split("/")[-1] for p in photos.list_pictures("Camera")["pictures"]] == ["v.mp4", "a.jpg"]


def test_only_the_owners_pictures(pictures, tmp_path_factory):
    assert photos.check_path(str(pictures / "Camera/a.jpg")).name == "a.jpg"
    elsewhere = tmp_path_factory.mktemp("other") / "x.png"
    elsewhere.write_bytes(b"x")
    for bad in (str(elsewhere), str(pictures / "Camera/missing.jpg"), str(pictures / "Camera/../../etc/passwd")):
        with pytest.raises(photos.PhotosError):
            photos.check_path(bad)
