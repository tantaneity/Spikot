# Spikot

A cat with a brain you can watch think.

Most game AI is a state machine wearing a trench coat, or an RL agent that did all its learning offline and now just replays a frozen policy. Spikot does neither. The cat runs on a spiking neural network written from scratch: leaky integrate-and-fire neurons, synapses that rewire through STDP, learning that happens live while you watch. No pretraining. The cat starts dumb and gets less dumb in front of you.

The visual is procedural pixel art. Every cat is seeded from a genome (body size, ear angle, tail length, fur, colors), so no two look the same. Emotions aren't canned animations, they're driven by neural activity: scared ears drop and pupils blow up, curious ears tilt forward, angry tail arcs.

## Stack

```
C        SNN simulation (LIF neurons + STDP), raw speed
raylib   rendering, OpenGL under the hood, native C
```

No shaders. The look comes from primitives and tiny pixel textures upscaled with point filtering.

## Build

You need a C compiler and CMake. raylib pulls itself in through CMake FetchContent, so there's nothing to install by hand.

```
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
./build/bin/spikot.exe
```

First configure takes a minute while raylib clones and compiles. After that it's fast.

## Layout

```
src/
  main.c        window + game loop
  config.h      window / sim constants
  snn/          neuron struct, network, STDP (coming)
  cat/          genome + procedural pixel generator (coming)
  env/          grid world, food, obstacles (coming)
```

## Why SNN and not regular RL

STDP is local and online. A synapse strengthens or weakens based purely on which of its two neurons fired first, no backprop, no replay buffer, no training phase. Add a reward modulator (food is +1, hitting something is -1) and you get R-STDP, which is about as close to "biologically plausible reinforcement learning" as you can get in a few lines of C. The point isn't benchmark scores. The point is that the learning is legible: you can render every neuron as a dot, watch it spike, and actually see the cat figure things out.

## Status

Early. Window and loop are up. Next is the procedural cat, then the brain.
