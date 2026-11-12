# Uninstall

## On the Mac

Quit Resolve, then in Terminal:

```
cd ~/Downloads/Velaria-1.0.x
sudo ./install.sh --uninstall
```

If you deleted the folder, unzip the download again to get the installer back.

**What it removes**

- The Velaria driver. If Blackmagic Desktop Video was set aside when you installed, it is put back.
- The Resolve bridge, the USB helper and the experimental NDI helper, and their background services.
- Velaria's copy of `adb`, if the installer placed one.

**What it leaves, and how to remove it by hand**

| Left behind | Why | To remove it |
|---|---|---|
| The Velaria effects in Resolve | So your existing projects keep working | Delete the folder `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects/Velaria` and the files named `Velaria *.setting` in `.../Fusion/Macros` |
| The script `Skylight_UpgradeMacros.lua` | It belongs with the effects | Delete it from `.../Fusion/Scripts/Comp` |
| Your settings file | So a reinstall keeps your settings | Delete `~/Library/Application Support/Velaria` |
| Log files | | Delete `~/Library/Logs/Velaria` |
| Montserrat fonts | Other projects may use them | Remove them in Font Book |

Restart Resolve afterwards. If you use a Blackmagic card, choose it again in **Preferences > Video and Audio I/O**.

## On the headset

**Settings > Apps > Velaria View > Uninstall.**

You can also turn developer mode off in the Meta Horizon app if you no longer need it.

## Switching between Velaria and a Blackmagic card

In 1.0 the two cannot be active together. To use your DeckLink or UltraStudio, uninstall Velaria as above. To come back, run `sudo ./install.sh --replace-blackmagic`. Your effects and settings are kept either way.
