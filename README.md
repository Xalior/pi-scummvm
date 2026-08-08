# pi-scummvm

**ScummVM running directly on a Raspberry Pi with no operating system.** The
board powers on and the adventure game is what boots: no Linux, no desktop, no
launcher, and nothing else running beside it.

It builds for the Raspberry Pi 3, Pi 4 and Pi 5, all three from one source
tree.

![Sam & Max Hit the Road running on a Raspberry Pi 5 with no operating system](docs/scummvm-on-bare-metal.jpg)

*Captured from the Pi 5's HDMI output.*

## What this is

[ScummVM](https://www.scummvm.org/) plays the classic point-and-click
adventure games by re-implementing the engines they were written for. This
repository is the thin layer that lets it run with nothing underneath: a
[Circle](https://github.com/rsta2/circle) kernel that brings the board up, and
[circle-libsdl2](https://github.com/Xalior/circle-libsdl2), an SDL2
implementation built on Circle's bare-metal drivers.

ScummVM's own source is not copied or modified here. It is a submodule, pinned
at an upstream release, and the build reads it without ever writing to it.
Where ScummVM needs something the SDL2 layer does not provide, this repository
supplies it in `host/` rather than changing ScummVM.

![ScummVM running on a Raspberry Pi 5 with no operating system](docs/scummvm-on-bare-metal.jpg)

*Captured from the Pi 5's HDMI output. The board is running this image and
nothing else — no kernel underneath it, no window system, no launcher.*

## One engine: SCUMM

**This build plays the LucasArts adventures.** ScummVM supports well over a
hundred engines, and building all of them makes a kernel image far larger
than a Raspberry Pi boots comfortably. This one carries the SCUMM engine:

- The Secret of Monkey Island, Monkey Island 2, The Curse of Monkey Island
- Day of the Tentacle, Maniac Mansion
- Sam & Max Hit the Road
- Indiana Jones and the Last Crusade, Indiana Jones and the Fate of Atlantis
- Loom, Zak McKracken, The Dig, Full Throttle

The Humongous Entertainment games — Putt-Putt, Freddi Fish, Backyard Sports —
are not included; they need things this port does not have.

## What works

The games play, with sound and music.

- **Picture.** The older games draw at 320x200 and ScummVM scales that to
  640x480; the later ones draw 640x480 themselves. Either way it is scaled
  once more onto your screen.
- **Mouse and keyboard.** Both, and the mouse is what matters — a SCUMM game
  is played entirely with a pointer.
- **Music.** Through ScummVM's own synthesisers: Adlib, FM Towns, PC-98 and
  the Commodore 64 sound chip.
- **Saved games and settings.** Written back to the SD card.

What is missing:

- **Compressed files of any kind.** Everything ScummVM reads must already be
  unpacked, including its own interface theme, which upstream ships as a zip.
  See *Putting it on a card* below.
- **Compressed audio.** Some releases replace the original sound files with
  Ogg, MP3 or FLAC versions. Use the game's original files.
- **The clipboard, the file browser, the screen saver.** There is no desktop
  for any of them to belong to.

One thing you may notice: music timing can stumble at a scene change, because
a game loading a room does not come up for air often enough for the music
clock to be serviced on time. Nothing is lost, only delayed.

## What you need to supply

**This repository ships no game data.** A SCUMM game's rooms, artwork,
scripts, speech and music are the publisher's, not ScummVM's and not this
project's. Building the images does not download anything, and neither does
staging a card — `make media` is a separate, explicit step, and the one
game it can fetch (see below) is a freely distributable demo, not a
retail release.

ScummVM's own [game
documentation](https://wiki.scummvm.org/index.php/Category:Supported_Games)
lists what each game's files are called. As a rule you need every file the
game shipped with, most often a small handful:

| Typical files | Example |
|---|---|
| `*.000` and `*.001` | `MONKEY.000`, `MONKEY.001` — Monkey Island 2 and later |
| `*.LFL` | `00.LFL`, `01.LFL` … — Maniac Mansion, Zak McKracken, Indiana Jones |
| `*.LA0` / `*.LA1`, `*.SM0` / `*.SM1` | Day of the Tentacle, Sam & Max |
| `*.LA0` plus `*.SAN` | Full Throttle, The Dig — the `.SAN` files are the videos |

### Where they legitimately come from

**A copy you own.** The Steam, GOG and disc releases all install the game's
files as ordinary files. Copying those files is all that is needed.

**One game has a legitimately free demo, and `make media` fetches it.**
LucasArts gave away a demo of Day of the Tentacle on magazine cover discs in
1997, and it is still freely available: the SCUMM engine's own index and
resource files, its speech and sound resource, and its music driver data —
the same shape the full game uses, just the one demo scenario. Checked
against ScummVM's own published freeware list first: none of the eleven
games it names use the SCUMM engine, so this demo is the only legitimately
free SCUMM data this build can fetch for you.

```sh
make media      # fetches the Day of the Tentacle demo into media/game/
```

It downloads one zip, verifies it against the checksum computed the first
time this project fetched it (no independently published checksum exists for
this file), unpacks it, and writes `media/provenance.txt` recording where it
came from and under what terms.

Every other SCUMM game, including the full Day of the Tentacle, is a
commercial product with no free equivalent. There is no address this project
could honestly download one from — copy your own into `media/game/`,
replacing the demo's files, and stage the card again.

### Where to put them

```sh
mkdir -p media/game
cp /path/to/your/game/* media/game/
make card
```

`media/` is where a game's files live **on the machine building the card**. It
is not tracked by git and it is never part of anything this repository
publishes. `make card` copies it onto the staged card and downloads nothing,
ever. A card staged with `media/` empty is complete except for the game, says
which part is missing, and is a perfectly good build — it is what a
continuous-integration runner produces.

Two practical points:

- **Unpack everything first.** Game downloads usually arrive as a `.zip`, and
  nothing on the board can open one.
- **Keep filenames 8.3-safe and do not rely on capitalisation.** The card is
  FAT, where `MONKEY.000` and `monkey.000` are the same file.

## Building

You need a Linux or macOS machine, GNU make, and the Arm GNU toolchain for
`aarch64-none-elf` (release 15.2.Rel1). Put its `bin` directory on your
`PATH`, or unpack it into `toolchains/` in this repository.

```sh
git clone --recursive https://github.com/Xalior/pi-scummvm.git
cd pi-scummvm
make deps       # long: builds newlib and libc++ from source, once per board
make kernels    # the three board images
make verify     # confirms each image exists and is not empty
```

`make deps` is the slow step, and it is slow once. It builds a complete C and
C++ world for each board, because each board's world is compiled for its own
processor.

Part of that world is libc++, whose sources are fetched from a git tag that
carries the bare-metal patches. That tag is hosted on Codeberg, which is small
and volunteer run. One copy is enough for every board and for every project on
your machine, so tell the build where to keep it and it is fetched once:

```sh
make deps CIRCLE_LLVM=/path/to/circle-llvm
```

The default puts that checkout beside this repository, which is the right
answer for a plain clone or a continuous-integration runner and needs no
setting at all.

`make kernels` compiles about five hundred ScummVM source files per board and
takes a while even on a fast machine. The images land in `host/build/<board>/`:

| Board | Image |
|---|---|
| Pi 3 | `host/build/rpi3/kernel8.img` |
| Pi 4 | `host/build/rpi4/kernel8-rpi4.img` |
| Pi 5 | `host/build/rpi5/kernel_2712.img` |

Building one board on its own is `make rpi5`, and its dependencies alone are
`make deps-rpi5`, which is what a machine without room for three worlds wants.

`make -C host sources` prints every ScummVM file this build compiles, which is
the answer to "is that file in the image" when memory is not good enough.

## Putting it on a card

```sh
make card
```

That stages the card into `build/sd-card/` for you to copy onto FAT32 media:
the three kernel images under the names each board's firmware looks for, the
boot configuration, and ScummVM's own support files in `games/scummvm/`.

That includes the game's files, if `media/game/` has any in it. Nothing is
downloaded at any point.

One thing is never staged and has to be added by hand: **the Raspberry Pi
firmware files** — `bootcode.bin`, `start*.elf`, `fixup*.dat` and, for the
Pi 4, `armstub8-rpi4.bin`. Take them from a Raspberry Pi OS card or from the
[firmware repository](https://github.com/raspberrypi/firmware).

The finished card looks like this:

```
kernel8.img  kernel8-rpi4.img  kernel_2712.img   the three boards' images
config.txt  cmdline.txt                          boot configuration
games/scummvm/themes/scummclassic/               the interface theme, unpacked
games/scummvm/game/                              one game, copied from media/
games/scummvm/scummvm.ini                        written on first run
games/scummvm/saves/                             written when you save
```

**`game/` holds one game, and the board works out which one it is.** The
kernel starts ScummVM pointed at that directory and asks it to identify what
is in there, so there is nothing to configure: put Day of the Tentacle's files
in `media/game/` and Day of the Tentacle boots. To play a different game,
change what is in the directory and stage the card again.

**Everything ScummVM touches is under `games/scummvm/`, and nothing is written
to the card's root.** One card can carry several of these games, and two of
them writing a configuration file into the root would each silently overwrite
the other's.

The interface theme is staged as an unpacked directory rather than the
`scummclassic.zip` upstream also ships, because this build has no zlib and
cannot open a zip. If you replace the theme, unpack your replacement the same
way.

### Keeping it cool

The card carries `cmdline.txt`, which sets the temperature the board is
allowed to reach and the pin its fan is on:

    socmaxtemp=70 gpiofanpin=45

Pin 45 is the Raspberry Pi 5 Case Fan and Active Cooler. With a fan named,
reaching 70°C switches the fan on and the processor keeps running at full
speed. Without one it would be slowed down instead, and a slowed processor
drops frames.

If your fan is wired somewhere else, change the pin number.

## License

The code in this repository — the kernel layer in `host/` and the build — is
released under the GNU Lesser General Public License, version 3. See
[LICENSE](LICENSE).

The submodules are other people's work and carry their own terms, and both
matter before you distribute anything you build here:

- **ScummVM** is released under the GNU General Public License, version 2 or
  later.
- **Circle** is released under the GNU General Public License, version 3.

Building a kernel image here combines all of them, and the result is covered
by the GNU General Public License, version 3. Doing that for yourself is
straightforward; redistributing the result means satisfying every one of those
terms at once, including supplying complete source.

The SCUMM games are the property of their publishers. This project is not
affiliated with any of them, nor with the ScummVM project.
