from mobarchy_agent import talk


def test_sentences_come_as_they_complete():
    s = talk.Sentences()
    assert s.feed("Hei! Jeg er Fri") == ["Hei!"]
    assert s.feed("gg. Hva kan jeg") == ["Jeg er Frigg."]
    assert s.feed(" gjøre for deg") == []
    assert s.flush() == ["Hva kan jeg gjøre for deg"]


def test_sentences_lose_markdown_and_links():
    s = talk.Sentences()
    assert s.feed("**Oslo** i dag: [yr](https://yr.no) sier sol. ") == ["Oslo i dag:", "yr sier sol."]


def test_long_runs_break_at_a_comma():
    s = talk.Sentences()
    out = s.feed("a" * 120 + ", " + "b" * 120)
    assert len(out) == 1 and out[0].endswith(",")


def test_endpointer_finds_a_turn():
    ep = talk.Endpointer()
    probs = [0.1] * 5 + [0.9] * 30 + [0.1] * 40
    events = [(i, e) for i, p in enumerate(probs) if (e := ep.step(p))]
    assert events[0] == (7, "start")          # third window over the threshold
    assert events[1][1] == "end"
    silence_windows = events[1][0] - 34
    assert abs(silence_windows * talk.WINDOW / talk.RATE - talk.END_SILENCE_S) < 0.05


def test_stream_lines():
    line = '{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Hei"}}}'
    assert talk.parse_stream_line(line) == [("text", "Hei")]
    tool = '{"type":"assistant","message":{"content":[{"type":"tool_use","name":"mcp__mobarchy__battery_status"}]}}'
    assert talk.parse_stream_line(tool) == [("tool", "battery_status")]
    assert talk.parse_stream_line('{"type":"result","is_error":false}') == [("end", "")]
    assert talk.parse_stream_line("not json") == []


def test_blocks_are_not_glued():
    s = talk.Sentences()
    stop = '{"type":"stream_event","event":{"type":"content_block_stop"}}'
    out = s.feed("Jeg sjekker") + s.feed(talk.parse_stream_line(stop)[0][1]) + s.feed("været nå. ")
    assert out == ["Jeg sjekker været nå."]
