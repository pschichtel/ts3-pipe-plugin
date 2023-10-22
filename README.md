# Pipe Plugin

This is a simple TeamSpeak 3 Client for Linux that reads actions from a named pipe/fifo and performs them within the
TS3 client.

The main purpose is to be able to perform certain actions using global keyboard shortcuts.

## Available Actions

* `toggle_speaker`: Toggle the speaker state on all connected servers (similar to the hotkey function)
* `toggle_microphone`: Toggle the microphone state on all connected servers (similar to the hotkey function)

## Usage

### GNOME

Configure a command similar to this as the shortcut command:

```bash
bash -c 'echo toggle_speaker > $HOME/.ts3client/plugins/pipe_plugin/commands.pipe'
```
