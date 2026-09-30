from mobarchy_mcp.policy import Decision, Policy, Risk


def test_defaults_deny_confirm():
    p = Policy()
    assert p.decide("screenshot", Risk.READ) is Decision.ALLOW
    assert p.decide("tap", Risk.ACT) is Decision.ALLOW
    assert p.decide("run_shell", Risk.CONFIRM) is Decision.DENY


def test_missing_file_uses_defaults(tmp_path):
    p = Policy.load(tmp_path / "nope.toml")
    assert p.source == "built-in defaults"
    assert p.decide("run_shell", Risk.CONFIRM) is Decision.DENY


def test_tool_override_beats_risk_level(tmp_path):
    f = tmp_path / "policy.toml"
    f.write_text('[risk]\nact = "deny"\n[tools]\nrun_shell = "allow"\ntap = "allow"\n')
    p = Policy.load(f)
    assert p.decide("run_shell", Risk.CONFIRM) is Decision.ALLOW
    assert p.decide("tap", Risk.ACT) is Decision.ALLOW
    assert p.decide("swipe", Risk.ACT) is Decision.DENY
