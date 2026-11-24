# Intel HDA bring-up

`audioconf` prints a read-only PCI snapshot. `doas audioconf --codec` performs
an explicit controller reset and queries codec widgets with immediate commands.
The supported playback test is `doas audioconf --tone`.

The real-machine snapshot identifies Intel 8086:a348 and codec 10ec:0235,
subsystem 17aa:36eb. The rear green line-out is pin 0x1b, whose first connection
is mixer 0x0c, whose first connection is DAC 0x02. The diagnostic also sees an
Intel display codec at address 2; it is not selected for the analog test.

The tone profile revalidates the widgets, pin defaults, first connections and
48 kHz/stereo/16-bit support before changing the route. It powers the audio
function and relevant widgets, unmutes the selected DAC input, mutes mixer
loopback inputs, enables output/EAPD, then plays two seconds of a quiet 440 Hz
signal with fade-in/out through a private BDL and PCM DMA buffer. No interrupts
are enabled. A QEMU 1af4:0022 profile exercises the same DMA path.

The test stops RUN, disables bus mastering and mutes the output before freeing
DMA memory. If both RUN stop and bus-master disable fail, the buffer and PCI
leases remain reserved until reboot. Other codecs are not configured for
playback. This is a bring-up test, not a general-purpose PCM device API.

Codec and tone reports are separately cached for the boot. The tone plays only
on the first `--tone` call after boot; later calls display its cached report.
The `/dev/hda` diagnostic ioctls require root. The image's doas
policy permits UID 1000 to run these two exact audioconf commands.

The QEMU test under `src/build/audio-check` booted the tone command via doas from
UID 1000, with HDA, AC97 and no sound controller. HDA advanced DMA with no
FIFO/descriptor errors. Captured stereo PCM was 48 kHz, 16-bit, approximately
two seconds, 440 Hz, with matching channels. Real analog output still needs a
listening check on the target machine.
