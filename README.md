
# vpcm

vpcm is a kernel extension similar to SoundFlower (https://github.com/mattingalls/Soundflower).
Unlike SoundFlower, it does not only create virtual sound devices in your computer's GUI, but also entries for virtual devices in your computer's `/dev` filesystem.

This is is useful in conjunction with existing sound tools from Linux available through Homebrew or MacPorts, or to send audio data over the network using a streaming tool like `netcat`.

Once vpcm has been installed, a new device entry `/dev/vpcmctl` exists in the `/dev` filesystem. Using the `echo` command, it is possible to send commands to the vpcm controller in order to create or delete virtual sound devices. Reading from `/dev/vpcmctl` will provide you with an overview of existing vpcm devices. Sending the `create` command will create a new vpcm:
```shell
$ echo create --rate=48000 --buffer-frames=1024 MyDevice >/dev/vpcmctl
$ cat /dev/vpcmctl
vpcm Virtual Audio Device, built Mar 27 2020 22:54:55
Number of device pairs: 1
"MyDevice" -> /dev/vpcm1
  CoreAudio clients: 0
  Device node state: closed
  Configuration:
	--playback
	--rate=48000
	--channels=2
	--buffer-frames=1024
	--latency-msec=0
	--format=float32le
	--overflow=zeros
```
In your computer's GUI, a new entry "MyDevice" will appear under audio devices. Choosing that for output, audio data will now be available to read from `/dev/vpcm1`. This will be raw data in the format described by the device options, and may be piped into a command line tool or written to disk:
```shell
$ cat /dev/vpcm1 > myaudio.raw
```
The following options are available when creating a device:
* `--playback` or `--record` to choose the direction into which the device operates.
* `--rate=<sampling rate>` to choose the sampling rate.
* `--channels=<number of channels>` for the number of playback or recording channels.
* `--buffer-frames=<frames>` for the device's internal buffer size in terms of audio frames.
* `--latency-msec=<latency>` for the device's nominal latency (used by the system when synchronizing audio and video).
* `--format=<float32|s16>` to choose the number format used.
* `--overflow=<zeros|noise|discard>` to specify what happens when the device is running out of data.
* `--[no-]eof-on-idle` determines whether a pipe or output file is closed as soon as the audio engine side of the device is idle.
* `--raw` to omit volume scaling and clipping operations on sample data.
* `--posix_pipe` will report EPIPE (broken pipe) to I/O requests if there is no active client on the GUI side. Some command line tools require this to work if data is piped to or from a vpcm device.
