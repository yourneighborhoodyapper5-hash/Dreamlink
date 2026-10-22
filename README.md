# Dreamlink

**An experimental multiplayer dream research and simulation platform.**

Dreamlink is an open-source project exploring the software foundations for connected, sleep-related experiences. It begins with simulated signals, structured events, and session-based experimentation, with future plans for hardware research and multiplayer sessions.

> **Scientific honesty:** Dreamlink does not currently detect dreams, read thoughts, or enable real dream sharing. Its initial software foundation focuses on simulated events and reliable infrastructure for future research.

## 🚧 Project Status

**Early development — Phase 1**

Dreamlink is being developed on Linux, with VEGA serving as the development server.

The initial C++ prototype defines the core event structures used by the system:

- `EventType` — identifies the kind of event.
- `Event` — contains an event ID, session ID, event type, confidence value, timestamp, and payload.

These structures form the foundation for event validation, serialization, simulation, and persistent storage.

Features should be considered experimental until implemented and tested.

## 🗺️ Roadmap

### Phase 1 — Software Foundation
- [ ] Establish and expand automated tests.
- [ ] Define a documented event wire format.
- [ ] Validate event fields and confidence values.
- [ ] Generate simulated events for testing.
- [ ] Implement an append-only SQLite event log.
- [ ] Test serialization, validation, persistence, and error handling.

### Phase 2 — Device Protocol
- [ ] Define how devices communicate with the server.
- [ ] Specify message formats, validation, and error handling.
- [ ] Establish a safe interface for future hardware integrations.

### Phase 3 — Website and Multiplayer
- [ ] Develop a web interface.
- [ ] Introduce shared sessions between users.
- [ ] Explore synchronized event streams and session communication.
- [ ] Add appropriate session controls and privacy safeguards.

### Phase 4 — Hardware Research
- [ ] Investigate affordable, suitable sensors.
- [ ] Research what available sensors can actually measure.
- [ ] Evaluate signal quality, limitations, and practical feasibility.
- [ ] Integrate hardware when the software and research justify it.

Hardware research is exploratory. No particular sensor is assumed to be capable of detecting dreams or transmitting dream experiences.

### Phase 5 — Wake and Session Controls
- [ ] Implement reliable session start and end controls.
- [ ] Provide a manual fallback for ending a session.
- [ ] Investigate wake-related features and their practical limitations.
- [ ] Consider safety, usability, and sleep disruption.

## 🧭 Design Principles

- **Open source:** Build transparently and welcome constructive contributions.
- **Linux first:** Prioritize Linux development and testing.
- **Local processing:** Prefer local computation and storage where practical.
- **No paid APIs:** Keep the core project independent of paid API services.
- **Minimal sensitive data:** Collect and retain only what is necessary.
- **Scientific honesty:** Clearly distinguish simulated events, measured signals, and unverified hypotheses.
- **Incremental development:** Establish a reliable foundation before introducing complex features.

## 🛠️ Technology

The initial software foundation uses:

- **C++17** for the core prototype.
- **SQLite** for planned persistent event logging.
- **Linux** as the primary development platform.
- **Automated tests** to verify correctness as the project evolves.

The technology stack may evolve as requirements become clearer.

## 🚀 Getting Started

Dreamlink is under active development, so setup instructions may change as the foundation matures.

## 🌐 Community

Dreamlink is being built with the help of people interested in software development, sleep research, sensors, hardware, design, and experimental technology.

You don't need to be an expert to participate. Beginners, curious people, testers, and experienced developers are all welcome.

### 💬 Join the Community

Join the Dreamlink Discord to:
- 💡 Share ideas and discuss possible features
- 🔬 Explore sleep research and sensor technology
- 💻 Contribute to development and documentation
- 🧪 Test prototypes and report bugs
- 🤝 Meet other people interested in the project

**Discord:** [Join the Dreamlink Community] (https://discord.gg/JGXAmsNb3n)

### 🛠️ Get Involved

You can contribute by writing code, researching hardware, improving documentation, testing features, suggesting ideas, or helping make the project better.

Check the repository's issues and contribution guidelines to see what needs work.

Everyone should be respectful, protect other people's privacy, and distinguish experimental ideas from scientifically verified results.

### Requirements

- A Linux environment.
- A C++17-compatible compiler.
- GNU Make, if using the project's Makefile.
- SQLite development libraries when required by the implementation.

### Clone the repository

Replace the placeholder below with the actual repository URL:

```bash
git clone https://github.com/yourneighborhoodyapper5-hash/Dreamlink
cd dreamlink
```
Nvm I did for you lazy people😡
