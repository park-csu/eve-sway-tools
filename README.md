# eve-sway-tools

Sway workspace management and a focus-aware Vulkan FPS limiter for
multi-client EVE Online on native Wayland.

## Features

- Moves EVE clients to a configurable Sway workspace.
- Keeps all clients in one flat tabbed container and restores configured order.
- Identifies each launcher client by any of its (up to three) characters.
- Cycles clients within groups and switches between groups.
- Remembers the last active client in each group.
- Allows a character to appear in more than one group.
- Caps inactive clients while keeping the active client at a separate FPS cap.
- Skips clients that are not running.
- Validates the configuration before starting the manager.

## Requirements

- Sway
- Python 3
- `jq`
- `flock`
- A C compiler
- Vulkan headers
- EVE Online running through 64-bit DXVK/Proton with Wine Wayland

Fedora packages for the build dependencies:

```sh
sudo dnf install gcc vulkan-loader-devel vulkan-headers
```

## Install

```sh
git clone https://github.com/park-csu/eve-sway-tools.git
cd eve-sway-tools
./install.sh
```

Add the manager to the Sway configuration:

```text
exec_always --no-startup-id ~/.local/bin/eve-sway-manager
for_window [app_id="^exefile[.]exe$"] floating disable, fullscreen disable
```

For GE-Proton, enable the implicit Vulkan layer through `user_settings.py`:

```python
if os.environ.get("SteamAppId") == "8500":
    user_settings["EVE_SWAY_TOOLS_ENABLE_FPS"] = "1"
```

Restart the EVE launcher and clients after enabling the layer.

## Configuration

Copy `config.example.yaml` to:

```text
~/.config/eve-sway-tools/config.yaml
```

Example:

```yaml
workspace: 10

bindings:
  group_next: Ctrl+Tab
  group_previous: Ctrl+Shift+Tab
  client_next: Tab
  client_previous: Shift+Tab

fps:
  active: 120
  inactive: 10

client_groups:
  main_client:
    - Main Character
    - Main Character Alt
  alt_client:
    - Alt One
    - Alt Two

cycle_groups:
  main:
    - main_client
  alts:
    - alt_client
```

Bindings are optional. Values use Sway `bindsym` syntax.
`client_groups` gives each EVE launcher client a stable name, so switching
characters (and therefore changing the window title) does not change its
cycle group or tab order. A client group accepts one to three unique character
names. `cycle_groups` entries refer to these stable client-group names.

For compatibility, configurations without `client_groups` continue to treat
`cycle_groups` entries as character names.

Validate the configuration:

```sh
eve-sway-cycle validate
```

Changing the configuration requires restarting the manager or reloading Sway.

## Commands

```text
eve-sway-cycle validate
eve-sway-cycle arrange
eve-sway-cycle client-next
eve-sway-cycle client-previous
eve-sway-cycle group-next
eve-sway-cycle group-previous
```

## How the limiter works

`VK_LAYER_EVE_sway_tools` intercepts `vkQueuePresentKHR`. The manager mirrors
the focused EVE PID and configured limits into Proton's mount namespace.
The active client uses `fps.active`; all other clients use `fps.inactive`.
When the configured workspace is not focused, every client uses the inactive
limit.

The layer only applies to processes whose main process name is `exefile.exe`.

## License

MIT
