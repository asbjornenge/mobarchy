import pytest

from mobarchy_mcp import camera

LIST = [[["0", "back", 90, 4000, 3000], ["1", "front", 270, 3840, 2600]]]


def test_parse_cameras():
    cams = camera.parse_cameras(LIST)
    assert cams[1] == {"id": "1", "facing": "front", "orientation": 270,
                       "max_width": 3840, "max_height": 2600}


def test_pick():
    cams = camera.parse_cameras(LIST)
    assert camera.pick(cams, "front")["id"] == "1"
    with pytest.raises(camera.CameraError, match="no external camera"):
        camera.pick(cams, "external")


def test_take_photo_checks_facing():
    with pytest.raises(camera.CameraError, match="back or front"):
        camera.take_photo("sideways")


def test_take_photo_tool(monkeypatch, tmp_path):
    import asyncio

    from mcp import Client

    from mobarchy_mcp import server
    from mobarchy_mcp.policy import Policy

    photo = tmp_path / "IMG.jpg"
    photo.write_bytes(b"\xff\xd8\xff\xd9")
    monkeypatch.setattr(server, "policy", Policy())
    monkeypatch.setattr(server, "locked", lambda: False)
    monkeypatch.setattr(camera, "take_photo", lambda facing="back": str(photo))

    async def call():
        async with Client(server.mcp) as c:
            return await c.call_tool("take_photo", {"facing": "front"})

    r = asyncio.run(call())
    assert [type(x).__name__ for x in r.content] == ["TextContent", "ImageContent"]
    assert str(photo) in r.content[0].text
