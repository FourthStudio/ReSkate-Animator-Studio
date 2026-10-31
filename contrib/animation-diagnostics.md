# One-button animation diagnostics

Reproduce a problem, open F8 and use SETTINGS > STUDIO > REPORT A BUG for the shareable ZIP.
For a local diagnostic folder, use the console command `animstudio dump`.
Opening the studio freezes the gameplay history so navigating the menus does not erase the bug.
No trace-start command is required. Opening the native pause menu also preserves the preceding gameplay window.

Reports are local folders under `ReSkate.Tricks/diagnostics/dump-...`:

- `summary.json`: format/build contract, capture scope, sample counts, largest callback gap,
  native-versus-desired joint deltas, invalid desired joints, current failure and pack name.
- `timeline.csv`: each observed animation callback, generation/sequence, physics state, trick,
  selected preset, enabled/recording flags, live/takeoff/recorded stance, mirror mode,
  source clip time, body/board blend weights, failures and render publication counts/ages.
- `poses.csv`: sampled parent-local transforms before custom edits, desired output before the
  finite check/publication, and the latest observed render pose before our override. Each stage
  carries its own timestamp; the render pose need not belong to the same animation update.
- `board.csv`: world-space mount transforms with separate animation/render timestamps.
- `skeleton.csv`: parent/mirror indices and editable joint labels.
- `presets/`: copies of referenced saved JSON/clip inputs, capped at 128 MiB total.
- `client-build-info.json`: installed build metadata, when present.

Metadata is bounded to 25 gameplay seconds or 8192 callbacks, whichever comes first. Full poses
are retained at up to 10 Hz (roughly 250 sets); a one-frame pose anomaly between those samples
can be missed. Callback timing is not throttled to the pose sampling rate. The rolling buffer
also works with custom playback disabled, allowing a native baseline capture. Capturing is in
memory; requested disk exports run in the background, one report at a time.

The report is not a process memory dump and does not upload anything. It does not yet expose
the active native graph branch, inertial-blend internals, rail/foot IK solver state or final GPU
pose. The skater's raw animation/render buffers are observed at defined hooks; they are not proof
that prior custom output cannot feed back through another engine pass. Exported stage timestamps
and generations must be compared before treating two poses as a paired frame.

The initial automated checks cover bounded storage/rate, invalid timestamp rejection, clock
reset, CSV escaping and export of known synthetic pose deltas. In-game button navigation and
the actual switch appearance still require a gameplay test on the exact release DLL.
