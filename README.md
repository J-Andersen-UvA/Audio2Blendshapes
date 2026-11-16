# Audio2Blendshapes

Win64 editor plugin for Unreal Engine 5.7. It converts PCM, WAV, or MP3 audio into timestamped blendshape weights using NVIDIA's standalone Audio2Face-3D SDK. Inference runs on the local GPU. The plugin has no dependency on NVIDIA ACE, Piper, or HandposeTimeline.

## Setup

1. Build the [NVIDIA Audio2Face-3D SDK](https://github.com/NVIDIA/Audio2Face-3D-SDK) for Windows in Release mode with CUDA 12.9 and TensorRT 10.13. The plugin expects the SDK's default output layout, including `_build/release/audio2x-sdk/include`, `lib/audio2x.lib`, and `bin/audio2x.dll`.
2. Obtain the public [Claire v2.3.1 regression model](https://huggingface.co/nvidia/Audio2Face-3D-v2.3.1-Claire/tree/main). Build its `network.trt` from `network.onnx` on the target GPU using the shape profiles in the model's `trt_info.json`. Keep the engine and companion files beside `model.json`. The default model location is `_data/audio2face-models/audio2face-3d-v2.3.1-claire/model.json` under the SDK root.
3. Place this folder under the Unreal project's `Plugins` directory and build the Development Editor target. Enable **Audio2Blendshapes** under **Edit → Plugins** if needed.

The workstation defaults are `D:/Code/Audio2Face-3D-SDK`, CUDA `v12.9` under Program Files, and TensorRT under `_toolkits/TensorRT-10.13.2.6` inside the SDK checkout. For a different setup, set `AUDIO2FACE_SDK_ROOT` for the build and editor, plus `CUDA_PATH` and `TENSORRT_ROOT_DIR` for the editor. Point these variables at the installation roots, then restart the editor. SDK binaries, CUDA/TensorRT libraries, and model weights are installed separately. Review their respective terms before redistributing them.

## Blueprint workflow

Use an Editor Utility Blueprint or Widget.

- **Generate Blendshapes from PCM** accepts interleaved float samples, sample rate in Hz, and channel count. For Piper, break the synthesis result's **Audio** struct and connect **Samples** and **Sample Rate**. Set **Channels** to **1**. Run this after Piper reports success.
- **Generate Blendshapes from File** accepts an absolute path to a **WAV** or **MP3**. Windows Media Foundation decodes the file. Enable the Windows Media Feature Pack if Media Foundation is missing on a Windows N installation.
- Both nodes run asynchronously. **Completed** returns the clip. **Failed** returns a message. Optionally supply a path to a compatible regression model's `model.json`. The default is Claire and the SDK receives neutral emotion input.
- **Export Live Link Face Style CSV** takes a clip and a writable absolute `.csv` path. It writes `Timecode,BlendshapeCount` followed by the model's curve names and weights. Create the destination folder before exporting. Timecode starts at zero with a 60 fps frame field.

## Clip format and consuming it

The clip contains ordered **Curve Names**, **Frames**, nominal **Frame Rate**, and original **Audio Duration Seconds**. Each frame contains **Time Seconds** relative to the first audio sample and a **Weights** array indexed by the clip's curve names. The names come from NVIDIA's skin and tongue solvers in their output order. The input is mixed to mono and resampled to 16 kHz before solving.

Play the source audio and sample the clip on the same timeline. For an animation edit, map the source names to the avatar's curve names, then replace the selected channels over the desired time range. A mouth/jaw-only mapping lets the destination keep its original blink and gaze animation. This plugin supplies the data for that editing step.

The CSV uses the header and timecode style of this project's LiveLinkFaceCSVWriter. It carries the model's native channels, including Claire's additional tongue curves. A strict importer that expects an exact iPhone channel set or order will need a channel mapping.
