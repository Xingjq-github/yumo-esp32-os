# Recorder SpeexDSP port

Source: Xiph.Org speexdsp, tag SpeexDSP-1.2.1, from the official xiph/speexdsp repository.
Only preprocess, filterbank, float smallft and its wrapper are built. Preserve
upstream/COPYING and each source copyright in redistributed firmware materials.

Configuration: 16 kHz / 256 sample frames, noise suppression -12 dB, upstream
AGC and VAD disabled. The recorder's bounded gain/limiter remains in charge;
speech probability only limits extra gain, never deletes speech frames.
This is noise suppression, not dual-mic beamforming or acoustic echo cancellation.

One upstream patch: the echo-residual call in preprocess.c is guarded by
RECORDER_NO_ECHO, avoiding an unused echo-canceller link. Echo state stays NULL;
upstream controls are private and not exposed by this component's public API.
All other upstream sources are byte-identical to the tagged archive. Archive
and file hashes are in historical development records (not distributed).

Upstream init has unchecked allocations: the private allocator uses one 64 KiB
PSRAM arena and catches exhaustion inside C with setjmp/longjmp. It never
unwinds across C++ or leaves a partial heap allocation chain. Reset reuses the
arena; destroy releases it entirely. The only user is the serialized foreground
recorder. No preloading, persistent DSP task or model partition is introduced.
