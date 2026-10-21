# Hardware interface requirements (for Samuel's research)

Goal of this document: say what the software side needs, and the questions
the hardware research must answer, before any purchase is considered.

## Honest scope
Consumer sensors cannot read a dream or send one person's experience to
another. What may be measurable is coarse physiological state (for example
movement, heart rate, maybe sleep stage with limited accuracy). Phase 4's job
is to find out which signals are real and reliable. Software only ever
receives an interpreted event plus a confidence; raw measurements stay on the
device side.

## What the software needs from a device
1. A way to send v1 events (docs/PROTOCOL.md): a small computer or phone
   between the sensor and the server is fine; the sensor itself does not need
   to speak JSON.
2. A stable device id (`source_id`) and a clock that is roughly correct (the
   server rejects timestamps more than 60 s ahead or 24 h old).
3. Retry safety: keep an event and resend with the same `id` until acked.
4. Local interpretation: raw signal to event happens before transmission, and
   raw streams are not uploaded by default.
5. A manual fallback for every consequential function (a physical button for
   session end). Wake or logoff must never depend on classification alone.

## Questions to answer per candidate sensor
- Which physical quantity does it measure, at what sample rate and resolution?
- Is raw data accessible without a cloud service or account? (Local-only,
  open-source tooling is preferred.)
- Linux support, and the connection (USB serial, BLE, Wi-Fi). Driver needs?
- Signal quality: noise, motion artifacts, electrode contact, drift over a night.
- What evidence exists that this signal indicates the state you care about?
  Cite studies; note sample sizes and whether results are per-person only.
- Power, comfort for sleeping, skin contact materials, safety certifications.
- What would it cost, and is any part single-source?

## Experiments to design (no purchases needed to plan them)
1. Define a narrow, testable claim, for example "detect a deliberate button-free
   signal like a defined eye movement or clench pattern while awake". Avoid
   claims about dream content.
2. Ground truth: how will you know the true state at each moment (a button the
   participant presses, a video, a lab-style protocol)?
3. Record raw data and ground truth together; compare against a baseline that
   guesses (accuracy must beat chance by a margin you set in advance).
4. Report false positives and false negatives, not only accuracy. A false
   LOGOFF_INTENT is a safety issue.
5. Decide thresholds from data, then set `confidence` mapping accordingly.

## Safety and privacy to settle early
- Wearing electronics during sleep: heat, skin irritation, battery and
  charging safety. Use only low-voltage, battery-powered, certified parts, and
  never mains-connected electrodes.
- Physiological data is sensitive: minimal collection, short retention, owner
  consent, deletion on request, nothing shared with other users by default.
- Vibration alarm: start with a button, a timer, or a software command. A
  sensor-triggered wake feature needs validation and a reliable manual override.

## Deliverable back to software
A one-page note per sensor: what it measures, how it connects, sample output
(a few lines of real data), the event types it could plausibly produce, and
measured accuracy against ground truth. Then we write the driver behind the
existing protocol.
