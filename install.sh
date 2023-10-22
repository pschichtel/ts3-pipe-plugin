#!/usr/bin/env bash

set -euo pipefail

make && cp pipe_plugin.so $HOME/.ts3client/plugins
