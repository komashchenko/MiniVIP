# MiniVIP
MetaMod:Source plugin that adds basic VIP player features for CS2

## Features
`health` - amount of base health<br>
`armor` - amount of armor with helmet<br>
`gravity` - fravity multiplier<br>
`money_min` - minimum amount of money bounds<br>
`money_add` - amount of money that will be adds after spawn<br>
`defuser` - diffuse kit for CT-side<br>
`items` - items given at spawn (separate by spaces)<br>
`smoke_color` - color of smoke. R G B or random

## Commands
`mini_vip_reload` - reloads a plugin config

## Installation
1. Install [Metamod:Source](https://www.sourcemm.net/downloads.php/?branch=master)
2. Grab the [latest release package](https://github.com/komashchenko/MiniVIP/releases)
3. Extract the package and upload it to your gameserver
4. Use `meta list` to check if the plugin works

## Build instructions
Just as any other [AMBuild](https://wiki.alliedmods.net/AMBuild) project:
1. [Install AMBuild](https://wiki.alliedmods.net/AMBuild#Installation)
2. Download [CS2 SDK](https://github.com/alliedmodders/hl2sdk/tree/cs2)
3. Download [Metamod:Source](https://github.com/alliedmodders/metamod-source)
4. `mkdir build && cd build`
5. `python ../configure.py --enable-optimize --sdks=cs2 --targets=x86_64 --mms_path=??? --hl2sdk-root=???`
6. `ambuild`
