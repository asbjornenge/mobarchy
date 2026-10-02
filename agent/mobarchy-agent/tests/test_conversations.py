from mobarchy_agent import conversations as cv


def test_roundtrip_and_title(tmp_path, monkeypatch):
    monkeypatch.setattr(cv, "ROOT", tmp_path)
    c = cv.Conversation()
    c.add("user", "Plan a trip to Bergen next week, by train", "claude")
    c.add("assistant", "Sure. Which day?", "claude")
    c.save()
    again = cv.Conversation.load(c.id)
    assert again.title == "Plan a trip to Bergen next week, by train"
    assert [t.role for t in again.turns] == ["user", "assistant"]
    assert [x.id for x in cv.all_conversations()] == [c.id]


def test_switching_agents_bridges_what_was_missed():
    c = cv.Conversation()
    c.add("user", "Find a hotel in Bergen", "claude")
    c.add("tool", "screenshot", "claude")
    c.add("assistant", "Hotel Norge has rooms.", "claude")
    c.seen["claude"] = len(c.turns)
    # The owner switches to the local agent.
    c.add("user", "Book it", "local")
    prompt = c.bridged_prompt("local", "Book it")
    assert "Owner: Find a hotel in Bergen" in prompt
    assert "Assistant (claude): Hotel Norge has rooms." in prompt
    assert prompt.endswith("The owner now says:\nBook it")
    c.add("assistant", "Done.", "local")
    c.seen["local"] = len(c.turns)
    # Back to Claude: it only gets what happened while it was away.
    c.add("user", "Thanks", "claude")
    back = c.bridged_prompt("claude", "Thanks")
    assert "Find a hotel" not in back
    assert "Owner: Book it" in back and "Assistant (local): Done." in back


def test_nothing_missed_means_plain_prompt():
    c = cv.Conversation()
    c.add("user", "Hi", "claude")
    assert c.bridged_prompt("claude", "Hi") == "Hi"
