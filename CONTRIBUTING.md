# Contributing to ESP-AgriNet Zigbee

Thanks for your interest in improving ESP-AgriNet Zigbee! This document explains how to contribute.

---

## Development setup

1. Fork and clone the repo:

```bash
git clone https://github.com/<your-username>/esp-agrinet-zigbee-glm5.2.git
cd esp-agrinet-zigbee-glm5.2
```

2. Set up ESP-IDF v5.2.3 and esp-zigbee-sdk as described in [docs/BUILD.md](docs/BUILD.md).

3. Create a branch:

```bash
git checkout -b feature/my-new-feature
```

4. Build and test your changes locally:

```bash
. $IDF_PATH/export.sh
./scripts/build_all.sh
```

5. Run clang-format on your changes:

```bash
find . -name '*.c' -o -name '*.h' | grep -v build/ | xargs clang-format -i
```

6. Commit and push:

```bash
git add -A
git commit -m "feat: short description of the change"
git push origin feature/my-new-feature
```

7. Open a pull request against `main`.

---

## Commit message conventions

We follow a simplified [Conventional Commits](https://www.conventionalcommits.org/) style:

- `feat: ...` — new feature
- `fix: ...` — bug fix
- `docs: ...` — documentation only
- `refactor: ...` — code restructure, no behavior change
- `test: ...` — test additions
- `ci: ...` — CI / build system changes
- `chore: ...` — misc

---

## Code style

- 4-space indentation
- Column limit 100
- `clang-format` is the source of truth (see `.clang-format`)
- All function definitions in `.c` files; declarations in `.h` files
- Header guards: `#pragma once`
- Always include the project header comment at the top of every new file
- Use `AG_LOGI/AG_LOGW/AG_LOGE` from `agrinet_log.h` instead of raw `ESP_LOGI/...`

---

## Testing

There is no automated hardware-in-the-loop test in this repo yet. For now, please test your changes on real hardware before opening a PR:

- Build all four firmware targets (use `./scripts/build_all.sh`)
- Flash to the appropriate devices
- Verify the MQTT topics and payloads are correct (subscribe to `agrinet/#`)
- Verify actuator commands work end-to-end

---

## Reporting bugs

Open a GitHub issue with:

1. The exact firmware version (git commit hash)
2. The target device and ESP-IDF version
3. Steps to reproduce
4. Expected vs. actual behavior
5. Relevant log output (use `idf.py monitor` and capture at least 30 seconds)

---

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE).
