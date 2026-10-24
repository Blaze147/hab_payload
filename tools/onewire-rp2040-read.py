"""Build a copy of pinned OneWire with RP2040 timing-critical slots replaced.

The fallback GPIO API overhead makes sampling unreliable on this installation.
Keep upstream discovery/reset/CRC code and the dependency cache intact.
"""
from pathlib import Path

Import("env")


def replace_read_slot(build_env, node):
    source = Path(node.srcnode().get_abspath())
    text = source.read_text()
    signature = "uint8_t CRIT_TIMING OneWire::read_bit(void)"
    assert text.count(signature) == 1, "Pinned OneWire read slot changed; review patch"
    start = text.index(signature)
    end = text.index("\n}", start) + len("\n}")
    replacement = '''uint8_t CRIT_TIMING OneWire::read_bit(void)
{
    // RP2040 stores its pin number in bitmask. Sample ~13 us after pulling
    // low, using direct SIO and a bounded busy wait instead of GPIO API calls.
    const uint pin = bitmask;
    const uint32_t irq = save_and_disable_interrupts();
    gpio_put(pin, false);
    gpio_set_dir(pin, true);
    busy_wait_us_32(3);
    gpio_set_dir(pin, false);
    busy_wait_us_32(10);
    const uint8_t value = gpio_get(pin);
    restore_interrupts(irq);
    busy_wait_us_32(53);
    return value;
}'''
    patched = text[:start] + replacement + text[end:]
    if build_env.GetProjectOption("custom_onewire_direct_writes", "no") == "yes":
        signature = "void CRIT_TIMING OneWire::write_bit(uint8_t v)"
        assert patched.count(signature) == 1, "Pinned OneWire write slot changed; review patch"
        start = patched.index(signature)
        end = patched.index("\n}", start) + len("\n}")
        replacement = '''void CRIT_TIMING OneWire::write_bit(uint8_t v)
{
    const uint pin = bitmask;
    const bool one = (v & 1) != 0;
    const uint32_t irq = save_and_disable_interrupts();
    gpio_put(pin, false);
    gpio_set_dir(pin, true);
    busy_wait_us_32(one ? 6 : 65);
    // Preserve upstream strong-high behavior; write() releases the pin unless
    // its caller requests parasite power. Reset/presence behavior is unchanged.
    gpio_put(pin, true);
    restore_interrupts(irq);
    busy_wait_us_32(one ? 64 : 5);
}'''
        patched = patched[:start] + replacement + patched[end:]
    patched = '#include <hardware/gpio.h>\n#include <hardware/sync.h>\n' + patched
    target = Path(build_env.subst("$BUILD_DIR")) / "onewire-rp2040" / "OneWire.cpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != patched:
        target.write_text(patched)
    return build_env.File(str(target))


env.AddBuildMiddleware(replace_read_slot, "*OneWire.cpp")
